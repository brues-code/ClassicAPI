// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

// Priority targeting — the 3.80.2 Classic client's candidate list, which its
// soft targets read (docs/notes/soft-targeting-3802.md, "Soft-target
// candidate picker"). Every TargetPriorityUpdateDelay ms (100) the list is
// rebuilt in three steps, as 3.80.2's FUN_1405e3ee0 / 4b30 / 5800 do:
//
//   1. Gather: every unit in the object manager within the bank's far range
//      plus both reaches (dead units: 16 yd), never the player, a unit the
//      player possesses, or one with no model. Each is classified: Enemy
//      (alive and attackable; a PvP-flagged unit never, while the player is
//      not flagged), Friend (alive, friendly either way, selectable,
//      assistable), Critter (the creature type's never-tab-target flag),
//      Boss, and CombatLock (an enemy in combat while the player is engaged).
//   2. Geometry: InFrustum (the model's box against the view frustum pulled
//      in by 0.2 on every side), Visible (and in line of sight, for an enemy
//      that is not a player), PointBlank, InMelee, the angles to the
//      character's facing and to the camera's yaw, InRange (the bank's range
//      shrinking from 51 yd within 20° to 3 yd past 90°), Excluded (more than
//      90° behind both, not hitting the player in melee), InCone (the
//      camera's 18° cone, widened by the model's radius, with a weight),
//      CameraRay / FacingRay (the camera's or the facing's line passes
//      through the model).
//   3. Score: the bank's weighted terms — camera alignment, distance (mixed
//      linear and squared), a sideways term along the camera's second row,
//      distance from the target, melee, the facing and camera ease terms, the
//      cone, point blank, PvP, combat lock, boss and critter — then the soft
//      order, best first.
//
// The tables are 3.80.2's three banks; soft targeting uses bank 1 (its
// TargetPriorityValueBank default; bank 2 is hold-to-target, not here). Where 1.12 has
// the input, it is 1.12's own: reach is UNIT_FIELD_BOUNDINGRADIUS (what its
// melee and interact ranges add), the model's radius and height are the
// selection radius and box height the model sets, the frustum is the world
// frame's own view-projection, and line of sight is CWorld::Intersect with
// the LoS geometry, between the two units at half their model height.
//
// Not in 1.12, so not here: the "trivial" and the 0x1000 template bits, the
// spec's melee weight (MeleeA is used), unit flags3, the transport test of
// the exclusion, and the PvP-zone rules (TargetPriorityPvp; outside a PvP
// zone 3.80.2 only applies the not-flagged rule above). The tab-only terms
// (casting, group) are left out; only the soft order is built.

#include "target/Priority.h"

#include "Game.h"
#include "Offsets.h"
#include "creature/Info.h"
#include "cvar/Factory.h"
#include "object/Resolve.h"
#include "target/Nearest.h"
#include "target/Reason.h"
#include "time/Clock.h"
#include "unit/CreatureID.h"
#include "unit/Identity.h"
#include "unit/Position.h"
#include "unit/Relation.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Target::Priority {

