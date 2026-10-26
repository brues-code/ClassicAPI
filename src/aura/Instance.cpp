// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// Aura instance IDs. See `Instance.h` for the contract.

#include "Instance.h"

#include "Game.h"
#include "Offsets.h"
#include "aura/Data.h"
#include "aura/Source.h"
#include "object/Resolve.h"
#include "tick/WorldTick.h"
#include "time/Clock.h"
#include "unit/Identity.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace Aura::Instance {

namespace {

constexpr int kSlots = Offsets::UNIT_AURA_TOTAL;
constexpr int kFlagBytes = kSlots / 2; // UNIT_FIELD_AURAFLAGS: a nibble per slot

// What was last seen of one unit's aura fields, the IDs given to its visible
// auras, and (while recording) its change set since the last Drain.
struct Tracked {
    const uint8_t *object = nullptr;
    uint32_t spell[kSlots] = {};
    uint8_t flags[kFlagBytes] = {};
    uint8_t stacks[kSlots] = {};
    uint32_t id[kSlots] = {}; // 0 = no visible aura in the slot
    bool full = false;
    std::vector<uint32_t> added, updated, removed;
};

std::unordered_map<uint64_t, Tracked> g_units;
uint32_t g_nextID = 1;
bool g_recording = false;

const uint8_t *Descriptor(const uint8_t *unit) {
    return Game::Read<const uint8_t *>(unit, Offsets::OFF_CGUNIT_OBJECT_FIELDS);
}

uint8_t Nibble(const uint8_t *flags, int slot) {
    return (flags[slot / 2] >> ((slot & 1) * 4)) & 0xF;
}

bool Contains(const std::vector<uint32_t> &v, uint32_t id) {
    return std::find(v.begin(), v.end(), id) != v.end();
}

bool Erase(std::vector<uint32_t> &v, uint32_t id) {
    const auto it = std::find(v.begin(), v.end(), id);
    if (it == v.end())
        return false;
    v.erase(it);
    return true;
}

void ClearPending(Tracked &t) {
    t.full = false;
    t.added.clear();
    t.updated.clear();
    t.removed.clear();
}

// The change-set writes, coalesced within one Drain interval: an aura added and
// removed before the consumer heard of it is never reported, and an added or
// removed aura is not also reported as updated. A pending full update already
// covers everything.
void NoteAdded(Tracked &t, uint32_t id) {
    if (g_recording && !t.full)
        t.added.push_back(id);
}

void NoteChanged(Tracked &t, uint32_t id) {
    if (g_recording && !t.full && !Contains(t.added, id) && !Contains(t.updated, id))
        t.updated.push_back(id);
}

void NoteRemoved(Tracked &t, uint32_t id) {
    if (!g_recording || t.full || Erase(t.added, id))
        return;
    Erase(t.updated, id);
    t.removed.push_back(id);
}

// Records `unit`'s aura fields as they are now, with a fresh ID for every
// visible aura. `full` marks the change set as a full update: the consumer has
// never been told about these IDs.
void Seed(Tracked &t, const uint8_t *unit, bool full) {
    const uint8_t *desc = Descriptor(unit);
    t.object = unit;
    ClearPending(t);
    t.full = full && g_recording;
    if (desc == nullptr) {
        std::memset(t.spell, 0, sizeof t.spell);
        std::memset(t.flags, 0, sizeof t.flags);
        std::memset(t.stacks, 0, sizeof t.stacks);
        std::memset(t.id, 0, sizeof t.id);
        return;
    }
    std::memcpy(t.spell, desc + Offsets::OFF_UNIT_FIELD_AURA, sizeof t.spell);
    std::memcpy(t.flags, desc + Offsets::OFF_UNIT_FIELD_AURAFLAGS, sizeof t.flags);
    std::memcpy(t.stacks, desc + Offsets::OFF_UNIT_FIELD_AURAAPPLICATIONS, sizeof t.stacks);
    for (int slot = 0; slot < kSlots; ++slot)
        t.id[slot] = Data::IsSlotPopulated(unit, slot) ? g_nextID++ : 0;
}

// Brings one slot's record in line with the live descriptor. A new spell, or a
// visibility change, ends the old instance and starts a new one; the same
// visible aura with new flags or stacks is the same instance, updated.
void SyncSlot(Tracked &t, const uint8_t *unit, const uint8_t *desc, int slot) {
    const uint32_t spell = Game::Read<uint32_t>(desc, Offsets::OFF_UNIT_FIELD_AURA + slot * 4);
    const uint8_t *flags = desc + Offsets::OFF_UNIT_FIELD_AURAFLAGS;
    const uint8_t nibble = Nibble(flags, slot);
    const uint8_t stacks = desc[Offsets::OFF_UNIT_FIELD_AURAAPPLICATIONS + slot];
    if (spell == t.spell[slot] && nibble == Nibble(t.flags, slot)) {
        if (stacks != t.stacks[slot]) {
            t.stacks[slot] = stacks;
            if (t.id[slot] != 0)
                NoteChanged(t, t.id[slot]);
        }
        return;
    }
    const bool visible = Data::IsSlotPopulated(unit, slot);
    if (visible && spell == t.spell[slot] && t.id[slot] != 0) {
        NoteChanged(t, t.id[slot]);
    } else {
        if (t.id[slot] != 0)
            NoteRemoved(t, t.id[slot]);
        t.id[slot] = visible ? g_nextID++ : 0;
        if (t.id[slot] != 0)
            NoteAdded(t, t.id[slot]);
    }
    t.spell[slot] = spell;
    // Only this slot's nibble: the byte's other half is the neighbour's, which
    // its own sync records.
    const uint8_t mask = static_cast<uint8_t>(0xF << ((slot & 1) * 4));
    t.flags[slot / 2] = static_cast<uint8_t>((t.flags[slot / 2] & ~mask) | (flags[slot / 2] & mask));
    t.stacks[slot] = stacks;
}

void SyncAll(Tracked &t, const uint8_t *unit) {
    const uint8_t *desc = Descriptor(unit);
    if (desc == nullptr)
        return;
    // The common case is no change at all: three compares, no per-slot work.
    if (std::memcmp(t.spell, desc + Offsets::OFF_UNIT_FIELD_AURA, sizeof t.spell) == 0 &&
        std::memcmp(t.flags, desc + Offsets::OFF_UNIT_FIELD_AURAFLAGS, sizeof t.flags) == 0 &&
        std::memcmp(t.stacks, desc + Offsets::OFF_UNIT_FIELD_AURAAPPLICATIONS,
                    sizeof t.stacks) == 0)
        return;
    for (int slot = 0; slot < kSlots; ++slot)
        SyncSlot(t, unit, desc, slot);
}

// The record for `unit`, created (seeded from the live fields) on first sight
// and re-seeded when its GUID now names a different object — the old one left
// view and a new one was built, so nothing recorded of it still holds. A
// rebuilt unit is always a full update (the consumer holds its old IDs). For a
// first sight, `full` says whether it is news to the consumer: true from the
// change handlers (an aura changed on a unit the consumer has no IDs for),
// false from the getters (the caller is reading the auras right now).
Tracked *Track(const uint8_t *unit, bool full, bool *seeded) {
    *seeded = false;
    const uint64_t guid = Unit::Identity::GuidForObject(unit);
    if (guid == 0 || Descriptor(unit) == nullptr)
        return nullptr;
    auto [it, inserted] = g_units.try_emplace(guid);
    Tracked &t = it->second;
    if (inserted || t.object != unit) {
        Seed(t, unit, inserted ? full : true);
        *seeded = true;
    }
    return &t;
}

// The engine's aura handlers name the slot that changed; syncing that slot is
// what keeps a spell removed and applied again in one slot two instances (the
// two arrive in separate update blocks, so each reaches its own sync).
void OnAuraChange(const Aura::Source::Change &c) {
    if (c.unit == nullptr || c.slot < 0 || c.slot >= kSlots)
        return;
    const auto *unit = static_cast<const uint8_t *>(c.unit);
    bool seeded;
    Tracked *t = Track(unit, /*full*/ true, &seeded);
    if (t != nullptr && !seeded)
        SyncSlot(*t, unit, Descriptor(unit), c.slot);
}

const Aura::Source::AutoObserve _obs{&OnAuraChange};

// Units that left view are forgotten; one coming back is seeded afresh with new
// IDs. While recording, `Drain` does this every frame; otherwise this sweep
// keeps the table to the units in view.
constexpr uint32_t kSweepIntervalMs = 1000;
uint32_t g_lastSweepMs = 0;

void OnWorldTick() {
    const uint32_t now = Time::Clock::NowMs();
    if (Time::Clock::Elapsed(g_lastSweepMs, now) < kSweepIntervalMs)
        return;
    g_lastSweepMs = now;
    for (auto it = g_units.begin(); it != g_units.end();) {
        if (Object::ByGuid(Offsets::TYPEMASK_UNIT, it->first) == nullptr)
            it = g_units.erase(it);
        else
            ++it;
    }
}

const Tick::WorldTick::AutoSubscribe _tick{&OnWorldTick};

// A delta copied out of its record, so `Drain`'s callback can run Lua that
// grows `g_units` without invalidating it.
struct OwnedDelta {
    uint64_t guid;
    bool full;
    std::vector<uint32_t> added, updated, removed;
};

} // namespace

