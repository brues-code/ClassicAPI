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

// The soft-target scan, from 1.12's own pieces:
//
//   - "On screen" is the scene's own election: an entity the last drain
//     drew in pass 1 (below).
//   - The arc (SoftTarget<Kind>Arc: 0 = directly in front, 1 = in the front
//     yaw arc, 2 = anywhere in the targeting area): 1 is the tab collector's
//     front cone (pi/6 either side, from the engine's own floats); 0 is a
//     strip along the forward line, measured in Classic Era 1.15.9 (below).
//     "Front" is the camera's, not the character's: in Classic Era 1.15 a
//     character turned away still soft targets what the camera faces. The
//     arc is centred on the player, turned to the camera's yaw (CGCamera::
//     Forward, the vector 4.3.4's GetCameraFacing reads).
//   - The range is SoftTarget<Kind>Range, capped by the tab-target range
//     targetNearestDistance ("limited to tab targeting ... ranges").
//   - The closest candidate wins — Classic Era 1.15 probes for interact,
//     enemy and friend alike, not the one nearest the forward line.
//
// The candidates are the scene's entities in sight, not the whole object
// manager. DrawEntityModels (0x683DD0) calls each entity's render callback
// WorldObjectRenderCallback (0x481540) once a frame — pass 1 for those in
// sight, pass 2 for the rest of the scene's bands, whose models it hides —
// so hooking the callback collects the in-sight GUIDs as the scene draws
// them, and hooking the drain publishes them when it returns: a scan reads a
// finished drain's list, never one still filling. Pass 1 is the drawn set
// itself: the cull walk clears bit 0 of an entity's cull state `+0x94` — what
// IsEntityDrawn (0x6704C0) reads — exactly as it moves the entity to the draw
// list the drain calls with pass 1 (benilla-re terrain/scene.md "Entity
// cull", "Object election"). The list is that verdict kept, so it holds until
// the next drain; asking IsEntityDrawn instead is right only between the
// drain and the next paint, which resets every node to culled after the
// world tick. A read in that window — a Lua UnitBuff naming its caster's
// token before the scene draws — found nothing drawn, and the pick it cached
// hid the soft-target icons for that frame. Stormwind probe, 34 s:
// ~200 pass-1 and ~360 pass-2 entities a frame, no pass-2 entity ever
// IsEntityDrawn, and no IsEntityDrawn object missing from the list (checked
// by a full ClntObjMgrEnumObjects walk beside it). That walk is what this
// replaces: it visits every live object, bag items included — ~870 a scan.
//
// The drain also hands over each entity's object: WorldObjectRenderCallback
// resolves the GUID and passes the object to AddPickCandidate (0x4815D0, its
// only caller), so hooking that saves the pointer beside the GUID. The
// pointer is used only while it is certain to be live: objects are released
// and freed only by the destroy message, by pool reuse when a new object is
// created, and by the manager's teardown (benilla-re objectlayer/manager.md)
// — the first two only while an incoming packet is handled, the last with a
// new manager. So the pointers hold from the drain until the next packet (a
// Net::PacketDispatch subscriber, which runs before the engine's handler)
// and only under the manager that drew them. The frame tick's picks run in
// the drain's own render, inside that window; a read after a packet (a Lua
// event, an OnUpdate) looks each GUID up as before.

#include "target/SoftTarget.h"

#include "Game.h"
#include "Offsets.h"
#include "net/PacketDispatch.h"
#include "object/Resolve.h"
#include "unit/Identity.h"
#include "unit/Position.h"

#include <cmath>
#include <vector>