namespace {

using Transform_t = float *(__fastcall *)(float *out, const float *v, const float *m);
using Row_t = void(__thiscall *)(void *camera, float *out);
using Intersect_t = char(__fastcall *)(const float *start, const float *end, int unused,
                                       float *hit, float *t, uint32_t flags);

constexpr float kPi = 3.14159265f;
constexpr float kDeg = 0.017453292f;

// 3.80.2's bank tables (decoded from its .rdata, by bank 0 / 1 / 2).
struct Bank {
    float rangeFar, rangeNear, angInner, angOuter, expA, expB, coneAng, interactRange;
    float fwd, distMix, distW, sideW, fromTargetW, melee, faceIn, faceOut, camIn, camOut;
    float cone, camRay, close, pvpPlayer, pvpNpc, combat, boss, critter, noLos;
};
constexpr Bank kBanks[3] = {
    {61, 4, 30 * kDeg, 120 * kDeg, 1, 1, 30 * kDeg, 16, 10, 0.3f, -10, -6, 0, 0, 0, 0, 200,
     1000, 0, 0, 0, 30000, 10000, 0, 0, 0, 0},
    {51, 3, 20 * kDeg, 90 * kDeg, 1, 2, 18 * kDeg, 16, 200, 0.5f, -10, -10, -50, 200, 100, 400,
     300, 500, 2000, 0, 1500, 30000, 10000, 40000, 1000, -20000, -4000},
    {100, 1, 5 * kDeg, 6 * kDeg, 0.4f, 10, 5 * kDeg, 16, 200, 0.25f, -1, 0, 0, 200, 0, 0, 0, 0,
     1600, 3000, 0, 100, 50, 100, 0, -20000, 0},
};

// The engine's registered values of its internal cvars (no help text, not
// in the shipped settings): the bank, the rebuild delay and the frustum
// pull-in.
constexpr int kBank = 1;
constexpr uint32_t kUpdateDelayMs = 100;
constexpr float kFrustumPullIn = 0.2f;
constexpr float kMinRadius = 0.5f;
constexpr float kCloseDistance = 1.5f;

struct Entry {
    uint64_t guid;
    void *object;
    uint32_t flags;
    float pos[3];
    float radius;
    float height;
    float aFace;
    float aCam;
    float cone;
    float score;
};

std::vector<Entry> g_entries;
std::vector<const Entry *> g_softOrder;
uint32_t g_allFlags = 0;
uint32_t g_lastBuild = 0;
bool g_built = false;

CVar::Factory::Handle g_combatLock = nullptr;
CVar::Factory::Handle g_relaxation = nullptr;

float Wrap(float a) {
    while (a > kPi)
        a -= 2 * kPi;
    while (a < -kPi)
        a += 2 * kPi;
    return a;
}

float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

float Dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

uint32_t UnitFlags(const void *unit) {
    const uint8_t *f = Object::Fields(unit);
    return f ? Game::Read<uint32_t>(f, Offsets::OFF_UNIT_FIELD_FLAGS) : 0;
}

bool Attacking(const void *unit) {
    return Game::Read<uint64_t>(unit, Offsets::OFF_UNIT_ATTACK_TARGET) != 0;
}

// The camera, its position, forward and second (left) rows. False without one.
struct Camera {
    void *worldFrame;
    const float *pos;
    float nearClip;
    float forward[3];
    float left[3];
    float yaw;
};

bool ReadCamera(Camera &c) {
    c.worldFrame = Game::Read<void *>(Offsets::VAR_GAME_STATE_PTR);
    void *camera = c.worldFrame
                       ? Game::Read<void *>(c.worldFrame, Offsets::OFF_GAME_STATE_CAMERA_PTR)
                       : nullptr;
    if (camera == nullptr)
        return false;
    c.pos = Game::Ptr<float>(camera, Offsets::OFF_CAMERA_POSITION);
    c.nearClip = Game::Read<float>(camera, Offsets::OFF_CAMERA_NEAR_CLIP);
    auto *vtable = Game::Read<uintptr_t *>(camera, 0);
    reinterpret_cast<Row_t>(vtable[Offsets::VTBL_CAMERA_FORWARD / sizeof(uintptr_t)])(camera,
                                                                                     c.forward);
    reinterpret_cast<Row_t>(vtable[Offsets::VTBL_CAMERA_LEFT / sizeof(uintptr_t)])(camera, c.left);
    if (c.forward[0] == 0 && c.forward[1] == 0)
        return false;
    c.yaw = std::atan2(c.forward[1], c.forward[0]);
    return true;
}

// The model's box (radius about the position, height up from it) against the
// view frustum, sides pulled in: rejected when every corner is outside one
// plane. Clip space is the world frame's view-projection of the offset from
// the camera, as GetScreenCoordinates builds it; a corner nearer than the
// near clip is behind.
bool BoxInFrustum(const Camera &c, const Entry &e) {
    const float *m = Game::Ptr<float>(c.worldFrame, Offsets::OFF_WORLDFRAME_VIEW_PROJECTION);
    const float k = 1.0f - kFrustumPullIn;
    uint32_t outside = 0x1F;
    for (int i = 0; i < 8; ++i) {
        const float corner[4] = {e.pos[0] + ((i & 1) ? e.radius : -e.radius) - c.pos[0],
                                 e.pos[1] + ((i & 2) ? e.radius : -e.radius) - c.pos[1],
                                 e.pos[2] + ((i & 4) ? e.height : 0.0f) - c.pos[2], 0.0f};
        float clip[4];
        reinterpret_cast<Transform_t>(Offsets::FUN_C44_TRANSFORM)(clip, corner, m);
        uint32_t out = 0;
        if (clip[0] < -k * clip[3])
            out |= 0x1;
        if (clip[0] > k * clip[3])
            out |= 0x2;
        if (clip[1] < -k * clip[3])
            out |= 0x4;
        if (clip[1] > k * clip[3])
            out |= 0x8;
        if (clip[2] < c.nearClip)
            out |= 0x10;
        outside &= out;
        if (outside == 0)
            return true;
    }
    return false;
}

// Line of sight is needed for an enemy that is not a player (FUN_1405e7030).
bool NeedsLos(uint32_t flags) { return (flags & (Enemy | Player)) == Enemy; }

bool InLineOfSight(const Entry &player, const Entry &e) {
    const float start[3] = {player.pos[0], player.pos[1], player.pos[2] + 0.5f * player.height};
    const float end[3] = {e.pos[0], e.pos[1], e.pos[2] + 0.5f * e.height};
    float hit[3];
    float t = 1.0f;
    const char blocked = reinterpret_cast<Intersect_t>(Offsets::FUN_CWORLD_INTERSECT)(
        start, end, 0, hit, &t, Offsets::WORLD_TRACE_CLASS_LOS);
    return blocked == 0 || t < 0.0f || t > 1.0f;
}

// 3.80.2's melee range (FUN_1414c71e0): both reaches plus 4/3, at least 5.
bool MeleeRange(void *a, const float *aPos, void *b, const float *bPos) {
    const float leeway = Game::Read<float>(static_cast<uintptr_t>(Offsets::VAR_MELEE_REACH_LEEWAY));
    const float floor = Game::Read<float>(static_cast<uintptr_t>(Offsets::VAR_MELEE_REACH_MIN));
    const float range = std::max(
        Unit::Position::BoundingRadius(a) + Unit::Position::BoundingRadius(b) + leeway, floor);
    const float dx = aPos[0] - bPos[0], dy = aPos[1] - bPos[1], dz = aPos[2] - bPos[2];
    return dx * dx + dy * dy + dz * dz < range * range;
}

// FUN_1405e3490: the player is engaged (a pet in combat, or attacking), and
// the unit is in it — by TargetPriorityCombatLock 1 any of in combat, a pet
// in combat or attacking; otherwise in combat and one of the other two.
bool CombatLocked(const void *player, const void *unit) {
    if ((UnitFlags(player) & Offsets::UNIT_FLAG_PET_IN_COMBAT) == 0 && !Attacking(player))
        return false;
    const uint32_t flags = UnitFlags(unit);
    const bool engaged = (flags & Offsets::UNIT_FLAG_PET_IN_COMBAT) != 0 || Attacking(unit);
    const bool inCombat = (flags & Offsets::UNIT_FLAG_IN_COMBAT) != 0;
    if (CVar::Factory::GetInt(g_combatLock, 1) == 1)
        return engaged || inCombat;
    return engaged && inCombat;
}

bool IsBoss(uint64_t guid) {
    const uint8_t *rec = Creature::Info::CacheRecord(Unit::CreatureID::ForGuid(guid));
    return rec != nullptr && (Game::Read<uint32_t>(rec, Offsets::OFF_CREATURE_TYPE_FLAGS) &
                              Offsets::CREATURE_TYPE_FLAG_BOSS) != 0;
}

// FUN_1405e87c0's friendly test: either side's reaction is friendly (4+).
bool Friendly(const void *player, const void *unit) {
    return Unit::Relation::Reaction(player, unit) >= 4 ||
           Unit::Relation::Reaction(unit, player) >= 4;
}

// Step 1 for one object: the entry, or false to leave it out.
bool Admit(void *player, const float *playerPos, uint64_t playerGuid, const Bank &bank,
           uint64_t guid, Entry &e) {
    void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid);
    if (unit == nullptr || unit == player || guid == playerGuid)
        return false;
    if (Game::Read<void *>(unit, Offsets::OFF_CGOBJECT_MODEL) == nullptr)
        return false;
    const uint8_t *fields = Object::Fields(unit);
    if (fields == nullptr || !Unit::Position::Read(unit, e.pos))
        return false;
    const uint32_t unitFlags = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FLAGS);
    if ((unitFlags & Offsets::UNIT_FLAG_POSSESSED) != 0 &&
        Game::Read<uint64_t>(fields, Offsets::OFF_UNIT_FIELD_CHARMEDBY) == playerGuid)
        return false;

    e.guid = guid;
    e.object = unit;
    e.flags = Unit;
    const bool dead = Game::Read<int32_t>(fields, Offsets::OFF_UNIT_FIELD_HEALTH) <= 0;
    float range;
    if (dead) {
        e.flags |= Dead;
        range = bank.interactRange;
    } else {
        range = bank.rangeFar + Unit::Position::BoundingRadius(player) +
                Unit::Position::BoundingRadius(unit);
    }
    const float dx = e.pos[0] - playerPos[0], dy = e.pos[1] - playerPos[1],
                dz = e.pos[2] - playerPos[2];
    if (dx * dx + dy * dy + dz * dz > range * range)
        return false;

    const bool isPlayer = Object::ByGuid(Offsets::TYPEMASK_PLAYER, guid) != nullptr;
    if (isPlayer)
        e.flags |= Player;
    if (!dead && !isPlayer && IsBoss(guid))
        e.flags |= Boss;
    if (!dead) {
        const bool pvpSelf = (UnitFlags(player) & Offsets::UNIT_FLAG_PVP) != 0;
        const bool pvpUnit = (unitFlags & Offsets::UNIT_FLAG_PVP) != 0;
        if ((pvpSelf || !pvpUnit) && Unit::Relation::CanAttack(player, unit)) {
            e.flags |= Enemy;
            if (CombatLocked(player, unit))
                e.flags |= CombatLock;
        }
        if (Friendly(player, unit) && (unitFlags & Offsets::UNIT_FLAG_NOT_SELECTABLE) == 0 &&
            Unit::Relation::CanAssist(player, unit))
            e.flags |= Friend;
    }
    if (Nearest::NotTabTarget(unit))
        e.flags |= Critter;
    if ((e.flags & (Enemy | Friend)) == 0)
        return false;

    const float scale = Game::Read<float>(unit, Offsets::OFF_CGOBJECT_SCALE);
    e.radius = Game::Read<float>(unit, Offsets::OFF_CGUNIT_SELECTION_RADIUS) * scale;
    e.height = Game::Read<float>(unit, Offsets::OFF_CGOBJECT_BOX_HEIGHT) * scale;
    return true;
}

