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
//      plus both reaches (dead units: 16 yd) and every game object within
//      16 yd, never the player, a unit the player possesses, or one with no
//      model. Interact: the cursor over it would show an interact mode
//      (1.12's own verdict, Cursor::Hover); OwnObject: the player's channel
//      object (the fishing bobber). A unit is also classified: Enemy
//      (alive and attackable; outside a PvP context a PvP unit never, while
//      the player is not one), Friend (alive, friendly either way, selectable,
//      assistable), Critter (the creature type's never-tab-target flag),
//      Boss, CombatLock (an enemy in combat while the player is engaged),
//      for a living player Party (and Raid: a party member) or Raid (a raid
//      member), and in a PvP context (a duel, free-for-all PvP, a
//      battleground) PvpPriority / CombatLock by TargetPriorityPvp.
//   2. Geometry: InFrustum (the model's box against the view frustum pulled
//      in by 0.2 on every side), Visible (and, for an enemy that is not a
//      player, in line of sight of the player or the camera; seen by the
//      camera alone costs the NoLOS term), PointBlank, InMelee, the angles to the
//      character's facing and to the camera's yaw, InRange (the bank's range
//      shrinking from 51 yd within 20° to 3 yd past 90°), Excluded (more than
//      90° behind both, not hitting the player in melee), InCone (the
//      camera's 18° cone, widened by the model's radius, with a weight),
//      CameraRay / FacingRay (the camera's or the facing's line passes
//      through the model).
//   3. Score: the bank's weighted terms — camera alignment, distance (mixed
//      linear and squared), a sideways term along the camera's second row,
//      distance from the target, melee, the facing and camera ease terms, the
//      cone, point blank, PvP, combat lock, boss, critter and own object —
//      then the soft order, best first. The tab score adds the casting term
//      (an enemy casting or channeling), and the tab order sorts by it.
//
// The tables are 3.80.2's three banks; soft targeting uses bank 1 (its
// TargetPriorityValueBank default; bank 2 is hold-to-target, not here). Where 1.12 has
// the input, it is 1.12's own: reach is UNIT_FIELD_COMBATREACH (what its
// melee and interact ranges add), the model's box is the bounds the engine
// stores when the model loads (+0xC0) times the scale, the frustum is the world
// frame's own view-projection, and line of sight is CWorld::Intersect with
// the LoS geometry, between the two units at half their model height.
//
// Not in 1.12, so not here: the "trivial" and the 0x1000 template bits, the
// spec's melee weight (MeleeA is used), unit flags3, the exclusion's
// shared-vehicle test (FUN_1415c1820 walks unit-to-unit vehicle seats; 1.12
// has no vehicles, so it is always false), arenas and the "in PvP combat" flag of the PvP context, the
// tab-only group term (it needs a specialization flag), and the casting
// term's uninterruptible test.

#include "target/Priority.h"

#include "Game.h"
#include "Offsets.h"
#include "creature/Info.h"
#include "cursor/Hover.h"
#include "cvar/Factory.h"
#include "group/Roster.h"
#include "map/Instance.h"
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
    float cone, camRay, close, pvpPlayer, pvpNpc, combat, boss, critter, noLos, ownObject;
    float casting;
};
constexpr Bank kBanks[3] = {
    {61, 4, 30 * kDeg, 120 * kDeg, 1, 1, 30 * kDeg, 16, 10, 0.3f, -10, -6, 0, 0, 0, 0, 200,
     1000, 0, 0, 0, 30000, 10000, 0, 0, 0, 0, 0, 0},
    {51, 3, 20 * kDeg, 90 * kDeg, 1, 2, 18 * kDeg, 16, 200, 0.5f, -10, -10, -50, 200, 100, 400,
     300, 500, 2000, 0, 1500, 30000, 10000, 40000, 1000, -20000, -4000, 20000, 4000},
    {100, 1, 5 * kDeg, 6 * kDeg, 0.4f, 10, 5 * kDeg, 16, 200, 0.25f, -1, 0, 0, 200, 0, 0, 0, 0,
     1600, 3000, 0, 100, 50, 100, 0, -20000, 0, 100, 500},
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
    float base;
    float height;
    float aFace;
    float aCam;
    float cone;
    float score;
    float tabScore;
    bool playerLos; // Visible came from the player's ray, not only the camera's
};

