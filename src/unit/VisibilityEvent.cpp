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

// `UNIT_VISIBILITY_CHANGED: unitTarget, isVisible` — a unit came into the
// client's view or left it. A ClassicAPI extension; no client has it. It is
// the change of UnitIsVisible's answer, which is a unit-object lookup
// (0x00515940, type mask 8): `isVisible` is that lookup at the fire.
//
// A unit enters and leaves the object table through three CGUnit_C bodies
// that CGPlayer_C's overrides also run (benilla-re objectlayer/units
// #lifecycle, players#release-and-reactivation): PostInit when it is created,
// Reactivate (vtable slot 2) when a released unit comes back, and Disable /
// Release (slot 1) when it goes. Co-hooks on the three only note the GUID;
// the world tick fires, so no script runs inside the object manager's update.
// At the fire the lookup decides, which also absorbs a direct call of the two
// slot bodies on a unit that stays in the table (UnitXP calls them) and a
// leave and return within one frame.
//
// The fire goes out as every unit event does (Unit::Signal): once per token
// naming the unit then, and to the frames that registered its GUID. A unit
// that left has the tokens that name it out of view (`partyN`, a group
// member's `target`), and its GUID. A loading screen fires nothing: the
// object table is torn down and rebuilt behind it, and PLAYER_ENTERING_WORLD
// says so.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "object/Resolve.h"
#include "tick/WorldTick.h"
#include "unit/Signal.h"

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace Unit::VisibilityEvent {

namespace {

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("unitTarget", "UnitToken"),
    Game::Doc::Req("isVisible", "bool"),
};
const Game::Doc::Event kDoc{"Unit", "Fires when the unit comes into view or leaves it.",
                            kPayload};
const Event::Custom::AutoReserve kChanged{"UNIT_VISIBILITY_CHANGED", &kDoc};

// GUIDs whose unit entered or left since the last fire, each once.
std::vector<uint64_t> g_pending;
// The units last reported in view. One that leaves is erased again, so the
// set holds what is in view, not every unit seen.
std::unordered_set<uint64_t> g_shown;

void Note(const void *unit) {
    if (unit == nullptr || !::Unit::Signal::HasListeners(kChanged.Slot()))
        return;
    const uint64_t guid = ::Object::Guid(unit);
    if (guid != 0 && std::find(g_pending.begin(), g_pending.end(), guid) == g_pending.end())
        g_pending.push_back(guid);
}

using Unit_t = void(__fastcall *)(void *unit, void *edx);
using PostInit_t = void(__fastcall *)(void *unit, void *edx, uint32_t time, uint32_t a,
                                      uint32_t spawned);
Unit_t g_disable = nullptr;
Unit_t g_reactivate = nullptr;
PostInit_t g_postInit = nullptr;

void __fastcall Disable_h(void *unit, void *edx) {
    Note(unit);
    g_disable(unit, edx);
}

void __fastcall Reactivate_h(void *unit, void *edx) {
    g_reactivate(unit, edx);
    Note(unit);
}

void __fastcall PostInit_h(void *unit, void *edx, uint32_t time, uint32_t a, uint32_t spawned) {
    g_postInit(unit, edx, time, a, spawned);
    Note(unit);
}

const Game::HookAutoRegister _disableHook{Offsets::FUN_UNIT_DISABLE,
                                          reinterpret_cast<void *>(&Disable_h),
                                          reinterpret_cast<void **>(&g_disable)};
const Game::HookAutoRegister _reactivateHook{Offsets::FUN_UNIT_REACTIVATE,
                                             reinterpret_cast<void *>(&Reactivate_h),
                                             reinterpret_cast<void **>(&g_reactivate)};
const Game::HookAutoRegister _postInitHook{Offsets::FUN_UNIT_POST_INIT,
                                           reinterpret_cast<void *>(&PostInit_h),
                                           reinterpret_cast<void **>(&g_postInit)};

void Forget() {
    g_pending.clear();
    g_shown.clear();
}

void OnWorldTick() {
    if (Game::Read<uint8_t>(static_cast<uintptr_t>(Offsets::VAR_IN_WORLD)) == 0 ||
        !::Object::RegistryReady()) {
        Forget();
        return;
    }
    if (g_pending.empty())
        return;
    const int slot = kChanged.Slot();
    if (!::Unit::Signal::HasListeners(slot)) {
        Forget();
        return;
    }
    std::vector<uint64_t> fire;
    fire.swap(g_pending); // a handler can note the next fire's units
    for (const uint64_t guid : fire) {
        const bool visible = ::Object::ByGuid(Offsets::TYPEMASK_UNIT, guid) != nullptr;
        if (visible) {
            // Reported in view already: a direct slot call, or a leave and
            // return within one frame.
            if (!g_shown.insert(guid).second)
                continue;
        } else {
            // Fires also for a unit never reported in view: a frame can have
            // started listening while it was.
            g_shown.erase(guid);
        }
        ::Unit::Signal::FireValues(guid, slot, [&](void *L) {
            Game::Lua::PushBool(L, visible);
            return 1;
        });
    }
}

const Tick::WorldTick::AutoSubscribe _tick{&OnWorldTick};

} // namespace

} // namespace Unit::VisibilityEvent