// Step 2 (FUN_1405e4b30). Returns whether the entry is a combat-locked unit
// in the cone (3.80.2's "contextual relaxation" input).
bool Geometry(const Camera &c, const Entry &player, float facing, const Bank &bank, Entry &e) {
    if (BoxInFrustum(c, e)) {
        e.flags |= InFrustum;
        if (!NeedsLos(e.flags) || InLineOfSight(player, e))
            e.flags |= Visible;
    }
    const float dx = e.pos[0] - player.pos[0], dy = e.pos[1] - player.pos[1],
                dz = e.pos[2] - player.pos[2];
    float d = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float rad = std::max(e.radius, kMinRadius);
    if (d < rad)
        e.flags |= PointBlank;
    const float heading = std::atan2(dy, dx);
    e.aFace = std::fabs(Wrap(heading - facing));
    const float camAngle = Wrap(heading - c.yaw);
    e.aCam = std::fabs(camAngle);
    if ((e.flags & Dead) == 0)
        d -= Unit::Position::BoundingRadius(player.object) +
             Unit::Position::BoundingRadius(e.object);

    bool hitsMe = false;
    if ((e.flags & Enemy) != 0) {
        if (MeleeRange(player.object, player.pos, e.object, e.pos))
            e.flags |= InMelee;
        const uint8_t *fields = Object::Fields(e.object);
        hitsMe = fields != nullptr &&
                 Game::Read<uint64_t>(fields, Offsets::OFF_UNIT_FIELD_TARGET) == player.guid &&
                 MeleeRange(e.object, e.pos, player.object, player.pos);
    }
    if (e.aFace > bank.angOuter && e.aCam > bank.angOuter && !hitsMe &&
        d >= rad && d >= kCloseDistance)
        e.flags |= Excluded;

    const float span = bank.angOuter - bank.angInner;
    const float t1 = Clamp01((e.aFace - bank.angInner) / span);
    const float t2 = Clamp01((e.aCam - bank.angInner) / span);
    const float a = std::min(1 - (1 - t1) * (1 - t1), 1 - (1 - t2) * (1 - t2));
    const float w = (1 - std::pow(1 - a, bank.expB)) * a + std::pow(a, bank.expA) * (1 - a);
    if (d < (1 - w) * bank.rangeFar + w * bank.rangeNear || hitsMe)
        e.flags |= InRange;

    // The cone and the camera's line, from the model's edge nearest that line.
    e.cone = 0;
    const float side = camAngle < 0 ? -1.0f : 1.0f;
    const float edgeX = e.pos[0] - rad * c.left[0] * side - player.pos[0];
    const float edgeY = e.pos[1] - rad * c.left[1] * side - player.pos[1];
    const float edgeAngle = Wrap(std::atan2(edgeY, edgeX) - c.yaw);
    const bool crosses = (edgeAngle < 0) != (camAngle < 0);
    const bool cameraRay = crosses && e.aCam < kPi / 2 && std::fabs(edgeAngle) < kPi / 2;
    if (cameraRay)
        e.flags |= CameraRay;
    const float edgeOffset = crosses ? -std::fabs(edgeAngle) : std::fabs(edgeAngle);
    if (e.aCam <= bank.coneAng) {
        e.cone = 1;
    } else if (edgeOffset < bank.coneAng) {
        const float k = Clamp01((e.aCam - bank.coneAng) / (e.aCam - edgeOffset));
        e.cone = 1 - k;
    }
    if (e.cone > 0)
        e.flags |= InCone;

    const float cosF = std::cos(facing), sinF = std::sin(facing);
    const float along = cosF * dx + sinF * dy;
    const float px = dx - along * cosF, py = dy - along * sinF;
    if (along > 0 && px * px + py * py < rad * rad)
        e.flags |= FacingRay;

    if ((e.flags & InFrustum) != 0 && d < bank.rangeFar &&
        (cameraRay || (e.flags & FacingRay) != 0 || e.cone > 0))
        e.flags = (e.flags & ~Excluded) | InRange;
    return (e.flags & (InCone | CombatLock)) == (InCone | CombatLock);
}