std::vector<Entry> g_entries;
std::vector<const Entry *> g_softOrder;
std::vector<const Entry *> g_tabOrder;
uint32_t g_allFlags = 0;
uint32_t g_lastBuild = 0;
bool g_built = false;
bool g_lockedInCone = false;

CVar::Factory::Handle g_combatLock = nullptr;
CVar::Factory::Handle g_relaxation = nullptr;
CVar::Factory::Handle g_pvp = nullptr;

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
                                 e.pos[2] + e.base + ((i & 4) ? e.height : 0.0f) - c.pos[2],
                                 0.0f};
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

// A clear ray from a point to the entry's mid-height. 3.80.2's LOS test
// (FUN_1405f48b0 -> FUN_1405f4fe0) keeps two results per GUID, from the player
// and from the camera (camera +0x10), and its last argument's bit 0 adds the
// camera's: the Visible bit asks for either (arg 1), the NoLOS score term asks
// for the player's alone (arg 0).
bool ClearRay(const float *start, const Entry &e) {
    const float end[3] = {e.pos[0], e.pos[1], e.pos[2] + e.base + 0.5f * e.height};
    float hit[3];
    float t = 1.0f;
    const char blocked = reinterpret_cast<Intersect_t>(Offsets::FUN_CWORLD_INTERSECT)(
        start, end, 0, hit, &t, Offsets::WORLD_TRACE_CLASS_LOS);
    return blocked == 0 || t < 0.0f || t > 1.0f;
}

bool PlayerLos(const Entry &player, const Entry &e) {
    const float start[3] = {player.pos[0], player.pos[1],
                            player.pos[2] + player.base + 0.5f * player.height};
    return ClearRay(start, e);
}

