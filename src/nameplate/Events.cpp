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
// Events fire with the unit's GUID string as payload — vanilla has no
// `"nameplateN"` tokens (engine resolver isn't extensible) and the
// event dispatcher's format codes don't include a "push frame" type.
// Addons that need the frame call
// `C_NamePlate.GetNamePlateForUnit(<token-or-GUID-equivalent>)`
// reactively.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "guid/Guid.h"
#include "nameplate/Walk.h"
#include "tick/WorldTick.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

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
std::unordered_set<const void *> g_seenPlates;

void FireWithGUID(const char *eventName, uint64_t guid) {
    if (guid == 0)
        return;
    const int slot = Event::Custom::Lookup(eventName);
    if (slot < 0)
        return;
    char buf[Guid::STRING_SIZE];
    Guid::FormatAsString(guid, buf, sizeof buf);
    Event::Custom::Fire(slot, "%s", buf);
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

    // Fire CREATED for never-before-seen frame pointers, ADDED for
    // GUIDs not in last tick's snapshot.
    for (const auto &kv : g_currentTickPlates) {
        if (g_seenPlates.insert(kv.second).second)
            FireWithGUID(kEventCreated, kv.first);
        if (g_lastTickPlates.find(kv.first) == g_lastTickPlates.end())
            FireWithGUID(kEventUnitAdded, kv.first);
    }

    // Fire REMOVED for GUIDs in last tick's snapshot but not current.
    for (const auto &kv : g_lastTickPlates) {
        if (g_currentTickPlates.find(kv.first) == g_currentTickPlates.end())
            FireWithGUID(kEventUnitRemoved, kv.first);
    }

    g_lastTickPlates.swap(g_currentTickPlates);
}

} // namespace

static const Tick::WorldTick::AutoSubscribe _tickSub{&OnWorldTick};

} // namespace NamePlate::Events