// Step 3 (FUN_1405e5800), the soft score.
void Score(const Camera &c, const Entry &player, const float *targetPos, float combat,
           const Bank &bank, Entry &e) {
    float toCam[3] = {e.pos[0] - c.pos[0], e.pos[1] - c.pos[1], e.pos[2] - c.pos[2]};
    const float camLen = std::sqrt(Dot3(toCam, toCam));
    float s = camLen > 1e-5f ? Dot3(toCam, c.forward) / camLen * bank.fwd : bank.fwd;
    if ((e.flags & Enemy) != 0) {
        if ((e.flags & Boss) != 0)
            s += bank.boss;
        if ((e.flags & CombatLock) != 0)
            s += combat;
    }
    if ((e.flags & Critter) != 0)
        s += bank.critter;
    if ((e.flags & Visible) != 0 && NeedsLos(e.flags) && !InLineOfSight(player, e))
        s += bank.noLos;

    const float rel[3] = {e.pos[0] - player.pos[0], e.pos[1] - player.pos[1],
                          e.pos[2] - player.pos[2]};
    const float d2 = Dot3(rel, rel);
    const float d = std::sqrt(d2);
    s += ((1 - bank.distMix) * d + d2 * bank.distMix) * bank.distW;
    s -= Dot3(rel, c.left) * bank.sideW;
    if (targetPos != nullptr) {
        const float tx = targetPos[0] - e.pos[0], ty = targetPos[1] - e.pos[1],
                    tz = targetPos[2] - e.pos[2];
        const float dt2 = tx * tx + ty * ty + tz * tz;
        if (dt2 > 1e-5f)
            s += std::sqrt(dt2) * bank.fromTargetW;
    }
    if ((e.flags & InMelee) != 0)
        s += bank.melee;
    const float span = bank.angOuter - bank.angInner;
    if (e.aFace < bank.angOuter) {
        const float t = Clamp01((e.aFace - bank.angInner) / span);
        const float k = (1 - t) * (1 - t);
        s += (1 - k) * bank.faceIn + k * bank.faceOut;
    }
    if (e.aCam < bank.angOuter) {
        const float t = Clamp01((e.aCam - bank.angInner) / span);
        const float k = (1 - t) * (1 - t);
        s += (1 - k) * bank.camIn + k * bank.camOut;
    }
    if (d < e.radius || d < kCloseDistance)
        s += bank.close;
    if ((e.flags & CameraRay) != 0)
        s += bank.camRay;
    if ((e.flags & PvpPriority) != 0)
        s += (e.flags & Player) != 0 ? bank.pvpPlayer : bank.pvpNpc;
    s += bank.cone * e.cone;
    e.score = s;
}

