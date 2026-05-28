// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU Lesser General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

// Nameplate lifecycle events: NAME_PLATE_CREATED, NAME_PLATE_UNIT_ADDED,
// NAME_PLATE_UNIT_REMOVED.
//
// Per-frame poll: walk visible CGUnits with allocated nameplates, diff
// against last frame's snapshot, fire events on real transitions only.
// The engine's internal hide/show cycle (z-order rebuilds, anchor
// changes, ~7 callers of `FUN_00608A10` that transiently zero `+0xE60`)
// is absorbed because we only compare with the previous *frame's*
// state, not every transient `unit + 0xE60` write.
//
// Payload shape (matches modern WoW exactly):
// - `NAME_PLATE_CREATED` — `arg1` is the nameplate **Frame**. The
//   engine's printf-style dispatcher (`FUN_FIRE_EVENT`) only knows
//   `%s`/`%d`/`%u`/`%f` format codes with no "push Lua value" option,
//   so we route this event through a pre-set path: save current
//   `_G.arg1`, set it to the frame, fire with empty format
//   (dispatcher leaves `_G.arg<N>` alone when no codes are parsed),
//   restore.
// - `NAME_PLATE_UNIT_ADDED` / `_REMOVED` — `arg1` is the
//   `"nameplateN"` unit token (formatted from the plate's index in
//   `g_orderedGUIDs` at fire time). The token resolves to the unit
//   via the `nameplateN`-aware token resolver in `unit/TokenExtensions.cpp`
//   — addons can pass it straight to `UnitName`, `UnitGUID`, etc.,
//   or to `GetNamePlateForUnit` for the frame.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "nameplate/Walk.h"
#include "tick/WorldTick.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace NamePlate::Events {

