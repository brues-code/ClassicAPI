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

// `BAG_UPDATE_DELAYED` event — fires once per frame in which any
// `BAG_UPDATE` fired, after all of them, as the 3.80.2 client fires it: its
// three BAG_UPDATE emitters arm a one-entry deferred event right after each
// BAG_UPDATE they fire (FUN_140EC0440), and the deferred-event flush near the
// end of the world update (FUN_140E9E460) fires it once and disarms it first,
// so a BAG_UPDATE raised by a handler arms the next frame's.
//
// Here the engine's three BAG_UPDATE emitters are co-hooked (names are
// benilla-re's), and each arms only when it fired a BAG_UPDATE:
//
//   1. `RaiseBagEvents` (`0x004F91A0`) — compares the ten bag slots
//      (4 bags, 6 bank bags) with the cached container GUIDs
//      (VAR_BAG_GUID_CACHE), rewrites the cache and fires BAG_CLOSED for
//      a changed slot that held a bag and BAG_UPDATE for one that holds
//      one now. So it arms when a cache entry changed to a GUID; a bag
//      taken out with nothing in its place fires BAG_CLOSED alone and
//      does not arm. Called by the bag-slot field watcher and at login.
//   2. `RaiseBagUpdate` (`0x004F9370`) — fires BAG_UPDATE for the player
//      or a cached container, nothing for any other GUID; it arms on the
//      same test. Its callers are the backpack and container slot
//      watchers, `Bag::BankUpdate` (the bank's main slots), and the
//      item-field watcher `0x004F9480`, which passes an item's container
//      — the PLAYER for equipped gear, so a durability or enchant change
//      on worn gear fires BAG_UPDATE(0) and therefore DELAYED, in 3.80.2
//      as well.
//   3. `OnBackpackSlotChanged` (`0x004F8DB0`) — the field watcher on
//      backpack slots 23..38 and keyring slots 81..112. It fires a
//      BAG_UPDATE on every call: keyring changes directly here, backpack
//      changes through `RaiseBagUpdate`. Standard watcher ABI
//      `__fastcall(offset, unused, guidLo, guidHi, previous*, user)`,
//      ret 0x10 — the same shape as `OnContainerSlotChanged`.
//   4. `Tick::WorldTick::AutoSubscribe` — shared per-frame
//      subscription (the engine's world-subsystem update at
//      `FUN_0066FD50`, single-hooked in `src/tick/WorldTick.cpp`).
//      Our callback drains `g_pending` and fires DELAYED if set.
//
// All hook targets are quiet (≤3 callers each, in code regions
// other DLLs don't touch). The previous attempt at this feature
// hooked `FUN_FIRE_EVENT` (100+ callers, in the chat path) and a
// per-frame UI dispatcher — both prime collision targets for
// SuperWoWhook / nampower / transmogfix. By contrast:
//
//   - `FUN_0066FD50` is the WORLD-subsystem update (not the UI one),
//     deep in the rendering pipeline. Only one caller, no DLL
//     touches it. It runs exactly once per frame and increments
//     the engine's own per-frame counter at `0x00C7B2C8`, so we
//     know it ticks 1:1 with frames.
//   - The bag-subsystem hooks (`FUN_004F91A0`, `FUN_004F9370`,
//     `FUN_004F8DB0`) live in the `0x004F8xxx`/`0x004F9xxx` region
//     none of the popular DLLs care about.
//
// Coverage verified by pattern-searching the `.text` for every
// caller of `FUN_FIRE_EVENT(0x148, "%d", ...)` (the BAG_UPDATE
// event ID): exactly three sites, all covered by these three
// hooks. Double-fire concern with `FUN_004F8DB0` → `FUN_004F9370`
// path is moot: `g_pending` is a bool, idempotent until the
// per-frame drain clears it.
//
// Unlike 3.80.2, the drain holds while the player is not in the world (see
// OnWorldTick): 1.12's sources keep firing behind a loading screen, where
// 3.80.2 gates them at the source.

#include "Game.h"
#include "Offsets.h"
#include "bag/UpdateDelayed.h"
#include "event/Custom.h"
#include "tick/WorldTick.h"
#include "unit/Identity.h"

#include <array>
#include <cstdint>

