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

// The replay's damage meter. The recording's combat lines are mapped to the
// meter's events as the live meter maps its entries (damagemeter/Live.cpp):
// the same subevents, the same melee spell, a minion's deeds credited to its
// owner, creatures keyed by creature ID. Who is an ally and who is an enemy
// comes from each line's own flags, as the recorder's client set them; the
// recorder itself is the player whose flags carry AFFILIATION_MINE.
//
// A stretch is one session: it opens at the first hit between an ally and an
// enemy (a hostile or neutral unit) and stays open, so the meter at a time shows the fight up to
// that time. The stretch the viewer loads is one fight (an encounter or a
// range the user chose), so the live meter's combat boundaries are not
// repeated here (our choice).
//
// A pet's owner is the player that owned it in the movement file's state at
// the time, else in its first state; without a movement file a pet stays its
// own unit.

#include "replay/Meter.h"

#include "combatlog/Entry.h"
#include "damagemeter/Units.h"
#include "replay/Names.h"

#include <climits>
#include <string_view>

namespace Replay::Meter {

namespace {

namespace Flag = CombatLog::ObjectFlag;
using DamageMeter::Actor;
using DamageMeter::EventKind;
using DamageMeter::MeterEvent;
using DamageMeter::MeterUnit;

constexpr uint32_t kMinionFlags = Flag::TYPE_PET | Flag::TYPE_GUARDIAN;

DamageMeter::Meter g_meter;
const Recording *g_fed = nullptr; // the recording g_meter was fed from
size_t g_next = 0;                // its next event to feed
int32_t g_time = INT32_MIN;       // the time it was fed up to

bool IsPetGuid(const std::string &guid) { return guid.rfind("Pet-", 0) == 0; }

MeterUnit UnitOf(const Recording &r, int32_t index, uint32_t flags, int32_t t) {
    MeterUnit m;
    if (index < 0 || static_cast<size_t>(index) >= r.units.size())
        return m;
    const Unit &u = r.units[static_cast<size_t>(index)];
    m.name = UnitName(u);
    m.ally = (flags & Flag::AFFILIATION_GROUP) != 0;
    m.enemy = (flags & Flag::REACTION_ENEMY) != 0;
    m.isPlayer = u.player;
    m.isPet = IsPetGuid(u.guid) || (flags & kMinionFlags) != 0;
    m.isLocalPlayer = u.player && (flags & Flag::AFFILIATION_MINE) != 0;
    if (u.player) {
        if (const Appearance *look = AppearanceAt(u, t))
            DamageMeter::SetPlayerDetails(m, look->classId, look->race);
    } else if (!m.isPet) {
        m.creatureID = u.creatureID;
    }
    m.classification = DamageMeter::Classification(m.creatureID);
    if (m.creatureID != 0) {
        m.key = DamageMeter::CreatureKey(m.creatureID);
    } else {
        m.key = static_cast<uint64_t>(index) + 1;
        m.guid = u.guid;
    }
    return m;
}

// The player that owns a minion at `t`, -1 for none.
int32_t OwnerOf(const Recording &r, int32_t index, uint32_t flags, int32_t t) {
    const Unit &u = r.units[static_cast<size_t>(index)];
    if ((flags & kMinionFlags) == 0 && !IsPetGuid(u.guid))
        return -1;
    const UnitState *state = StateAt(u, t);
    if (state == nullptr && !u.states.empty())
        state = &u.states.front().second;
    const int32_t owner = state != nullptr ? state->owner : -1;
    if (owner < 0 || owner == index || static_cast<size_t>(owner) >= r.units.size() ||
        !r.units[static_cast<size_t>(owner)].player)
        return -1;
    return owner;
}

// The side of an event as the meter credits it: a minion's to its owner,
// with the minion's name kept (RedirectSourceToOwner).
Actor ActorOf(const Recording &r, int32_t index, uint32_t flags, int32_t t) {
    Actor a;
    const int32_t owner = index >= 0 ? OwnerOf(r, index, flags, t) : -1;
    if (owner < 0) {
        a.unit = UnitOf(r, index, flags, t);
        return a;
    }
    a.unit = UnitOf(r, owner, flags & ~kMinionFlags, t);
    a.petName = UnitName(r.units[static_cast<size_t>(index)]);
    return a;
}

// The meter's event for a line; false for one it does not count.
// Environmental damage has no source and is left out, as live.
bool EventOf(const Recording &r, const CombatEvent &e, MeterEvent &out) {
    const std::string_view type = e.type;
    out.spellID = e.spellID;
    if (type == "SWING_DAMAGE" || type == "RANGE_DAMAGE" || type == "SPELL_DAMAGE" ||
        type == "SPELL_PERIODIC_DAMAGE" || type == "DAMAGE_SHIELD" || type == "DAMAGE_SPLIT") {
        out.kind = EventKind::Damage;
        if (type == "SWING_DAMAGE")
            out.spellID = DamageMeter::kMeleeSpellID;
        out.amount = e.amount;
        out.overkill = e.overAmount;
    } else if (type == "SPELL_HEAL" || type == "SPELL_PERIODIC_HEAL") {
        out.kind = EventKind::Heal;
        out.amount = e.amount;
        out.overhealing = e.overAmount;
    } else if (type == "SPELL_INTERRUPT") {
        out.kind = EventKind::Interrupt;
        out.spellID = e.extraSpellID;
    } else if (type == "SPELL_DISPEL") {
        out.kind = EventKind::Dispel;
        out.spellID = e.extraSpellID;
    } else if (type == "UNIT_DIED") {
        out.kind = EventKind::Death;
    } else {
        return false;
    }
    if (out.kind == EventKind::Death ? e.dest < 0 : e.source < 0)
        return false;
    out.timeMs = e.t;
    // Only the source is credited to its owner: a pet that takes a hit or
    // dies is its own unit, as live.
    out.source = ActorOf(r, e.source, e.sourceFlags, e.t);
    out.dest.unit = UnitOf(r, e.dest, e.destFlags, e.t);
    return true;
}

// A pull: damage between an ally and an enemy, by the line's flags.
bool Pull(const CombatEvent &e, const MeterEvent &event) {
    if (event.kind != EventKind::Damage)
        return false;
    const auto fights = [](uint32_t a, uint32_t b) {
        return (a & Flag::AFFILIATION_GROUP) != 0 && (b & Flag::REACTION_ENEMY) != 0;
    };
    return fights(e.sourceFlags, e.destFlags) || fights(e.destFlags, e.sourceFlags);
}

} // namespace

const DamageMeter::Meter &At(const Recording &r, int32_t t) {
    if (&r != g_fed || t < g_time) {
        g_meter.Reset();
        g_fed = &r;
        g_next = 0;
    }
    g_time = t;
    for (; g_next < r.events.size() && r.events[g_next].t <= t; ++g_next) {
        const CombatEvent &e = r.events[g_next];
        MeterEvent event;
        if (!EventOf(r, e, event))
            continue;
        if (!g_meter.IsOpen() && Pull(e, event))
            g_meter.Open(e.t, std::string());
        g_meter.Feed(event);
    }
    return g_meter;
}

void Forget() {
    g_meter.Reset();
    g_fed = nullptr;
    g_next = 0;
    g_time = INT32_MIN;
}

} // namespace Replay::Meter