namespace {

constexpr const char *kEventCreated = "NAME_PLATE_CREATED";
constexpr const char *kEventUnitAdded = "NAME_PLATE_UNIT_ADDED";
constexpr const char *kEventUnitRemoved = "NAME_PLATE_UNIT_REMOVED";

const Event::Custom::AutoReserve _r1{kEventCreated};
const Event::Custom::AutoReserve _r2{kEventUnitAdded};
const Event::Custom::AutoReserve _r3{kEventUnitRemoved};

// Previous tick's snapshot — GUID → nameplate-frame pointer for each
// nameplated unit. Compared against the next tick's walk to compute
// ADDED/REMOVED diffs.
std::unordered_map<uint64_t, const void *> g_lastTickPlates;

// Scratch map reused each tick, swapped into `g_lastTickPlates` at
// the end. File-static so we don't pay the constructor/destructor
// cycle every frame — `clear()` keeps the existing bucket capacity.
std::unordered_map<uint64_t, const void *> g_currentTickPlates;

// Frame pointers we've ever surfaced as nameplate plates. First
// sighting fires NAME_PLATE_CREATED; same pointer reappearing (pool
// reuse) doesn't refire.
//
// Bounded by the engine's CGNamePlateFrame freelist high-water mark:
// `FUN_006087F0` first checks the global freelist head at
// `DAT_00c4d920` and recycles any waiting frame before falling back
// to `SMemAlloc(0x518)`. So the set grows only up to the peak
// simultaneous-visible-plate count for the session — typically
// <80 even in AV-scale scenes (matching the `reserve(64)` below) —
// then tops out as pool reuse covers all subsequent shows.
std::unordered_set<const void *> g_seenPlates;

// Ordered list of currently-visible nameplate GUIDs, in
// creation-order. Append on UNIT_ADDED, erase on UNIT_REMOVED. Backs
// the `nameplateN` unit-token resolver in `unit/TokenExtensions.cpp`. Order
// matches modern WoW semantics: stable for the lifetime of each
// plate, gaps when middle plates vanish (until the next REMOVED
// shifts later entries down).
std::vector<uint64_t> g_orderedGUIDs;

// Fire `eventName` with a pre-formatted string as `arg1`. The engine
// dispatcher's `%s` format code pushes the C string into `_G.arg1`
// as a Lua string — no escaping concerns for our own input
// (`"nameplateN"`).
void FireWithString(const char *eventName, const char *value) {
    if (value == nullptr)
        return;
    const int slot = Event::Custom::Lookup(eventName);
    if (slot < 0)
        return;
    Event::Custom::Fire(slot, "%s", value);
}

// Format a 1-based nameplate index as `"nameplateN"` for ADDED /
// REMOVED event payloads. Buffer should be at least 24 bytes — 9
// for the prefix, up to 10 digits for the index, room for null.
// Returns `buf` for convenient inline use.
const char *FormatNamePlateToken(char *buf, size_t bufSize, int oneBasedIndex) {
    std::snprintf(buf, bufSize, "nameplate%d", oneBasedIndex);
    return buf;
}

// Fire `eventName` with the nameplate `Frame` set as `_G.arg1`.
// `FUN_FIRE_EVENT`'s format-string parser only handles primitive
// types, but it only mutates `_G.arg<N>` for codes it actually
// parses. With an empty format we pre-set `_G.arg1` and the
// dispatcher leaves it alone. Restore the previous value after
// fire so we don't leak our frame into unrelated global state.
//
// Lua-stack-clean: stack depth on entry == stack depth on exit.
using LuaRefRef_t = int(__fastcall *)(void *L, int t);
using LuaRefUnref_t = void(__fastcall *)(void *L, int t, int ref);
using LuaRawGetI_t = void(__fastcall *)(void *L, int t, int n);

void FireWithFrame(const char *eventName, void *frame) {
    if (frame == nullptr)
        return;
    const int slot = Event::Custom::Lookup(eventName);
    if (slot < 0)
        return;

    void *L = Game::Lua::State();
    if (L == nullptr)
        return;

    auto refRef = reinterpret_cast<LuaRefRef_t>(
        static_cast<uintptr_t>(Offsets::LUA_REF_REF));
    auto refUnref = reinterpret_cast<LuaRefUnref_t>(
        static_cast<uintptr_t>(Offsets::LUA_REF_UNREF));
    auto rawgeti = reinterpret_cast<LuaRawGetI_t>(
        Offsets::FUN_FRAMESCRIPT_PUSH_OBJECT);

    // Save current `_G.arg1` to the registry.
    Game::Lua::PushString(L, "arg1");
    Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
    const int savedRef = refRef(L, Game::Lua::REGISTRY_INDEX);

    // Set `_G.arg1 = frame`.
    Game::Lua::PushString(L, "arg1");
    NamePlate::Info::PushNamePlateFrame(L, frame);
    Game::Lua::SetTable(L, Game::Lua::GLOBALS_INDEX);

    // Fire — empty format, so the dispatcher doesn't touch `arg1`.
    Event::Custom::Fire(slot, "");

    // Restore previous `_G.arg1` from the saved registry ref.
    Game::Lua::PushString(L, "arg1");
    rawgeti(L, Game::Lua::REGISTRY_INDEX, savedRef);
    Game::Lua::SetTable(L, Game::Lua::GLOBALS_INDEX);
    refUnref(L, Game::Lua::REGISTRY_INDEX, savedRef);
}

void OnWorldTick() {
    g_currentTickPlates.clear();
    g_currentTickPlates.reserve(64); // typical visible-nameplate ceiling
    NamePlate::Walk::ForEachNamePlatedUnit(
        [](const uint8_t *, const uint8_t *nameplate,
           const uint8_t *instance) {
            const uint64_t guid = *reinterpret_cast<const uint64_t *>(instance);
            if (guid == 0)
                return;
            g_currentTickPlates.emplace(guid, nameplate);
        });

    // Fire CREATED (with the Frame as arg1) for never-before-seen
    // frame pointers; ADDED (with `"nameplateN"` token as arg1) for
    // GUIDs not in last tick's snapshot. New entries are appended to
    // the ordered list *before* firing so the token resolves to the
    // newly-added plate during the event handler.
    for (const auto &kv : g_currentTickPlates) {
        if (g_seenPlates.insert(kv.second).second)
            FireWithFrame(kEventCreated, const_cast<void *>(kv.second));
        if (g_lastTickPlates.find(kv.first) == g_lastTickPlates.end()) {
            g_orderedGUIDs.push_back(kv.first);
            char tokenBuf[24];
            FireWithString(kEventUnitAdded,
                FormatNamePlateToken(tokenBuf, sizeof tokenBuf,
                                     static_cast<int>(g_orderedGUIDs.size())));
        }
    }

    // Fire REMOVED for GUIDs in last tick's snapshot but not current.
    // We compute the token from the position *before* erasing so the
    // event payload reflects the slot the unit just vacated; the
    // handler can still resolve the token to the unit via
    // `g_orderedGUIDs[slot]` during dispatch. Later plates shift down
    // when we erase — matches modern semantics.
    for (const auto &kv : g_lastTickPlates) {
        if (g_currentTickPlates.find(kv.first) == g_currentTickPlates.end()) {
            auto it = std::find(g_orderedGUIDs.begin(), g_orderedGUIDs.end(),
                                kv.first);
            if (it == g_orderedGUIDs.end())
                continue;
            const int oneBased = static_cast<int>(it - g_orderedGUIDs.begin()) + 1;
            char tokenBuf[24];
            FireWithString(kEventUnitRemoved,
                FormatNamePlateToken(tokenBuf, sizeof tokenBuf, oneBased));
            g_orderedGUIDs.erase(it);
        }
    }

    g_lastTickPlates.swap(g_currentTickPlates);
}

} // namespace

static const Tick::WorldTick::AutoSubscribe _tickSub{&OnWorldTick};

// Called from `FrameScript_Initialize_h` ahead of the engine's Lua
// teardown. Clears the diff state alongside `NamePlate::Info`'s
// wrapper-cache reset so that on the first post-reload tick every
// currently-visible plate refires `NAME_PLATE_CREATED` and
// `NAME_PLATE_UNIT_ADDED`. Without this, addons that decorate via
// CREATED (pfUI: builds its overlay button per-pointer) never see
// the existing plates after a `/reload` — `g_seenPlates` would
// suppress every refire, and the freshly-built wrapper would lack
// the addon's `.nameplate` field.
//
// `g_orderedGUIDs` is also cleared so the post-reload token indices
// start at `nameplate1` again, matching the order plates re-fire in.
void PrepareForReload() {
    g_seenPlates.clear();
    g_lastTickPlates.clear();
    g_orderedGUIDs.clear();
}

// Exposed via `nameplate/Walk.h` so the `nameplateN` token resolver
// in `unit/TokenExtensions.cpp` can map an index to a GUID without seeing
// the internal vector.
uint64_t GetGUIDByIndex(int oneBased) {
    if (oneBased <= 0)
        return 0;
    const size_t idx = static_cast<size_t>(oneBased - 1);
    if (idx >= g_orderedGUIDs.size())
        return 0;
    return g_orderedGUIDs[idx];
}

} // namespace NamePlate::Events