uint32_t IDForSlot(const uint8_t *unit, int slot) {
    if (unit == nullptr || slot < 0 || slot >= kSlots)
        return 0;
    bool seeded;
    Tracked *t = Track(unit, /*full*/ false, &seeded);
    if (t == nullptr)
        return 0;
    if (!seeded)
        SyncSlot(*t, unit, Descriptor(unit), slot);
    return t->id[slot];
}

int SlotForID(const uint8_t *unit, uint32_t id) {
    if (unit == nullptr || id == 0)
        return -1;
    bool seeded;
    Tracked *t = Track(unit, /*full*/ false, &seeded);
    if (t == nullptr || seeded) // a fresh seed hands out new IDs only
        return -1;
    for (int slot = 0; slot < kSlots; ++slot) {
        if (t->id[slot] != id)
            continue;
        SyncSlot(*t, unit, Descriptor(unit), slot);
        return t->id[slot] == id ? slot : -1;
    }
    return -1;
}

void NoteUpdated(uint64_t guid, int slot) {
    if (slot < 0 || slot >= kSlots)
        return;
    const auto it = g_units.find(guid);
    if (it != g_units.end() && it->second.id[slot] != 0)
        NoteChanged(it->second, it->second.id[slot]);
}

void SetRecording(bool on) {
    if (on == g_recording)
        return;
    g_recording = on;
    if (!on)
        for (auto &[guid, t] : g_units)
            ClearPending(t);
}

void Drain(void (*fn)(const Delta &delta)) {
    std::vector<OwnedDelta> out;
    for (auto it = g_units.begin(); it != g_units.end();) {
        Tracked &t = it->second;
        const auto *unit = static_cast<const uint8_t *>(
            Object::ByGuid(Offsets::TYPEMASK_UNIT, it->first));
        if (unit == nullptr) {
            it = g_units.erase(it);
            continue;
        }
        if (unit != t.object)
            Seed(t, unit, /*full*/ true);
        else
            SyncAll(t, unit);
        if (t.full || !t.added.empty() || !t.updated.empty() || !t.removed.empty()) {
            out.push_back({it->first, t.full, std::move(t.added), std::move(t.updated),
                           std::move(t.removed)});
            ClearPending(t);
        }
        ++it;
    }
    for (const OwnedDelta &d : out) {
        const Delta view{d.guid,
                         d.full,
                         d.added.data(),
                         static_cast<int>(d.added.size()),
                         d.updated.data(),
                         static_cast<int>(d.updated.size()),
                         d.removed.data(),
                         static_cast<int>(d.removed.size())};
        fn(view);
    }
}

} // namespace Aura::Instance