// 3.80.2's melee range (FUN_1414c71e0): both reaches plus 4/3, at least 5.
bool MeleeRange(void *a, const float *aPos, void *b, const float *bPos) {
    const float leeway = Game::Read<float>(static_cast<uintptr_t>(Offsets::VAR_MELEE_REACH_LEEWAY));
    const float floor = Game::Read<float>(static_cast<uintptr_t>(Offsets::VAR_MELEE_REACH_MIN));
    const float range = std::max(
        Unit::Position::CombatReach(a) + Unit::Position::CombatReach(b) + leeway, floor);
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

// --- PvP (FUN_1405e35d0 / 3530 / 6cf0 / 14ab560) on 1.12's fields --------

using ControllingPlayer_t = void *(__fastcall *)(void *unit);

bool IsPlayerObject(const void *unit) {
    return Game::Read<int>(unit, Offsets::OFF_CGOBJECT_TYPE_ID) == Offsets::OBJECT_TYPE_PLAYER;
}

// A player's block (PLAYER_FLAGS, the duel fields); only a player has one.
const uint8_t *PlayerInfo(const void *player) {
    return IsPlayerObject(player) ? Game::Read<const uint8_t *>(player, Offsets::OFF_CGPLAYER_INFO)
                                  : nullptr;
}

uint32_t PlayerFlags(const void *player) {
    const uint8_t *info = PlayerInfo(player);
    return info ? Game::Read<uint32_t>(info, Offsets::OFF_PLAYER_INFO_FLAGS) : 0;
}

int DuelTeam(const void *player) {
    const uint8_t *info = PlayerInfo(player);
    return info ? Game::Read<int>(info, Offsets::OFF_PLAYER_INFO_DUEL_TEAM) : 0;
}

uint64_t DuelArbiter(const void *player) {
    const uint8_t *info = PlayerInfo(player);
    return info ? Game::Read<uint64_t>(info, Offsets::OFF_PLAYER_INFO_DUEL_ARBITER) : 0;
}

// FUN_1414ab560: the two player-controlled units' players duel each other
// (one arbiter, two teams).
bool Dueling(void *a, void *b) {
    if ((UnitFlags(a) & Offsets::UNIT_FLAG_PLAYER_CONTROLLED) == 0 ||
        (UnitFlags(b) & Offsets::UNIT_FLAG_PLAYER_CONTROLLED) == 0)
        return false;
    const auto controller = reinterpret_cast<ControllingPlayer_t>(Offsets::FUN_UNIT_CONTROLLING_PLAYER);
    void *pa = controller(a);
    void *pb = controller(b);
    if (pa == nullptr || pb == nullptr)
        return false;
    const int ta = DuelTeam(pa), tb = DuelTeam(pb);
    return ta != 0 && tb != 0 && ta != tb && DuelArbiter(pa) == DuelArbiter(pb);
}

// FUN_1405e35d0: the player is in a PvP context — dueling, free-for-all
// PvP, or on a battleground map. (3.80.2 also counts an arena and its "in
// PvP combat" flag; 1.12 has neither.)
bool PvpContext(const void *player) {
    return DuelTeam(player) != 0 || (PlayerFlags(player) & Offsets::PLAYER_FLAG_FFA_PVP) != 0 ||
           Map::Instance::CurrentType() == Offsets::MAP_INSTANCE_TYPE_BATTLEGROUND;
}

// FUN_1405e3530: the unit is a PvP unit. A creature: player-controlled and
// PvP flagged. A player: PvP flagged, free-for-all, wanting PvP
// (PLAYER_FLAGS_PVP_DESIRED), or in a duel with the player.
bool PvpUnit(void *player, void *unit) {
    const uint32_t flags = UnitFlags(unit);
    if (!IsPlayerObject(unit))
        return (flags & Offsets::UNIT_FLAG_PLAYER_CONTROLLED) != 0 &&
               (flags & Offsets::UNIT_FLAG_PVP) != 0;
    if ((flags & Offsets::UNIT_FLAG_PVP) != 0 ||
        (PlayerFlags(unit) & (Offsets::PLAYER_FLAG_FFA_PVP | Offsets::PLAYER_FLAG_PVP_DESIRED)) != 0)
        return true;
    return unit != player && DuelTeam(unit) != 0 && Dueling(player, unit);
}

// The rebuild's PvP inputs: the context, the player as a PvP unit (or in
// the context), and TargetPriorityPvp.
struct Pvp {
    bool context;
    bool self;
    int mode;
};

// FUN_1405e6de0's enemy test: not dead or feigning (vfunc 0x230), not shown
// as dead (UNIT_FLAG3_FAKE_DEAD), stand state not 7, attackable. 1.12's tab
// predicate in its enemy mode is the same test on 1.12's data — a dead check
// (0x00605F30), health > 0, CanAttack, not UNIT_DYNFLAG_DEAD (how a vanilla
// server shows a unit as dead), stand state not 7 — so a unit that lies dead
// is never an enemy here, as 1.12's own Tab never picks it.
bool EnemyOf(void *player, void *unit) {
    using Valid_t = int(__fastcall *)(void *player, void *candidate, int mode);
    return reinterpret_cast<Valid_t>(Offsets::FUN_TARGET_CANDIDATE_VALID)(
               player, unit, Offsets::TARGET_MODE_ENEMY) != 0;
}

// FUN_1405e87c0's friendly test: either side's reaction is friendly (4+).
bool Friendly(const void *player, const void *unit) {
    return Unit::Relation::Reaction(player, unit) >= 4 ||
           Unit::Relation::Reaction(unit, player) >= 4;
}

// The model's box from its bounds (+0xC0, model space) and the object's
// scale: the half width about the position, and the bottom and height.
void ReadBox(void *object, Entry &e) {
    const float scale = Game::Read<float>(object, Offsets::OFF_CGOBJECT_SCALE);
    const float *b = Game::Ptr<float>(object, Offsets::OFF_CGOBJECT_BOUNDS);
    e.radius = 0.5f * std::max(b[3] - b[0], b[4] - b[1]) * scale;
    e.base = b[2] * scale;
    e.height = (b[5] - b[2]) * scale;
}

// 3.80.2's interact test (FUN_1405e6f70), on 1.12's own judgement: the
// cursor over the object shows an interact mode. OwnObject is the player's
// channel object (the fishing bobber).
void ClassifyInteract(const void *player, uint64_t guid, void *object, Entry &e) {
    if (Cursor::Hover::InteractReach(object) == Cursor::Hover::Reach::None)
        return;
    e.flags |= Interact;
    const uint8_t *fields = Object::Fields(player);
    if ((e.flags & GameObject) != 0 && fields != nullptr &&
        Game::Read<uint64_t>(fields, Offsets::OFF_UNIT_FIELD_CHANNEL_OBJECT) == guid)
        e.flags |= OwnObject;
}

bool WithinRange(const float *a, const float *b, float range) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz <= range * range;
}