void Build() {
    g_entries.clear();
    g_softOrder.clear();
    g_allFlags = 0;
    void *playerObj = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    Camera camera;
    if (playerObj == nullptr || !ReadCamera(camera))
        return;
    Entry player{};
    player.object = playerObj;
    player.guid = Unit::Identity::PlayerGuid();
    if (!Unit::Position::Read(playerObj, player.pos))
        return;
    player.height =
        Game::Read<float>(playerObj, Offsets::OFF_CGOBJECT_BOX_HEIGHT) *
        Game::Read<float>(playerObj, Offsets::OFF_CGOBJECT_SCALE);
    const float facing = Unit::Position::Facing(playerObj);
    const Bank &bank = kBanks[kBank];

    Object::ForEachVisible([&](uint64_t guid) {
        Entry e{};
        if (Admit(playerObj, player.pos, player.guid, bank, guid, e))
            g_entries.push_back(e);
        return true;
    });

    bool lockedInCone = false;
    for (Entry &e : g_entries)
        lockedInCone |= Geometry(camera, player, facing, bank, e);

    float targetPos[3];
    void *target = Object::ByGuid(Offsets::TYPEMASK_OBJECT, Reason::Selection());
    const bool haveTarget = target != nullptr && Unit::Position::Read(target, targetPos);
    const float combat =
        (lockedInCone || CVar::Factory::GetInt(g_relaxation, 1) == 0) ? bank.combat : 0;
    for (Entry &e : g_entries) {
        Score(camera, player, haveTarget ? targetPos : nullptr, combat, bank, e);
        g_allFlags |= e.flags;
        g_softOrder.push_back(&e);
    }
    std::stable_sort(g_softOrder.begin(), g_softOrder.end(),
                     [](const Entry *a, const Entry *b) { return a->score > b->score; });
}

