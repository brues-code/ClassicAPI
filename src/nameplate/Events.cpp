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
// Per-frame poll: walk the engine's shown-plate list (`NamePlate::Walk`),
// diff against last frame's snapshot, fire events on real transitions only.
// Comparing frame to frame (instead of hooking acquire / release) keeps the
// hot per-unit `FUN_006086E0` un-hooked and folds a release and re-acquire
// inside one frame — the name-cache callback `FUN_005FB840` drops a plate so
// it rebuilds with the name — into no event at all.
//
// Payload shape (matches modern WoW exactly):
// - `NAME_PLATE_CREATED` — `arg1` is the nameplate **Frame**, the
//   engine's canonical wrapper table. The printf-style dispatcher
//   (`FUN_FIRE_EVENT`) has no code for a Lua object, so it goes out
//   through `Event::Custom::FireValues` (see `FireWithFrame`).
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
#include "unit/TokenEvents.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace NamePlate::Events {

namespace {

const Event::Custom::AutoReserve _evtCreated{"NAME_PLATE_CREATED"};
const Event::Custom::AutoReserve _evtUnitAdded{"NAME_PLATE_UNIT_ADDED"};
const Event::Custom::AutoReserve _evtUnitRemoved{"NAME_PLATE_UNIT_REMOVED"};

// A nameplated unit and its nameplate frame.
struct Plate {
    uint64_t guid;
    const void *plate;
};

// Previous tick's snapshot, in walk order. Compared against the next tick's
// walk to compute ADDED/REMOVED diffs. Flat arrays searched linearly: the
// engine shows a few dozen plates at most, and a hash container allocated a
// node per plate every frame.
std::vector<Plate> g_lastTickPlates;

// Scratch snapshot reused each tick, swapped into `g_lastTickPlates` at the
// end; `clear()` keeps the capacity, so steady state allocates nothing.
std::vector<Plate> g_currentTickPlates;

// GUIDs whose ADDED couldn't go out this tick (see `OnWorldTick`). Dropped
// from the snapshot before the swap so the next tick sees them as new again.
// File-static for the same reuse-the-capacity reason as the maps above.
std::vector<uint64_t> g_deferredAdds;

// Frame pointers we've ever surfaced as nameplate plates. First
// sighting fires NAME_PLATE_CREATED; same pointer reappearing (pool
// reuse) doesn't refire.
//
// Bounded by the engine's CGNamePlateFrame freelist high-water mark:
// `FUN_006087F0` first checks the global freelist head at
// `DAT_00c4d920` and recycles any waiting frame before falling back
// to `SMemAlloc(0x518)`. So the set grows only up to the peak
// simultaneous-visible-plate count for the session — typically
// <80 even in AV-scale scenes — then tops out as pool reuse covers all
// subsequent shows.
std::vector<const void *> g_seenPlates;

bool HasGuid(const std::vector<Plate> &plates, uint64_t guid) {
    for (const Plate &p : plates)
        if (p.guid == guid)
            return true;
    return false;
}

bool Seen(const void *plate) {
    return std::find(g_seenPlates.begin(), g_seenPlates.end(), plate) != g_seenPlates.end();
}

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

// True iff the engine has rebound `plate` to a unit other than `guid` since
// the tick that last saw it. Frames are pooled and reused (`FUN_006087F0`), so
// the pointer stays valid memory either way; the plate's own GUID
// (OFF_NAMEPLATE_GUID — kept on a recycled plate until `Initialize` rebinds
// it) says whose it is now.
bool PlateReassigned(const void *plate, uint64_t guid) {
    const auto *p = static_cast<const uint8_t *>(plate);
    return *reinterpret_cast<const uint64_t *>(p + Offsets::OFF_NAMEPLATE_GUID) != guid;
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

// Fire `event` with the nameplate Frame as its one payload value. The engine's
// printf-style dispatcher has no code for a Lua object, so the frame goes out
// through `Event::Custom::FireValues`, the value form of the same dispatch: each
// listener's OnEvent gets `arg1` set and restored by the argument runner, and a
// modern `function(self, event, namePlate)` gets it positionally. Setting
// `_G.arg1` around an empty-format fire instead reached only the global — the
// positional `namePlate` was nil.
//
// Returns whether the event went out. `false` when the slot isn't claimed yet
// or there's no Lua state — the caller must not treat the plate as announced.
bool FireWithFrame(const Event::Custom::AutoReserve &event, void *frame) {
    if (frame == nullptr)
        return false;
    const int slot = event.Slot();
    if (slot < 0)
        return false;

    void *L = Game::Lua::State();
    if (L == nullptr)
        return false;

    const int top = Game::Lua::GetTop(L);
    NamePlate::Info::PushNamePlateFrame(L, frame);
    Event::Custom::FireValues(slot, L, top + 1, 1);
    Game::Lua::SetTop(L, top);
    return true;
}

void OnWorldTick() {
    g_currentTickPlates.clear();
    NamePlate::Walk::ForEachNamePlatedUnit(
        [](const uint8_t *, const uint8_t *nameplate, uint64_t guid) {
            g_currentTickPlates.push_back({guid, nameplate});
        });

    // Fire CREATED (with the Frame as arg1) for never-before-seen
    // frame pointers; ADDED (with `"nameplateN"` token as arg1) for
    // GUIDs not in last tick's snapshot. The slot is assigned *before*
    // firing so the token resolves to the newly-added plate during the
    // event handler.
    //
    // Neither event is recorded as sent until it actually goes out. A fire
    // dropped because its slot isn't claimed yet retries next tick; otherwise
    // the plate stays unannounced until /reload clears the diff state. ADDED
    // also waits for its frame's CREATED, so a handler never gets a unit for a
    // plate it hasn't been introduced to. A deferred GUID gets no token slot
    // and no observers, and is kept out of the snapshot, so it produces no
    // REMOVED for an ADDED that never fired.
    g_deferredAdds.clear();
    for (const Plate &cur : g_currentTickPlates) {
        bool announced = Seen(cur.plate);
        if (!announced &&
            FireWithFrame(_evtCreated, const_cast<void *>(cur.plate))) {
            g_seenPlates.push_back(cur.plate);
            announced = true;
        }
        if (!HasGuid(g_lastTickPlates, cur.guid)) {
            if (!announced || _evtUnitAdded.Slot() < 0) {
                g_deferredAdds.push_back(cur.guid);
                continue;
            }
            const int slot = AssignSlot(cur.guid);
            // The unit now has a "nameplateN" token: every unit event the
            // engine fires for it fires for that token too.
            Unit::TokenEvents::Refresh(cur.guid);
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
    for (const Plate &last : g_lastTickPlates) {
        if (!HasGuid(g_currentTickPlates, last.guid)) {
            auto it = std::find(g_slots.begin(), g_slots.end(), last.guid);
            if (it == g_slots.end())
                continue;
            const int oneBased = static_cast<int>(it - g_slots.begin()) + 1;
            // Let the handler reach the frame it is being told about; live
            // state can no longer answer for it. Skipped once the engine has
            // rebound the frame to another unit.
            if (!PlateReassigned(last.plate, last.guid)) {
                g_removingGuid = last.guid;
                g_removingPlate = last.plate;
            }
            char tokenBuf[24];
            FireWithString(_evtUnitRemoved,
                FormatNamePlateToken(tokenBuf, sizeof tokenBuf, oneBased));
            g_removingGuid = 0;
            g_removingPlate = nullptr;
            *it = 0; // free the slot (no shift of survivors)
            Unit::TokenEvents::Refresh(last.guid);
            while (!g_slots.empty() && g_slots.back() == 0)
                g_slots.pop_back();
        }
    }

    // Deferred ADDs leave the snapshot so the next tick sees them as new.
    if (!g_deferredAdds.empty())
        g_currentTickPlates.erase(
            std::remove_if(g_currentTickPlates.begin(), g_currentTickPlates.end(),
                           [](const Plate &p) {
                               return std::find(g_deferredAdds.begin(), g_deferredAdds.end(),
                                                p.guid) != g_deferredAdds.end();
                           }),
            g_currentTickPlates.end());

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
    // Objects survive a /reload, and so does the engine's watch on them; drop
    // it with the tokens. The first post-reload tick re-fires ADDED, which
    // gives the tokens back.
    std::vector<uint64_t> slotted;
    slotted.swap(g_slots);
    for (uint64_t guid : slotted)
        Unit::TokenEvents::Refresh(guid);
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