// Step 1 for one object (the gather callback 0x1405e66d0 and the
// classifiers): the entry, or false to leave it out.
bool Admit(void *player, const float *playerPos, uint64_t playerGuid, const Bank &bank,
           const Pvp &pvp, uint64_t guid, Entry &e) {
    if (guid == playerGuid)
        return false;
    void *object = Object::ByGuid(Offsets::TYPEMASK_UNIT | Offsets::TYPEMASK_GAMEOBJECT, guid);
    if (object == nullptr || object == player ||
        Game::Read<void *>(object, Offsets::OFF_CGOBJECT_MODEL) == nullptr ||
        !Unit::Position::Read(object, e.pos))
        return false;
    e.guid = guid;
    e.object = object;

    void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid);
    if (unit == nullptr) {
        e.flags = GameObject;
        if (!WithinRange(e.pos, playerPos, bank.interactRange))
            return false;
        ClassifyInteract(player, guid, object, e);
        if ((e.flags & Interact) == 0)
            return false;
        ReadBox(object, e);
        return true;
    }

    const uint8_t *fields = Object::Fields(unit);
    if (fields == nullptr)
        return false;
    const uint32_t unitFlags = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FLAGS);
    if ((unitFlags & Offsets::UNIT_FLAG_POSSESSED) != 0 &&
        Game::Read<uint64_t>(fields, Offsets::OFF_UNIT_FIELD_CHARMEDBY) == playerGuid)
        return false;

    e.flags = Unit;
    const bool dead = Game::Read<int32_t>(fields, Offsets::OFF_UNIT_FIELD_HEALTH) <= 0;
    float range;
    if (dead) {
        e.flags |= Dead;
        range = bank.interactRange;
    } else {
        range = bank.rangeFar + Unit::Position::CombatReach(player) +
                Unit::Position::CombatReach(unit);
    }
    if (!WithinRange(e.pos, playerPos, range))
        return false;

    const bool isPlayer = Object::ByGuid(Offsets::TYPEMASK_PLAYER, guid) != nullptr;
    if (isPlayer)
        e.flags |= Player;
    if (isPlayer && !dead) {
        if (Group::Roster::IsPartyMember(guid))
            e.flags |= Party | Raid;
        else if (Group::Roster::IsRaidMember(guid))
            e.flags |= Raid;
    }
    if (!dead && !isPlayer && IsBoss(guid))
        e.flags |= Boss;

    // FUN_1405e6cf0. Outside a PvP context, a PvP unit is never an enemy
    // while the player is not one. Inside one, by TargetPriorityPvp: 2 marks
    // every PvP unit; 1 and 2 lock bosses; 1 to 3 lock PvP players.
    const bool pvpUnit = PvpUnit(player, unit);
    if (pvp.context) {
        if (pvp.mode == 2 && (pvpUnit || (unitFlags & Offsets::UNIT_FLAG_PVP) != 0))
            e.flags |= PvpPriority;
        if ((pvp.mode == 1 || pvp.mode == 2) && (e.flags & Boss) != 0)
            e.flags |= PvpPriority | CombatLock;
        if (pvp.mode >= 1 && pvp.mode <= 3 && isPlayer && pvpUnit)
            e.flags |= PvpPriority | CombatLock;
    }
    if (!dead) {
        if ((pvp.self || !pvpUnit) && EnemyOf(player, unit)) {
            e.flags |= Enemy;
            if ((e.flags & CombatLock) == 0 && CombatLocked(player, unit))
                e.flags |= CombatLock;
        }
        if (Friendly(player, unit) && (unitFlags & Offsets::UNIT_FLAG_NOT_SELECTABLE) == 0 &&
            Unit::Relation::CanAssist(player, unit))
            e.flags |= Friend;
    }
    if (Nearest::NotTabTarget(unit))
        e.flags |= Critter;
    ClassifyInteract(player, guid, unit, e);
    if ((e.flags & (Enemy | Friend | Interact)) == 0)
        return false;
    ReadBox(unit, e);
    return true;
}

