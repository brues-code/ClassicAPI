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

// The soft-interact target — the modern Interact Key (Classic Era 1.15's
// `SoftTargetInteract*` cvars, the `softinteract` / `anyinteract` tokens and
// PLAYER_SOFT_INTERACT_CHANGED; the key itself is the INTERACTTARGET binding
// calling `C_PlayerInteractionManager.InteractUnit("anyinteract")`).
//
// No decomp has it (it post-dates 4.3.4), so the contract comes from
// Blizzard's interface code and the exe's cvar help, and the picking is
// 1.12's own pieces:
//
//   - "Can this be interacted with" is the hover cursor's verdict
//     (Cursor::Hover): the mode the cursor would show over the object. A
//     mode other than none, the base mode, Point, Cast and Attack (and
//     their Unable forms) is an interact mode; the Unable form is "out of
//     reach", as is a named game object cursor the player cannot use now.
//   - "On screen" is the tab-target collector's IsEntityDrawn on the
//     object's world node.
//   - The arc (SoftTargetInteractArc: 0 = directly in front, 1 = in the
//     front yaw arc, 2 = anywhere in the targeting area): 1 is the tab
//     collector's front cone (pi/6 either side, from the engine's own
//     floats); 0 is a strip along the player's forward line, measured in
//     Classic Era 1.15.9 (below).
//   - The range is SoftTargetInteractRange, capped by the tab-target
//     range targetNearestDistance ("limited to tab targeting ... ranges").
//     With SoftTargetInteractRangeIsHard 0 something interactable right now
//     (in reach / usable) still counts past it.
//   - The closest candidate wins. Re-picked every UI frame while enabled;
//     nothing runs while SoftTargetInteract is off. Not on the world tick:
//     there every world node reads as culled (probe: 3 drawn from Lua, 0 on
//     the world tick), while the UI pass — where the engine's own hover
//     classification runs, CGWorldFrame::OnUpdate — sees the drawn state.
//
// SoftTargetInteract's 1 is "gamepad"; 1.12 has no gamepad, so only 2 (KBM)
// and 3 (always) turn it on. Defaults are Classic Era's (0, 0, 10, 0).

#include "target/SoftInteract.h"

#include "Game.h"
#include "Offsets.h"
#include "cursor/Hover.h"
#include "cvar/Factory.h"
#include "event/Custom.h"
#include "guid/Guid.h"
#include "object/Resolve.h"
#include "tick/FrameTick.h"
#include "unit/Identity.h"
#include "unit/Position.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace Target::SoftInteract {