const Entry *Find(uint64_t guid) {
    for (const Entry &e : g_entries)
        if (e.guid == guid)
            return &e;
    return nullptr;
}

void Register() {
    const auto reg = [](const char *name, const char *help, const char *value) {
        return CVar::Factory::Register(name, help, Offsets::CVAR_CATEGORY_GAME, value);
    };
    g_combatLock = reg("TargetPriorityCombatLock",
        "1=Lock to in-combat targets when starting from an in-combat target. 2=Further restrict to in-combat with player.", "1");
    g_relaxation = reg("TargetPriorityCombatLockContextualRelaxation",
        "1=Enables relaxation of combat lock based on context (eg. no in-combat target infront)", "1");
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

bool Update() {
    if (!Object::RegistryReady()) {
        Invalidate();
        return false;
    }
    const uint32_t now = Time::Clock::NowMs();
    if (g_built && Time::Clock::Elapsed(g_lastBuild, now) < kUpdateDelayMs)
        return false;
    g_built = true;
    g_lastBuild = now;
    Build();
    return true;
}

void Invalidate() {
    g_entries.clear();
    g_softOrder.clear();
    g_allFlags = 0;
    g_built = false;
}

uint32_t FlagsOf(uint64_t guid) {
    const Entry *e = Find(guid);
    return e != nullptr ? e->flags : 0;
}

bool Has(uint64_t guid, uint32_t mask) {
    if (guid == 0)
        return false;
    if ((FlagsOf(guid) & mask) == mask)
        return true;
    return (mask & Friend) != 0 && guid == Unit::Identity::PlayerGuid();
}

uint64_t First(uint32_t type, int arc) {
    if ((g_allFlags & type) != type)
        return 0;
    const uint32_t need = type | InFrustum | Visible | InRange;
    const uint32_t any = arc == 0   ? (PointBlank | CameraRay | FacingRay)
                         : arc == 1 ? (PointBlank | CameraRay | FacingRay | InCone)
                                    : InFrustum;
    for (const Entry *e : g_softOrder)
        if ((e->flags & Excluded) == 0 && (e->flags & need) == need && (e->flags & any) != 0)
            return e->guid;
    return 0;
}

} // namespace Target::Priority