// Step 2 (FUN_1405e4b30). Returns whether the entry is a combat-locked unit
// in the cone (3.80.2's "contextual relaxation" input).
bool Geometry(const Camera &c, const Entry &player, float facing, const Bank &bank, Entry &e) {
    if (BoxInFrustum(c, e)) {
        e.flags |= InFrustum;
        if (!NeedsLos(e.flags)) {
            e.flags |= Visible;
        } else {
            e.playerLos = PlayerLos(player, e);
            if (e.playerLos || ClearRay(c.pos, e))
                e.flags |= Visible;
        }
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
    if ((e.flags & (Unit | Dead)) == Unit)
        d -= Unit::Position::CombatReach(player.object) +
             Unit::Position::CombatReach(e.object);

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

// The tab score's casting test (3.80.2: unit +0x794 / +0x7cc): a cast in
// progress, or a channel.
bool Casting(void *unit) {
    const uint8_t *fields = Object::Fields(unit);
    return Game::Read<uint32_t>(unit, Offsets::OFF_UNIT_CAST_SPELL) != 0 ||
           (fields != nullptr &&
            Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_CHANNEL_SPELL) != 0);
}

// Step 3 (FUN_1405e5800), the soft and the tab score.
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
    if ((e.flags & Visible) != 0 && NeedsLos(e.flags) && !e.playerLos)
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
    if ((e.flags & OwnObject) != 0)
        s += bank.ownObject;
    s += bank.cone * e.cone;
    e.score = s;
    e.tabScore = s;
    if ((e.flags & (Unit | Enemy)) == (Unit | Enemy) && Casting(e.object))
        e.tabScore += bank.casting;
}