namespace {

using GetFacing_t = float(__fastcall *)(void *object, void *edx);
using Heading_t = float(__fastcall *)(const float *from, const float *to);
using IsDrawn_t = uint32_t(__fastcall *)(void *node, void *edx);
using EnumCallback_t = int(__fastcall *)(void *ctx, void *edx, uint64_t guid);
using EnumVisibleObjects_t = int(__fastcall *)(EnumCallback_t cb, void *ctx);

// Arc 0's strip: how far beside the forward line an object may sit, in
// yards. Classic Era 1.15.9 probe, a mailbox, facing swept until
// `softinteract` dropped: ON span ~33 deg close in and ~15 deg at ~9 yd,
// both a half-width of ~1.2 yd — a strip, not a cone.
constexpr float kArc0HalfWidth = 1.2f;

constexpr int kTypeMask =
    Offsets::TYPEMASK_UNIT | Offsets::TYPEMASK_GAMEOBJECT | Offsets::TYPEMASK_CORPSE;

const Game::Doc::Field kChangedPayload[] = {
    Game::Doc::Req("oldTarget", "WOWGUID", "The previous soft-interact object, or nil."),
    Game::Doc::Req("newTarget", "WOWGUID", "The new soft-interact object, or nil."),
};
const Game::Doc::Event kChangedDoc{
    "Unit", "Fires when the softinteract unit changes.", kChangedPayload};
const Event::Custom::AutoReserve _changed{"PLAYER_SOFT_INTERACT_CHANGED", &kChangedDoc};

CVar::Factory::Handle g_enable = nullptr;
CVar::Factory::Handle g_arc = nullptr;
CVar::Factory::Handle g_range = nullptr;
CVar::Factory::Handle g_rangeIsHard = nullptr;

uint64_t g_soft = 0;

// ── The hover cursor's verdict ───────────────────────────────────────────

enum class Reach { None, Out, Now };

Reach ReachOf(void *object) {
    const uint32_t mode = Cursor::Hover::Mode(object);
    if (mode == 0 || mode == Game::Read<uint32_t>(Offsets::VAR_CURSOR_BASE_MODE))
        return Reach::None;
    if (mode == Offsets::CURSOR_MODE_CUSTOM)
        return Cursor::Hover::IsUsable(object) ? Reach::Now : Reach::Out;
    const bool unable = mode > Offsets::CURSOR_MODE_LAST_NAMED;
    const uint32_t kind = unable ? mode - Offsets::CURSOR_MODE_UNABLE_OFFSET : mode;
    if (kind == 0 || kind > Offsets::CURSOR_MODE_LAST_NAMED || kind == Offsets::CURSOR_MODE_POINT ||
        kind == Offsets::CURSOR_MODE_CAST || kind == Offsets::CURSOR_MODE_ATTACK)
        return Reach::None;
    return unable ? Reach::Out : Reach::Now;
}

// ── Picking ──────────────────────────────────────────────────────────────

bool Enabled() {
    const int v = CVar::Factory::GetInt(g_enable);
    return v == 2 || v == 3;
}

float FloatCVar(CVar::Factory::Handle cvar, float fallback) {
    const char *s = cvar ? CVar::Factory::GetString(cvar) : nullptr;
    return s ? static_cast<float>(std::atof(s)) : fallback;
}

float MaxRange() {
    const float range = FloatCVar(g_range, 10.0f);
    const void *tabRange = Game::Read<void *>(Offsets::VAR_CVAR_TARGET_NEAREST_DISTANCE);
    if (tabRange == nullptr)
        return range;
    const float cap = Game::Read<float>(tabRange, Offsets::OFF_CVAR_VALUE_FLOAT);
    return range < cap ? range : cap;
}

float Facing(void *object) {
    auto *const *vtable = *static_cast<void *const *const *>(object);
    return reinterpret_cast<GetFacing_t>(
        vtable[Offsets::OFF_CGOBJECT_VTBL_GET_FACING / sizeof(void *)])(object, nullptr);
}

bool IsDrawn(const void *object) {
    void *node = Game::Read<void *>(object, Offsets::OFF_CGOBJECT_WORLD_OBJECT);
    return reinterpret_cast<IsDrawn_t>(Offsets::FUN_WORLD_OBJECT_VISIBLE)(node, nullptr) != 0;
}

struct Scan {
    void *player;
    float pos[3];
    float facing;
    int arc;
    float coneHalf;
    float rangeSq;
    bool hard;
    uint64_t best;
    float bestSq;
};

bool InArc(const Scan &s, const float pos[3]) {
    if (s.arc >= 2)
        return true;
    const float pi = Game::Read<float>(Offsets::VAR_TARGETING_PI);
    const float heading =
        reinterpret_cast<Heading_t>(Offsets::FUN_HEADING_BETWEEN_POINTS)(s.pos, pos);
    // The tab collector's form: (heading - facing + 4 pi) mod 2 pi.
    const float angle = std::fmod(heading - s.facing + 4.0f * pi, 2.0f * pi);
    if (s.arc == 1)
        return angle < s.coneHalf || angle > 2.0f * pi - s.coneHalf;
    const float dx = pos[0] - s.pos[0];
    const float dy = pos[1] - s.pos[1];
    const float flat = std::sqrt(dx * dx + dy * dy);
    return std::cos(angle) > 0.0f && std::fabs(flat * std::sin(angle)) <= kArc0HalfWidth;
}

int __fastcall ScanCallback(void *ctx, void *, uint64_t guid) {
    auto &s = *static_cast<Scan *>(ctx);
    void *object = Object::ByGuid(kTypeMask, guid, nullptr, 0);
    if (object == nullptr || object == s.player || !IsDrawn(object))
        return 1;
    float pos[3];
    if (!Unit::Position::Read(object, pos))
        return 1;
    const float dx = pos[0] - s.pos[0];
    const float dy = pos[1] - s.pos[1];
    const float dz = pos[2] - s.pos[2];
    const float distSq = dx * dx + dy * dy + dz * dz;
    if (distSq >= s.bestSq || !InArc(s, pos))
        return 1;
    const Reach reach = ReachOf(object);
    if (reach == Reach::None)
        return 1;
    if (distSq > s.rangeSq && (s.hard || reach != Reach::Now))
        return 1;
    s.best = guid;
    s.bestSq = distSq;
    return 1;
}

uint64_t Pick() {
    auto *player = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    if (player == nullptr)
        return 0;
    Scan s{};
    s.player = player;
    if (!Unit::Position::Read(player, s.pos))
        return 0;
    s.facing = Facing(player);
    s.arc = CVar::Factory::GetInt(g_arc);
    s.coneHalf = Game::Read<float>(Offsets::VAR_TARGETING_PI) *
                 Game::Read<float>(Offsets::VAR_TARGETING_ONE_SIXTH);
    const float range = MaxRange();
    s.rangeSq = range * range;
    s.hard = CVar::Factory::GetInt(g_rangeIsHard) != 0;
    s.bestSq = HUGE_VALF;
    reinterpret_cast<EnumVisibleObjects_t>(Offsets::FUN_CLNT_OBJ_MGR_ENUM_VISIBLE_OBJECTS)(
        &ScanCallback, &s);
    return s.best;
}

void Set(uint64_t guid) {
    if (guid == g_soft)
        return;
    const uint64_t old = g_soft;
    g_soft = guid;
    char oldBuf[Guid::STRING_SIZE];
    char newBuf[Guid::STRING_SIZE];
    Event::Custom::Fire(_changed.Slot(), "%s%s",
                        old ? Guid::FormatAsString(old, oldBuf, sizeof oldBuf) : nullptr,
                        guid ? Guid::FormatAsString(guid, newBuf, sizeof newBuf) : nullptr);
}

void OnFrameTick() {
    if (!Object::RegistryReady()) {
        g_soft = 0;
        return;
    }
    Set(Enabled() ? Pick() : 0);
}

const Tick::FrameTick::AutoSubscribe _tick{&OnFrameTick};

void Register() {
    g_enable = CVar::Factory::Register("SoftTargetInteract", "0");
    g_arc = CVar::Factory::Register("SoftTargetInteractArc", "0");
    g_range = CVar::Factory::Register("SoftTargetInteractRange", "10");
    g_rangeIsHard = CVar::Factory::Register("SoftTargetInteractRangeIsHard", "0");
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

uint64_t Get() { return g_soft; }

// Retail 12.1.5 probe, camera still, a soft-interact game object in front:
// with an interactable NPC targeted `anyinteract` is the NPC, with no
// target it is the game object — the target wins.
uint64_t GetAny() {
    if (!Object::RegistryReady())
        return 0;
    const uint64_t target = Game::Read<uint64_t>(Offsets::VAR_CURRENT_SELECTION_GUID_LO);
    if (target != 0) {
        void *object = Object::ByGuid(kTypeMask, target);
        if (object != nullptr && ReachOf(object) != Reach::None)
            return target;
    }
    return g_soft;
}

} // namespace Target::SoftInteract
