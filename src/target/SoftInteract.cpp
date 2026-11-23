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

// The soft-interact target — the modern Interact Key: the
// `SoftTargetInteract*` cvars, the `softinteract` / `anyinteract` tokens and
// PLAYER_SOFT_INTERACT_CHANGED; the key itself is the INTERACTTARGET binding
// calling `C_PlayerInteractionManager.InteractUnit("anyinteract")`.
//
// As the 3.80.2 Classic client runs it (docs/notes/soft-targeting-3802.md),
// from the soft driver (Target::SoftUnit):
//   - the candidates are Target::Priority's interact candidates: units, and
//     game objects within 16 yd, that the cursor over them would show an
//     interact mode for (1.12's own verdict, Cursor::Hover);
//   - the pick, on a rebuild (FUN_1405e1410 / 1280): a locked target that is
//     an interact candidate the player can use right now, when
//     SoftTargetMatchLocked matches it; else, with the player's own channel
//     object (the fishing bobber) a candidate, the best of those anywhere on
//     screen; else the best interact candidate in view inside
//     SoftTargetInteractArc;
//   - the check, every frame (FUN_1405e1900): still a candidate the cursor
//     shows an interact mode over, within SoftTargetInteractRange of the
//     player (3D, centre to centre) — or, with SoftTargetInteractRangeIsHard
//     0, usable right now from where the player stands;
//   - PLAYER_SOFT_INTERACT_CHANGED(old, new), first of the three soft events,
//     when the object changes or becomes usable / unusable.
// SoftTargetInteract's 1 is "gamepad"; 1.12 has no gamepad, so only 2 (KBM)
// and 3 (always) turn it on. The defaults are Classic Era's (1.15.9): off,
// arc 0, range 10, not a hard range.
//
// Not here: 3.80.2 also drops the object the player is already interacting
// with (its open gossip / vendor / loot) — 1.12 keeps no single GUID for it.

#include "target/SoftInteract.h"

#include "Game.h"
#include "Offsets.h"
#include "cursor/Hover.h"
#include "cvar/Factory.h"
#include "event/Custom.h"
#include "guid/Guid.h"
#include "object/Resolve.h"
#include "target/Priority.h"
#include "target/Reason.h"
#include "target/SoftTarget.h"
#include "unit/Identity.h"
#include "unit/Position.h"
#include "unit/TokenEvents.h"

#include <cstdint>

namespace Target::SoftInteract {

namespace {

using Cursor::Hover::Reach;

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
bool g_now = false; // usable right now
// What the last event told.
uint64_t g_told = 0;
bool g_toldNow = false;

void *ObjectOf(uint64_t guid) {
    return guid != 0 ? Object::ByGuid(Offsets::TYPEMASK_UNIT | Offsets::TYPEMASK_GAMEOBJECT, guid)
                     : nullptr;
}

Reach ReachOf(uint64_t guid) {
    void *object = ObjectOf(guid);
    return object != nullptr ? Cursor::Hover::InteractReach(object) : Reach::None;
}

bool On() {
    return SoftTarget::Enabled(g_enable) && (CVar::Factory::GetFloat(g_range) > 0.0f ||
                                             CVar::Factory::GetInt(g_rangeIsHard) == 0);
}

bool WithinRange(uint64_t guid) {
    void *player = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    float a[3], b[3];
    if (player == nullptr || !Unit::Position::Read(player, a) ||
        !Unit::Position::Read(ObjectOf(guid), b))
        return false;
    const float r = CVar::Factory::GetFloat(g_range);
    return Game::DistanceSq(a, b) <= r * r;
}

int __fastcall SettingChanged(CVar::Factory::Handle, const char *, const char *, void *) {
    Priority::Invalidate();
    return 1;
}

void Register() {
    const auto reg = [](const char *name, const char *help, const char *value) {
        return CVar::Factory::Register(name, help, Offsets::CVAR_CATEGORY_GAME, value,
                                       &SettingChanged);
    };
    g_enable = reg("SoftTargetInteract",
        "Sets when soft interact should be enabled. 0=off, 1=gamepad, 2=KBM, 3=always", "0");
    g_arc = reg("SoftTargetInteractArc",
        "0 = No yaw arc allowance, must be directly in front. 1 = Must be in front yaw arc. 2 = Can be anywhere in targeting area.", "0");
    g_range = reg("SoftTargetInteractRange",
        "Max range to soft target interacts (limited to tab targeting and individual interact ranges)", "10");
    g_rangeIsHard = reg("SoftTargetInteractRangeIsHard",
        "Sets if it should be a hard range cutoff, even for something you can interact with right now.", "0");
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

uint64_t Get() { return g_soft; }

// 3.80.2's ActionTarget for interact (FUN_1405dd580 with 0x100): with no
// soft object, the target; the soft object while the target is empty,
// loose or the soft object itself; the target when it is an interact
// candidate the player can use right now; else the soft object.
uint64_t GetAny() {
    const uint64_t target = Reason::Selection();
    if (g_soft == 0)
        return target;
    if (target == 0 || g_soft == target || Reason::IsLoose() ||
        !Priority::Has(target, Priority::Interact))
        return g_soft;
    return ReachOf(target) == Reach::Now ? target : g_soft;
}

bool InReach(void *object) { return Cursor::Hover::InteractReach(object) == Reach::Now; }

void Pick(uint32_t lockedFlags, uint64_t target) {
    const bool on = On();
    if ((lockedFlags & Priority::Interact) != 0) {
        if (!on) {
            g_soft = 0;
            return;
        }
        if (!Reason::IsLoose() && ReachOf(target) == Reach::Now) {
            g_soft = target;
            return;
        }
    } else if (!on) {
        g_soft = 0;
        return;
    }
    g_soft = Priority::First(Priority::Interact | Priority::OwnObject, 2);
    if (g_soft == 0)
        g_soft = Priority::First(Priority::Interact, CVar::Factory::GetInt(g_arc));
}

void Validate() {
    g_now = false;
    if (g_soft == 0)
        return;
    const Reach reach = ReachOf(g_soft);
    if (!Priority::Has(g_soft, Priority::Interact) || reach == Reach::None) {
        g_soft = 0;
        return;
    }
    g_now = reach == Reach::Now;
    if (!WithinRange(g_soft) && (CVar::Factory::GetInt(g_rangeIsHard) != 0 || !g_now)) {
        g_soft = 0;
        g_now = false;
    }
}

void Clear() {
    g_soft = 0;
    g_now = false;
}

void Notify() {
    if (g_soft == g_told && g_now == g_toldNow)
        return;
    const uint64_t old = g_told;
    g_told = g_soft;
    g_toldNow = g_now;
    // A unit `softinteract` gets that token's unit events (the engine watches
    // a unit that holds a token), so the registration moves before the event.
    if (old != g_soft) {
        Unit::TokenEvents::Refresh(old);
        Unit::TokenEvents::Refresh(g_soft);
    }
    char oldBuf[Guid::STRING_SIZE];
    char newBuf[Guid::STRING_SIZE];
    Event::Custom::Fire(_changed.Slot(), "%s%s",
                        old ? Guid::FormatAsString(old, oldBuf, sizeof oldBuf) : nullptr,
                        g_soft ? Guid::FormatAsString(g_soft, newBuf, sizeof newBuf) : nullptr);
}

} // namespace Target::SoftInteract
