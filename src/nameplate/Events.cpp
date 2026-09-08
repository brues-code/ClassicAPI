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
//   `"nameplateN"` unit token (`N` = the plate's assigned slot). The token
//   resolves to the unit via the `nameplateN`-aware token resolver in
//   `unit/TokenExtensions.cpp` — addons can pass it straight to `UnitName`,
//   `UnitGUID`, etc., or to `GetNamePlateForUnit` for the frame.
//
// Slot assignment (retail-exact): a plate keeps its slot for its whole
// lifetime, so surviving plates are NEVER renumbered when another is removed
// — a removal frees the slot and the next new plate reuses the lowest free
// one. `g_slots` is therefore a sparse array (freed middle slots read back as
// the empty GUID 0 until reused), not a compacting list.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "nameplate/Walk.h"
#include "tick/WorldTick.h"
#include "unit/TokenObserver.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace NamePlate::Events {

namespace {

const Event::Custom::AutoReserve _evtCreated{"NAME_PLATE_CREATED"};
const Event::Custom::AutoReserve _evtUnitAdded{"NAME_PLATE_UNIT_ADDED"};
const Event::Custom::AutoReserve _evtUnitRemoved{"NAME_PLATE_UNIT_REMOVED"};

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

// Nameplate token slots: slot `i` (0-based) holds the GUID assigned to
// `nameplate(i+1)`, or 0 when free. Retail-exact slot assignment — a plate
// keeps its slot for its whole lifetime (survivors are never renumbered),
// UNIT_REMOVED frees the slot (sets 0), and a new plate reuses the lowest
// free slot. Sparse: freed middle slots stay as gaps until reused. Backs the
// `nameplateN` unit-token resolver in `unit/TokenExtensions.cpp`.
std::vector<uint64_t> g_slots;

// The (GUID, Frame) pair currently being torn down, published only for the
// duration of that GUID's `NAME_PLATE_UNIT_REMOVED` dispatch and cleared
// immediately after. `NamePlate::Info`'s getters fall back to it because the
// unit→plate binding they read is already gone by the time the event fires —
// see `PlateBeingRemoved` in `nameplate/Walk.h` for the retail precedent.
uint64_t g_removingGuid = 0;
const void *g_removingPlate = nullptr;

// True iff `plate` is bound to some unit in THIS tick's walk — i.e. the engine
// pulled the frame off its freelist and rebound it to another unit in the gap
// between the tick that last saw it and the tick that noticed it was gone.
// Frames are pooled and reused (`FUN_006087F0`), so the pointer stays valid
// memory either way; what makes it unservable is that it now belongs to
// somebody else.
bool PlateReassigned(const void *plate) {
    for (const auto &kv : g_currentTickPlates)
        if (kv.second == plate)
            return true;
    return false;
}

// Assign `guid` the lowest free slot, reusing a vacated one when present and
// growing the array only when every slot is occupied. Returns the 0-based
// slot. Never shifts an existing entry.
int AssignSlot(uint64_t guid) {
    for (size_t i = 0; i < g_slots.size(); ++i) {
        if (g_slots[i] == 0) {
            g_slots[i] = guid;
            return static_cast<int>(i);
        }
    }
    g_slots.push_back(guid);
    return static_cast<int>(g_slots.size() - 1);
}

// Fire `event` with a pre-formatted string as `arg1`. The engine
// dispatcher's `%s` format code pushes the C string into `_G.arg1`
// as a Lua string — no escaping concerns for our own input
// (`"nameplateN"`).
void FireWithString(const Event::Custom::AutoReserve &event, const char *value) {
    if (value == nullptr)
        return;
    const int slot = event.Slot();
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

// Descriptor-field observer callback — makes unit events (UNIT_HEALTH,
// UNIT_AURA, …) fire with the `"nameplateN"` token, so a nameplated unit is a
// first-class unit-event source exactly like a party member. Registered per
// nameplate GUID via `Unit::TokenObserver` on UNIT_ADDED, unregistered on
// UNIT_REMOVED. We reverse-look-up the changed GUID's slot at fire time
// (cheap; the array is small) so the token reflects its current assignment.
// The event id is the field index (`fieldOffset >> 2`); the engine fires every
// unit event as "%s" + token, so we match. A GUID no longer in any slot
// (removal raced this field change) simply drops the fire.
int __fastcall NamePlateFieldCb(uint32_t fieldOffset, uint32_t /*size*/,
                                uint32_t guidLo, uint32_t guidHi,
                                const uint32_t * /*oldValue*/, void * /*userArg*/) {
    const uint64_t guid =
        (static_cast<uint64_t>(guidHi) << 32) | static_cast<uint64_t>(guidLo);
    auto it = std::find(g_slots.begin(), g_slots.end(), guid);
    if (it == g_slots.end())
        return 1;
    const int oneBased = static_cast<int>(it - g_slots.begin()) + 1;
    char tokenBuf[24];
    FormatNamePlateToken(tokenBuf, sizeof tokenBuf, oneBased);
    using Fire_t = void(__cdecl *)(int eventId, const char *fmt, ...);
    reinterpret_cast<Fire_t>(static_cast<uintptr_t>(Offsets::FUN_FIRE_EVENT))(
        static_cast<int>(fieldOffset >> 2), "%s", tokenBuf);
    return 1;
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

void FireWithFrame(const Event::Custom::AutoReserve &event, void *frame) {
    if (frame == nullptr)
        return;
    const int slot = event.Slot();
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
    // GUIDs not in last tick's snapshot. The slot is assigned *before*
    // firing so the token resolves to the newly-added plate during the
    // event handler.
    for (const auto &kv : g_currentTickPlates) {
        if (g_seenPlates.insert(kv.second).second)
            FireWithFrame(_evtCreated, const_cast<void *>(kv.second));
        if (g_lastTickPlates.find(kv.first) == g_lastTickPlates.end()) {
            const int slot = AssignSlot(kv.first);
            // Watch this unit's fields so UNIT_HEALTH/UNIT_AURA/… fire with
            // its "nameplateN" token (the engine only watches its own
            // target/party/raid tokens, not nameplates).
            Unit::TokenObserver::Register(kv.first, &NamePlateFieldCb);
            char tokenBuf[24];
            FireWithString(_evtUnitAdded,
                FormatNamePlateToken(tokenBuf, sizeof tokenBuf, slot + 1));
        }
    }

    // Fire REMOVED for GUIDs in last tick's snapshot but not current. The
    // slot is *freed* (set 0), not erased, so surviving plates keep their
    // index (retail-exact — no shift); the next ADD reuses it. Token is
    // computed before freeing so the payload names the slot just vacated.
    // Trailing empties are trimmed so the array tracks the high-water mark.
    for (const auto &kv : g_lastTickPlates) {
        if (g_currentTickPlates.find(kv.first) == g_currentTickPlates.end()) {
            auto it = std::find(g_slots.begin(), g_slots.end(), kv.first);
            if (it == g_slots.end())
                continue;
            const int oneBased = static_cast<int>(it - g_slots.begin()) + 1;
            // Let the handler reach the frame it is being told about; live
            // state can no longer answer for it. Skipped once the engine has
            // rebound the frame to another unit.
            if (!PlateReassigned(kv.second)) {
                g_removingGuid = kv.first;
                g_removingPlate = kv.second;
            }
            char tokenBuf[24];
            FireWithString(_evtUnitRemoved,
                FormatNamePlateToken(tokenBuf, sizeof tokenBuf, oneBased));
            g_removingGuid = 0;
            g_removingPlate = nullptr;
            Unit::TokenObserver::Unregister(kv.first, &NamePlateFieldCb);
            *it = 0; // free the slot (no shift of survivors)
            while (!g_slots.empty() && g_slots.back() == 0)
                g_slots.pop_back();
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
// `g_slots` is also cleared so post-reload token slots start fresh.
void PrepareForReload() {
    g_seenPlates.clear();
    g_lastTickPlates.clear();
    // Objects survive a /reload, so their observer nodes do too. The first
    // post-reload tick re-fires ADDED and re-registers, which would stack a
    // second observer per field (the registrar never dedups) → double events.
    // Tear ours down here so re-registration starts clean (skip free slots).
    for (uint64_t guid : g_slots)
        if (guid != 0)
            Unit::TokenObserver::Unregister(guid, &NamePlateFieldCb);
    g_slots.clear();
}

static const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

// Exposed via `nameplate/Walk.h` so the `nameplateN` token resolver
// in `unit/TokenExtensions.cpp` can map an index to a GUID without seeing
// the internal array. Returns 0 for an out-of-range OR currently-free slot.
uint64_t GetGUIDByIndex(int oneBased) {
    if (oneBased <= 0)
        return 0;
    const size_t idx = static_cast<size_t>(oneBased - 1);
    if (idx >= g_slots.size())
        return 0;
    return g_slots[idx]; // 0 when the slot is free
}

// The frame `guid` is losing, for the span of its UNIT_REMOVED dispatch only
// (see `nameplate/Walk.h`).
const void *PlateBeingRemoved(uint64_t guid) {
    if (guid == 0 || guid != g_removingGuid)
        return nullptr;
    return g_removingPlate;
}

// Number of slots (1-based max index). Because the slot array is SPARSE,
// callers that iterate all plates must scan `1..GetSlotCount()` and skip the
// slots where `GetGUIDByIndex` returns 0 — they can't stop at the first gap.
int GetSlotCount() { return static_cast<int>(g_slots.size()); }

} // namespace NamePlate::Events