namespace Bag::UpdateDelayed {

namespace {

constexpr const char *kEventName = "BAG_UPDATE_DELAYED";

const Game::Doc::Event kUpdateDelayedDoc{
    "Container",
    "Fires once in each frame in which one or more BAG_UPDATE events fired, "
    "after all of them.",
    {}};
const Event::Custom::AutoReserve _reserve{kEventName, &kUpdateDelayedDoc};

// Set by the bag-update hooks, drained by the world-tick hook.
// Single bool, no atomics needed — the engine is single-threaded.
bool g_pending = false;

using BagGuidCache = std::array<uint64_t, Offsets::BAG_GUID_CACHE_COUNT>;

BagGuidCache ReadBagGuidCache() {
    BagGuidCache cache;
    for (size_t i = 0; i < cache.size(); ++i)
        cache[i] = Game::Read<uint64_t>(Offsets::VAR_BAG_GUID_CACHE + i * sizeof(uint64_t));
    return cache;
}

} // namespace

// `RaiseBagEvents` — `__cdecl void()`.
using RaiseBagEvents_t = void(__cdecl *)();
RaiseBagEvents_t RaiseBagEvents_o = nullptr;
void __cdecl RaiseBagEvents_h() {
    const BagGuidCache before = ReadBagGuidCache();
    RaiseBagEvents_o();
    const BagGuidCache after = ReadBagGuidCache();
    // A BAG_UPDATE fired for each entry rewritten to a bag (0x004F92CC).
    for (size_t i = 0; i < after.size(); ++i)
        if (after[i] != before[i] && after[i] != 0)
            g_pending = true;
}

// `RaiseBagUpdate` — `__stdcall void(containerGuidLo, containerGuidHi)`.
using RaiseBagUpdate_t = void(__stdcall *)(int guidLo, int guidHi);
RaiseBagUpdate_t RaiseBagUpdate_o = nullptr;
void __stdcall RaiseBagUpdate_h(int guidLo, int guidHi) {
    RaiseBagUpdate_o(guidLo, guidHi);
    // It fired for the player or a cached container (0x004F9386..0x004F93AC).
    const uint64_t guid = static_cast<uint32_t>(guidLo) | static_cast<uint64_t>(static_cast<uint32_t>(guidHi)) << 32;
    if (guid == Unit::Identity::PlayerGuid()) {
        g_pending = true;
        return;
    }
    for (const uint64_t bag : ReadBagGuidCache())
        if (bag == guid)
            g_pending = true;
}

// `OnBackpackSlotChanged` — field-watcher ABI, `__fastcall(offset,
// unused, guidLo, guidHi, previous*, user)`, callee cleans 0x10.
using BackpackSlotChanged_t = unsigned(__fastcall *)(unsigned offset, unsigned unused,
                                                      unsigned guidLo, unsigned guidHi,
                                                      const void *previous, void *user);
BackpackSlotChanged_t BackpackSlotChanged_o = nullptr;
unsigned __fastcall BackpackSlotChanged_h(unsigned offset, unsigned unused, unsigned guidLo,
                                          unsigned guidHi, const void *previous, void *user) {
    const unsigned result = BackpackSlotChanged_o(offset, unused, guidLo, guidHi, previous, user);
    g_pending = true;
    return result;
}

// Per-frame drain. Runs at the tail of each world tick (subscribed
// through `Tick::WorldTick`, which owns the single hook on
// `FUN_0066FD50`). All this frame's BAG_UPDATEs have already fired
// by the time we run; we just consume the pending flag and emit one
// DELAYED if set.
void OnWorldTick() {
    if (!g_pending)
        return;
    // Bag changes aren't meaningful until the player is actually in the world.
    // During any loading screen (initial login, zone/instance transition) the
    // engine re-populates bag slots as objects stream in (setting g_pending)
    // while still behind the screen; the per-frame world tick runs there too,
    // so firing BAG_UPDATE_DELAYED would make addons rescan bags repeatedly
    // mid-load for no real change. Hold the pending flag — do NOT clear it —
    // until the engine's in-world flag is set; the first in-world frame then
    // emits a single DELAYED covering the settled inventory (matching retail's
    // one BAG_UPDATE_DELAYED per load). VAR_IN_WORLD clears on every transition,
    // so this re-suppresses on later zone changes too, not just initial login.
    if (*reinterpret_cast<const volatile uint8_t *>(
            static_cast<uintptr_t>(Offsets::VAR_IN_WORLD)) == 0)
        return;
    g_pending = false;
    const int slot = _reserve.Slot();
    if (slot >= 0)
        Event::Custom::Fire(slot, "");
    // Notify C++ subscribers of the bag-change frame (see UpdateDelayed.h).
    for (auto *node = AutoSubscribe::head; node != nullptr; node = node->next)
        node->fn();
}

static const Game::HookAutoRegister _hookRaiseBagEvents{
    Offsets::FUN_RAISE_BAG_EVENTS,
    reinterpret_cast<void *>(&RaiseBagEvents_h),
    reinterpret_cast<void **>(&RaiseBagEvents_o)};

static const Game::HookAutoRegister _hookRaiseBagUpdate{
    Offsets::FUN_RAISE_BAG_UPDATE,
    reinterpret_cast<void *>(&RaiseBagUpdate_h),
    reinterpret_cast<void **>(&RaiseBagUpdate_o)};

static const Game::HookAutoRegister _hookBackpackSlot{
    Offsets::FUN_ON_BACKPACK_SLOT_CHANGED,
    reinterpret_cast<void *>(&BackpackSlotChanged_h),
    reinterpret_cast<void **>(&BackpackSlotChanged_o)};

static const Tick::WorldTick::AutoSubscribe _tickSub{&OnWorldTick};

} // namespace Bag::UpdateDelayed