void Build() {
    g_entries.clear();
    g_softOrder.clear();
    g_tabOrder.clear();
    g_allFlags = 0;
    g_lockedInCone = false;
    void *playerObj = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    Camera camera;
    if (playerObj == nullptr || !ReadCamera(camera))
        return;
    Entry player{};
    player.object = playerObj;
    player.guid = Unit::Identity::PlayerGuid();
    if (!Unit::Position::Read(playerObj, player.pos))
        return;
    ReadBox(playerObj, player);
    const float facing = Unit::Position::Facing(playerObj);
    const Bank &bank = kBanks[kBank];
    Pvp pvp;
    pvp.context = PvpContext(playerObj);
    pvp.self = pvp.context || PvpUnit(playerObj, playerObj);
    pvp.mode = CVar::Factory::GetInt(g_pvp, 1);

    Object::ForEachVisible([&](uint64_t guid) {
        Entry e{};
        if (Admit(playerObj, player.pos, player.guid, bank, pvp, guid, e))
            g_entries.push_back(e);
        return true;
    });

    bool lockedInCone = false;
    for (Entry &e : g_entries)
        lockedInCone |= Geometry(camera, player, facing, bank, e);
    g_lockedInCone = lockedInCone;

    float targetPos[3];
    void *target = Object::ByGuid(Offsets::TYPEMASK_OBJECT, Reason::Selection());
    const bool haveTarget = target != nullptr && Unit::Position::Read(target, targetPos);
    const float combat =
        (lockedInCone || CVar::Factory::GetInt(g_relaxation, 1) == 0) ? bank.combat : 0;
    for (Entry &e : g_entries) {
        Score(camera, player, haveTarget ? targetPos : nullptr, combat, bank, e);
        g_allFlags |= e.flags;
        g_softOrder.push_back(&e);
        g_tabOrder.push_back(&e);
    }
    std::stable_sort(g_softOrder.begin(), g_softOrder.end(),
                     [](const Entry *a, const Entry *b) { return a->score > b->score; });
    std::stable_sort(g_tabOrder.begin(), g_tabOrder.end(),
                     [](const Entry *a, const Entry *b) { return a->tabScore > b->tabScore; });
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
    g_pvp = reg("TargetPriorityPvp",
        "When in pvp, give higher priority to players and important pvp targets (1 = players & npc bosses, 2 = all pvp targets, 3 = players only)", "1");
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

void Refresh() {
    if (!Object::RegistryReady()) {
        Invalidate();
        return;
    }
    const uint32_t now = Time::Clock::NowMs();
    if (g_built && g_lastBuild == now)
        return;
    g_built = true;
    g_lastBuild = now;
    Build();
}

void Invalidate() {
    g_entries.clear();
    g_softOrder.clear();
    g_tabOrder.clear();
    g_allFlags = 0;
    g_lockedInCone = false;
    g_built = false;
}

size_t TabSize() { return g_tabOrder.size(); }

uint64_t TabGuid(size_t rank) { return g_tabOrder[rank]->guid; }

uint32_t TabFlags(size_t rank) { return g_tabOrder[rank]->flags; }

int TabRank(uint64_t guid) {
    if (guid == 0)
        return -1;
    for (size_t i = 0; i < g_tabOrder.size(); ++i)
        if (g_tabOrder[i]->guid == guid)
            return static_cast<int>(i);
    return -1;
}

bool LockedInCone() { return g_lockedInCone; }

int CombatLockMode() { return CVar::Factory::GetInt(g_combatLock, 1); }

bool CombatLockedWith(uint64_t guid) {
    const void *player = Unit::Identity::PlayerObject();
    const void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid);
    return player != nullptr && unit != nullptr && CombatLocked(player, unit);
}

bool CombatLockRelaxation() { return CVar::Factory::GetInt(g_relaxation, 1) != 0; }

bool PvpLockedWith(uint64_t guid) {
    void *player = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid);
    return player != nullptr && unit != nullptr && PvpContext(player) &&
           CVar::Factory::GetInt(g_pvp, 1) == 2 &&
           (PvpUnit(player, unit) || (UnitFlags(unit) & Offsets::UNIT_FLAG_PVP) != 0);
}

bool CameraYaw(float &yaw) {
    Camera camera;
    if (!ReadCamera(camera))
        return false;
    yaw = camera.yaw;
    return true;
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
    // An interact candidate needs no InRange: its 16 yd gather is its range.
    const uint32_t need = type | InFrustum | Visible | ((type & Interact) != 0 ? 0 : InRange);
    const uint32_t any = arc == 0   ? (PointBlank | CameraRay | FacingRay)
                         : arc == 1 ? (PointBlank | CameraRay | FacingRay | InCone)
                                    : InFrustum;
    for (const Entry *e : g_softOrder)
        if ((e->flags & Excluded) == 0 && (e->flags & need) == need && (e->flags & any) != 0)
            return e->guid;
    return 0;
}

} // namespace Target::Priority