namespace Target::SoftTarget {

namespace {

using Forward_t = void(__thiscall *)(void *camera, float *out);
using Heading_t = float(__fastcall *)(const float *from, const float *to);
// `ctx` (the node's callback argument) is unused; the object comes from the
// GUID. RET 0xC.
using EntityRender_t = int(__fastcall *)(void *ctx, int pass, uint32_t guidLo, uint32_t guidHi,
                                         int unused);
using DrawEntityModels_t = void(__cdecl *)();
using AddPickCandidate_t = void(__thiscall *)(void *worldFrame, void *object, int pass);

// An entity in sight: its GUID, and its object when the drain resolved one
// (a released object or one with no model ready is not handed over).
struct Entity {
    uint64_t guid;
    void *object;
};

// `g_drawing` fills while a drain runs; `g_entities` is the last finished
// drain's, which scans read. A GUID gone since resolves to nothing and is
// skipped.
std::vector<Entity> g_drawing;
std::vector<Entity> g_entities;
bool g_inDrain = false;
uint32_t g_drains = 0; // finished drains, each a new g_entities
bool g_inEntity = false; // inside a pass-1 callback, whose entity is g_drawing.back()
// The window the objects in `g_entities` are live in: closed by the next
// incoming packet, and only under the manager the drain ran with.
bool g_objectsLive = false;
const void *g_objectsManager = nullptr;
EntityRender_t g_origEntityRender = nullptr;
DrawEntityModels_t g_origDrawEntityModels = nullptr;
AddPickCandidate_t g_origAddPickCandidate = nullptr;

int __fastcall EntityRender_h(void *ctx, int pass, uint32_t guidLo, uint32_t guidHi, int unused) {
    if (!g_inDrain || pass != 1)
        return g_origEntityRender(ctx, pass, guidLo, guidHi, unused);
    g_drawing.push_back({static_cast<uint64_t>(guidHi) << 32 | guidLo, nullptr});
    g_inEntity = true;
    const int result = g_origEntityRender(ctx, pass, guidLo, guidHi, unused);
    g_inEntity = false;
    return result;
}

void __fastcall AddPickCandidate_h(void *worldFrame, void *, void *object, int pass) {
    if (g_inEntity)
        g_drawing.back().object = object;
    g_origAddPickCandidate(worldFrame, object, pass);
}

void __cdecl DrawEntityModels_h() {
    g_drawing.clear();
    g_inDrain = true;
    g_origDrawEntityModels();
    g_inDrain = false;
    g_entities.swap(g_drawing);
    ++g_drains;
    g_objectsLive = true;
    g_objectsManager = Game::Read<void *>(Offsets::VAR_LOCAL_PLAYER_PTR);
}

void OnPacket(uint32_t, Net::CDataStore *) { g_objectsLive = false; }

const Game::HookAutoRegister _entityRenderHook{
    Offsets::FUN_WORLD_OBJECT_RENDER_CALLBACK, reinterpret_cast<void *>(&EntityRender_h),
    reinterpret_cast<void **>(&g_origEntityRender)};
const Game::HookAutoRegister _addPickCandidateHook{
    Offsets::FUN_ADD_PICK_CANDIDATE, reinterpret_cast<void *>(&AddPickCandidate_h),
    reinterpret_cast<void **>(&g_origAddPickCandidate)};
const Game::HookAutoRegister _drawEntityModelsHook{
    Offsets::FUN_DRAW_ENTITY_MODELS, reinterpret_cast<void *>(&DrawEntityModels_h),
    reinterpret_cast<void **>(&g_origDrawEntityModels)};
const Net::PacketDispatch::AutoSubscribe _packetSub{&OnPacket};

// Arc 0's strip: how far beside the forward line an object may sit, in
// yards. Classic Era 1.15.9 probe, a mailbox, view swept until
// `softinteract` dropped: ON span ~33 deg close in and ~15 deg at ~9 yd,
// both a half-width of ~1.2 yd — a strip, not a cone.
constexpr float kArc0HalfWidth = 1.2f;

// The camera's yaw, in the heading convention FUN_HEADING_BETWEEN_POINTS
// uses (atan2 of the y and x deltas). False before the world frame has a
// camera.
bool CameraYaw(float &yaw) {
    void *worldFrame = Game::Read<void *>(Offsets::VAR_GAME_STATE_PTR);
    void *camera =
        worldFrame ? Game::Read<void *>(worldFrame, Offsets::OFF_GAME_STATE_CAMERA_PTR) : nullptr;
    if (camera == nullptr)
        return false;
    float forward[3];
    auto *vtable = Game::Read<uintptr_t *>(camera, 0);
    reinterpret_cast<Forward_t>(vtable[Offsets::VTBL_CAMERA_FORWARD / sizeof(uintptr_t)])(camera,
                                                                                         forward);
    if (forward[0] == 0.0f && forward[1] == 0.0f)
        return false;
    yaw = std::atan2(forward[1], forward[0]);
    return true;
}

bool InArc(const Area &a, const float pos[3]) {
    if (a.arc >= 2)
        return true;
    const float pi = Game::Read<float>(Offsets::VAR_TARGETING_PI);
    const float heading =
        reinterpret_cast<Heading_t>(Offsets::FUN_HEADING_BETWEEN_POINTS)(a.pos, pos);
    // The tab collector's form, with the camera's yaw: (heading - yaw + 4 pi) mod 2 pi.
    const float angle = std::fmod(heading - a.yaw + 4.0f * pi, 2.0f * pi);
    if (a.arc == 1)
        return angle < a.coneHalf || angle > 2.0f * pi - a.coneHalf;
    const float dx = pos[0] - a.pos[0];
    const float dy = pos[1] - a.pos[1];
    const float flat = std::sqrt(dx * dx + dy * dy);
    return std::cos(angle) > 0.0f && std::fabs(flat * std::sin(angle)) <= kArc0HalfWidth;
}

// Where: the checks before the arc, which needs the position.
bool Locate(const Area &a, void *object, float pos[3], float &distSq) {
    if (object == nullptr || object == a.player || !Unit::Position::Read(object, pos))
        return false;
    const float dx = pos[0] - a.pos[0];
    const float dy = pos[1] - a.pos[1];
    const float dz = pos[2] - a.pos[2];
    distSq = dx * dx + dy * dy + dz * dz;
    return true;
}

struct Scan {
    const Area *area;
    int typeMask;
    Accept accept;
    void *ctx;
    uint64_t best;
    float bestSq;
};

// The entity's object, filtered by `typeMask`: the drain's own pointer while
// it is live, with the typed lookup's test (the type word, field 2, shares a
// bit with the mask); otherwise the lookup itself.
void *Resolve(const Entity &e, int typeMask, bool live) {
    if (!live || e.object == nullptr)
        return Object::ByGuid(typeMask, e.guid, nullptr, 0);
    const auto *fields = Game::Read<const uint8_t *>(e.object, Offsets::OFF_UNIT_GUID_PTR);
    return fields != nullptr &&
                   (Game::Read<uint32_t>(fields, Offsets::OFF_OBJECT_FIELD_TYPE) & typeMask) != 0
               ? e.object
               : nullptr;
}

void Consider(Scan &s, const Entity &e, bool live) {
    const uint64_t guid = e.guid;
    void *object = Resolve(e, s.typeMask, live);
    float pos[3];
    float distSq;
    if (!Locate(*s.area, object, pos, distSq) || distSq >= s.bestSq || !InArc(*s.area, pos) ||
        !s.accept(*s.area, object, distSq, s.ctx))
        return;
    s.best = guid;
    s.bestSq = distSq;
}

} // namespace

bool Enabled(CVar::Factory::Handle enable) {
    const int v = CVar::Factory::GetInt(enable);
    return v == 2 || v == 3;
}

bool Begin(Area &area, CVar::Factory::Handle arc, CVar::Factory::Handle range,
           float defaultRange) {
    area = Area{};
    area.player = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    if (area.player == nullptr || !Unit::Position::Read(area.player, area.pos) ||
        !CameraYaw(area.yaw))
        return false;
    area.arc = CVar::Factory::GetInt(arc);
    area.coneHalf = Game::Read<float>(Offsets::VAR_TARGETING_PI) *
                    Game::Read<float>(Offsets::VAR_TARGETING_ONE_SIXTH);
    float r = CVar::Factory::GetFloat(range, defaultRange);
    if (CVar::Factory::Handle tabRange =
            Game::Read<void *>(Offsets::VAR_CVAR_TARGET_NEAREST_DISTANCE)) {
        const float cap = CVar::Factory::GetFloat(tabRange);
        if (cap < r)
            r = cap;
    }
    area.rangeSq = r * r;
    return true;
}

uint32_t Drains() { return g_drains; }

bool InView(const Area &area, uint64_t guid, void *object, float &distSq) {
    float pos[3];
    for (const Entity &e : g_entities)
        if (e.guid == guid)
            return Locate(area, object, pos, distSq) && InArc(area, pos);
    return false;
}

uint64_t Nearest(const Area &area, int typeMask, Accept accept, void *ctx) {
    Scan s{&area, typeMask, accept, ctx, 0, HUGE_VALF};
    const bool live =
        g_objectsLive && g_objectsManager == Game::Read<void *>(Offsets::VAR_LOCAL_PLAYER_PTR);
    for (const Entity &e : g_entities)
        Consider(s, e, live);
    return s.best;
}

} // namespace Target::SoftTarget
