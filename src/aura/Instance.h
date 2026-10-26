// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

#pragma once

#include <cstdint>

// Aura instance IDs — retail's `auraInstanceID`. 1.12 has no aura identity:
// a unit's descriptor holds 48 spell IDs and nothing that tells one application
// of a spell from the next. This module gives each aura a number when it lands
// in a descriptor slot and keeps it until the aura leaves, so the same aura
// reads the same ID from every getter, and an aura removed and applied again
// gets a new one. IDs are never reused.
//
// Only the visible auras of a unit in view get an ID: the slots
// `Aura::Data::IsSlotPopulated` accepts (the UnitBuff gate). An aura only the
// `Aura::Source` cache or the out-of-range group array knows has none.
//
// Every change goes through one step: compare what was recorded for a slot
// with the live descriptor. The engine's aura handlers (`Aura::Source`'s
// Added / Removed / Stacks changes) run it for the slot they name, which is
// what tells a removal and re-application of one spell apart; the getters run
// it before answering; and `Drain` runs it over every tracked unit, which
// catches what no handler reports (a bulk clear, an object rebuilt).
//
// While recording is on, each unit also collects the change set since the last
// `Drain` — what `UNIT_AURA_DELTA` reports.

namespace Aura::Instance {

// The instance ID of the aura in absolute descriptor `slot` on `unit`, or 0
// when the slot holds no visible aura.
uint32_t IDForSlot(const uint8_t *unit, int slot);

// The absolute descriptor slot holding instance `id` on `unit`, or -1 when no
// aura on the unit has that ID.
int SlotForID(const uint8_t *unit, uint32_t id);

// Records the aura in `slot` on `guid` as updated in place — a timing change
// with no descriptor change behind it (a refresh, a duration packet). No-op
// for a slot with no ID or a unit nobody has asked about.
void NoteUpdated(uint64_t guid, int slot);

// One unit's changes since the last `Drain`. With `fullUpdate` set the lists
// are empty and the consumer re-reads every aura (the unit was first seen, or
// its object was rebuilt).
struct Delta {
    uint64_t guid;
    bool fullUpdate;
    const uint32_t *added;
    int addedCount;
    const uint32_t *updated;
    int updatedCount;
    const uint32_t *removed;
    int removedCount;
};

// Turns change collection on or off. Off, no change set is kept, and turning
// it off discards what was collected. IDs are assigned either way.
void SetRecording(bool on);

// Reconciles every tracked unit with its descriptor, then hands each unit with
// changes to `fn` and clears them. The deltas are copied out first, so `fn`
// may run Lua that calls the getters; changes made meanwhile wait for the next
// `Drain`.
void Drain(void (*fn)(const Delta &delta));

} // namespace Aura::Instance
