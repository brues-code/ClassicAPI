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

// The damage meter's totals. The data model and the readback follow the 3.80.2
// client (DamageMeter.cpp, around 0x142989000), whose collector is not in its
// binary, so which event fills which storage is not visible there; where a
// rule here is neither 3.80.2's code nor retail's documented contract, its
// comment says "our choice".
//
// Which storage an event fills:
//   - Damage: damage an ally did to a unit that is not an ally (friendly fire
//     is left out); Damage taken: damage an ally took, from anyone; Enemy
//     damage taken: damage an enemy took from an ally.
//   - Healing: effective healing (amount - overhealing) an ally did.
//   - Interrupts / Dispels: one per interrupt or dispel an ally made.
//   - Deaths: one source per ally death, with the hits the ally took before
//     it (3.80.2 keeps one SourceCombatData per death, its hits in a vector).
//   - Absorbs and Avoidable damage taken stay empty: 1.12 reports no absorb
//     by a shield's caster and has no list of avoidable spells.

#include "damagemeter/Totals.h"

#include <algorithm>

namespace DamageMeter {

namespace {

Source &SourceIn(Totals &totals, int storage, const MeterUnit &unit) {
    Source &s = totals.storages[storage][unit.key];
    s.unit = unit; // the latest identity: a name that resolved since, a new owner
    return s;
}

void AddSpell(Source &source, uint32_t spellID, const std::string &creatureName,
              const MeterUnit &other, double amount) {
    SpellKey key{spellID, creatureName};
    Spell &spell = source.spells[key];
    spell.key = std::move(key);
    spell.amount += amount;
    Target &target = spell.targets[other.key];
    target.unit = other;
    target.amount += amount;
}

bool Fights(const MeterUnit &a, const MeterUnit &b) { return a.key != 0 && b.key != 0 && a.ally && b.enemy; }

} // namespace

int StorageOf(int type) {
    switch (type) {
    case TYPE_DAMAGE_DONE:
    case TYPE_DPS:
        return STORAGE_DAMAGE;
    case TYPE_HEALING_DONE:
    case TYPE_HPS:
        return STORAGE_HEALING_AND_ABSORBS;
    case TYPE_ABSORBS:
        return STORAGE_ABSORBS;
    case TYPE_INTERRUPTS:
        return STORAGE_INTERRUPTS;
    case TYPE_DISPELS:
        return STORAGE_DISPELS;
    case TYPE_DAMAGE_TAKEN:
        return STORAGE_DAMAGE_TAKEN;
    case TYPE_AVOIDABLE_DAMAGE_TAKEN:
        return STORAGE_AVOIDABLE_DAMAGE_TAKEN;
    case TYPE_DEATHS:
        return STORAGE_DEATHS;
    case TYPE_ENEMY_DAMAGE_TAKEN:
        return STORAGE_ENEMY_DAMAGE_TAKEN;
    default:
        return -1;
    }
}

int DetailsOf(int type) {
    switch (type) {
    case TYPE_DAMAGE_TAKEN:
    case TYPE_AVOIDABLE_DAMAGE_TAKEN:
        return DETAILS_UNIT_SPECIFIC_SPELL_CASTED;
    case TYPE_INTERRUPTS:
    case TYPE_DISPELS:
        return DETAILS_SPELL_AFFECTED;
    case TYPE_DEATHS:
        return DETAILS_DEATHS;
    case TYPE_ENEMY_DAMAGE_TAKEN:
        return DETAILS_ENEMY_DAMAGE_TAKEN;
    default:
        return DETAILS_SPELL_CASTED;
    }
}

int64_t Session::ActiveMs() const { return lastEventMs > startMs ? lastEventMs - startMs : 0; }

int64_t Session::DurationMs(int64_t nowMs) const {
    if (!open)
        return ActiveMs();
    return nowMs > startMs ? nowMs - startMs : 0;
}

void Meter::Reset() {
    sessions_.clear();
    nextID_ = 1;
    overall_ = Totals{};
    overallClosedMs_ = 0;
    lastHits_.clear();
}

uint32_t Meter::Open(int64_t nowMs, const std::string &name) {
    Close();
    Session s;
    s.id = nextID_++;
    s.name = name;
    s.startMs = nowMs;
    s.lastEventMs = nowMs;
    s.open = true;
    sessions_.push_back(std::move(s));
    while (sessions_.size() > kMaxSessions)
        sessions_.pop_front();
    return sessions_.back().id;
}

// The session's time is up to its last event; Overall adds it, as 3.80.2's
// end of a session (FUN_14298ad50) adds its duration to the manager's +0xa8.
void Meter::Close() {
    if (!IsOpen())
        return;
    Session &s = sessions_.back();
    s.open = false;
    overallClosedMs_ += s.ActiveMs();
}

bool Meter::IsOpen() const { return !sessions_.empty() && sessions_.back().open; }

void Meter::Rename(const std::string &name) {
    if (IsOpen())
        sessions_.back().name = name;
}

const Session *Meter::Current() const { return sessions_.empty() ? nullptr : &sessions_.back(); }

const Session *Meter::Find(uint32_t sessionID) const {
    for (const Session &s : sessions_) {
        if (s.id == sessionID)
            return &s;
    }
    return nullptr;
}

void Meter::Add(int storage, const Actor &subject, const Actor &other, uint32_t spellID,
                const std::string &creatureName, double amount) {
    Totals *both[] = {&sessions_.back().totals, &overall_};
    for (Totals *totals : both) {
        Source &source = SourceIn(*totals, storage, subject.unit);
        source.total += amount;
        AddSpell(source, spellID, creatureName, other.unit, amount);
    }
}

// Retail names a session after an enemy (the user's retail check); ours is
// the first enemy an ally fights in it (the plan's rule).
void Meter::Name(const MeterEvent &e) {
    Session &s = sessions_.back();
    if (!s.name.empty() || e.kind != EventKind::Damage)
        return;
    if (Fights(e.source.unit, e.dest.unit))
        s.name = e.dest.unit.name;
    else if (Fights(e.dest.unit, e.source.unit))
        s.name = e.source.unit.name;
}

uint32_t Meter::Feed(const MeterEvent &e) {
    const MeterUnit &src = e.source.unit;
    const MeterUnit &dst = e.dest.unit;
    if (e.kind == EventKind::Damage && dst.key != 0 && dst.ally && e.amount > 0) {
        std::deque<DeathHit> &hits = lastHits_[dst.key];
        hits.push_back(DeathHit{e.source, e.spellID, e.amount, e.overkill, false});
        if (hits.size() > kDeathHits)
            hits.pop_front();
    }
    if (!IsOpen())
        return 0;
    Name(e);

    uint32_t changed = 0;
    auto add = [&](int storage, const Actor &subject, const Actor &other, uint32_t spellID,
                   const std::string &creatureName, double amount) {
        Add(storage, subject, other, spellID, creatureName, amount);
        changed |= 1u << storage;
    };
    const std::string &minion = e.source.petName;
    switch (e.kind) {
    case EventKind::Damage:
        if (e.amount <= 0)
            break;
        if (src.key != 0 && src.ally && !dst.ally)
            add(STORAGE_DAMAGE, e.source, e.dest, e.spellID, minion, e.amount);
        if (dst.key != 0 && dst.ally)
            add(STORAGE_DAMAGE_TAKEN, e.dest, e.source, e.spellID, minion, e.amount);
        if (dst.key != 0 && dst.enemy && src.key != 0 && src.ally)
            add(STORAGE_ENEMY_DAMAGE_TAKEN, e.dest, e.source, e.spellID, minion, e.amount);
        break;
    case EventKind::Heal: {
        const double effective = e.amount - e.overhealing;
        if (src.key != 0 && src.ally && effective > 0)
            add(STORAGE_HEALING_AND_ABSORBS, e.source, e.dest, e.spellID, minion, effective);
        break;
    }
    case EventKind::Interrupt:
        if (src.key != 0 && src.ally)
            add(STORAGE_INTERRUPTS, e.source, e.dest, e.spellID, minion, 1);
        break;
    case EventKind::Dispel:
        if (src.key != 0 && src.ally)
            add(STORAGE_DISPELS, e.source, e.dest, e.spellID, minion, 1);
        break;
    case EventKind::Death: {
        if (dst.key == 0 || !dst.ally)
            break;
        Source death;
        death.unit = dst;
        death.total = 1;
        death.deathMs = e.timeMs;
        death.deathRecapID = e.deathRecapID;
        const auto hits = lastHits_.find(dst.key);
        if (hits != lastHits_.end()) {
            death.hits.assign(hits->second.begin(), hits->second.end());
            lastHits_.erase(hits);
            // Our choice: the last hit before the death is the deadly one
            // (the killing blow is logged before the death, CombatLog::Deaths).
            death.hits.back().deadly = true;
        }
        sessions_.back().totals.deaths.push_back(death);
        overall_.deaths.push_back(std::move(death));
        changed |= 1u << STORAGE_DEATHS;
        break;
    }
    }
    if (changed != 0)
        sessions_.back().lastEventMs = std::max(sessions_.back().lastEventMs, e.timeMs);
    return changed;
}

// Overall: the ended sessions' time and the open one's up to its last event
// (FUN_14298abc0: manager +0xa8, plus the current session's +0x00 - start).
// Expired holds nothing: 3.80.2 keeps no Expired session (its FromType
// builders answer it with the empty table, its duration with nil).
View Meter::ViewOfType(int sessionType, int64_t nowMs) const {
    View v;
    switch (sessionType) {
    case SESSION_OVERALL: {
        v.totals = &overall_;
        const Session *open = IsOpen() ? Current() : nullptr;
        v.durationMs = overallClosedMs_ + (open ? open->ActiveMs() : 0);
        return v;
    }
    case SESSION_CURRENT: {
        const Session *s = Current();
        return s != nullptr ? ViewOfSession(s->id, nowMs) : v;
    }
    default:
        return v;
    }
}

View Meter::ViewOfSession(uint32_t sessionID, int64_t nowMs) const {
    View v;
    const Session *s = Find(sessionID);
    if (s == nullptr)
        return v;
    v.totals = &s->totals;
    v.durationMs = s->DurationMs(nowMs);
    v.timedDeaths = true;
    v.startMs = s->startMs;
    return v;
}

} // namespace DamageMeter
