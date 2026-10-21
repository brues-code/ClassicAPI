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
// Blizzard's interface code and the exe's cvar help. The scan (on screen,
// arc, range, nearest) is Target::SoftTarget's; this kind adds:
//
//   - "Can this be interacted with" is the hover cursor's verdict
//     (Cursor::Hover): the mode the cursor would show over the object. A
//     mode other than none, the base mode, Point, Cast and Attack (and
//     their Unable forms) is an interact mode; the Unable form is "out of
//     reach", as is a named game object cursor the player cannot use now.
//   - With SoftTargetInteractRangeIsHard 0 something interactable right now
//     (in reach / usable) still counts past SoftTargetInteractRange.
//   - It is picked when something asks: a read of the token (the key's
//     InteractUnit("anyinteract"), UnitGUID, the icon's per-frame read while
//     SoftTargetIconInteract is on), and on the UI frame tick only while a
//     frame is registered for PLAYER_SOFT_INTERACT_CHANGED — so with no icon
//     and no listener nothing runs per frame.
//
// SoftTargetInteract's 1 is "gamepad"; 1.12 has no gamepad, so only 2 (KBM)
// and 3 (always) turn it on. It defaults to 3, what the "Enable Interact
// Key" setting writes: the key does nothing until bound. The other defaults
// are Classic Era's (0, 10, 0).

#include "target/SoftInteract.h"

#include "Game.h"
#include "Offsets.h"
#include "cursor/Hover.h"
#include "cvar/Factory.h"
#include "event/Custom.h"
#include "guid/Guid.h"
#include "object/Resolve.h"
#include "target/SoftTarget.h"
#include "tick/FrameTick.h"

#include <cstdint>

namespace Target::SoftInteract {

namespace {

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

bool Accept(const SoftTarget::Area &area, void *object, float distSq, void *ctx) {
    const Reach reach = ReachOf(object);
    if (reach == Reach::None)
        return false;
    const bool hard = *static_cast<const bool *>(ctx);
    return distSq <= area.rangeSq || (!hard && reach == Reach::Now);
}

uint64_t Pick() {
    SoftTarget::Area area;
    if (!SoftTarget::Begin(area, g_arc, g_range, 10.0f))
        return 0;
    bool hard = CVar::Factory::GetInt(g_rangeIsHard) != 0;
    return SoftTarget::Nearest(area, kTypeMask, &Accept, &hard);
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

void Refresh() { Set(SoftTarget::Enabled(g_enable) ? Pick() : 0); }

bool Watched() { return Event::Custom::HasListeners(_changed.Slot()); }

void OnFrameTick() {
    if (!Object::RegistryReady()) {
        g_soft = 0;
        return;
    }
    if (Watched())
        Refresh();
}

const Tick::FrameTick::AutoSubscribe _tick{&OnFrameTick};

void Register() {
    g_enable = CVar::Factory::Register("SoftTargetInteract", "3");
    g_arc = CVar::Factory::Register("SoftTargetInteractArc", "0");
    g_range = CVar::Factory::Register("SoftTargetInteractRange", "10");
    g_rangeIsHard = CVar::Factory::Register("SoftTargetInteractRangeIsHard", "0");
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

// Fresh when nothing watches; the frame tick's pick (this frame's) when a
// listener does.
uint64_t Get() {
    if (!Object::RegistryReady())
        return 0;
    if (!Watched())
        Refresh();
    return g_soft;
}

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
    return Get();
}

} // namespace Target::SoftInteract
