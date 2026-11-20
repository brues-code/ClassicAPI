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

// The damage meter's totals. Retail keeps them on the C side and its rules
// are not visible; where a rule here is not retail's documented contract, its
// comment says "our choice".
//
// Which storage an event fills:
//   - Damage: damage an ally did to a unit that is not an ally (friendly fire
//     is left out); Damage taken: damage an ally took, from anyone; Enemy
//     damage taken: damage an enemy took from an ally.
//   - Healing: effective healing (amount - overhealing) an ally did.
//   - Interrupts / Dispels: one per interrupt or dispel an ally made.
//   - Deaths: one per ally death, with the hit that killed it.
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
              const MeterUnit &other, double amount, double overkill, bool deadly) {
    SpellKey key{spellID, creatureName};
    Spell &spell = source.spells[key];
    spell.key = std::move(key);
    spell.amount += amount;
    spell.overkill += overkill;
    spell.deadly = spell.deadly || deadly;
    Target &target = spell.targets[other.key];
    target.unit = other;
    target.amount += amount;
    target.overkill += overkill;
    target.deadly = target.deadly || deadly;
}

void MergeSpell(Spell &into, const Spell &from) {
    into.key = from.key;
    into.amount += from.amount;
    into.overkill += from.overkill;
    into.deadly = into.deadly || from.deadly;
    for (const auto &[key, t] : from.targets) {
        Target &target = into.targets[key];
        if (target.unit.key == 0)
            target.unit = t.unit;
        target.amount += t.amount;
        target.overkill += t.overkill;
        target.deadly = target.deadly || t.deadly;
    }
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

bool Totals::Empty() const {
    for (const auto &storage : storages) {
        if (!storage.empty())
            return false;
    }
    return true;
}

void Totals::Merge(const Totals &from) {
    for (int i = 0; i < STORAGE_COUNT; ++i) {
        for (const auto &[key, s] : from.storages[i]) {
            Source &into = storages[i][key];
            if (into.unit.key == 0)
                into.unit = s.unit;
            into.total += s.total;
            if (s.deathMs >= into.deathMs) {
                into.deathMs = s.deathMs;
                into.deathRecapID = s.deathRecapID;
            }
            for (const auto &[spellKey, spell] : s.spells)
                MergeSpell(into.spells[spellKey], spell);
        }
    }
}

int64_t Session::DurationMs(int64_t nowMs) const {
    const int64_t end = open ? nowMs : endMs;
    return end > startMs ? end - startMs : 0;
}

void Meter::Reset() {
    sessions_.clear();
    overall_ = Totals{};
    overallClosedMs_ = 0;
    overallAny_ = false;
    expired_ = Totals{};
    expiredMs_ = 0;
    expiredAny_ = false;
    lastHits_.clear();
}

uint32_t Meter::Open(int64_t nowMs, const std::string &name) {
    Close(nowMs);
    Session s;
    s.id = nextID_++;
    s.name = name;
    s.startMs = nowMs;
    s.open = true;
    sessions_.push_back(std::move(s));
    overallAny_ = true;
    // Our choice: Expired holds the sessions that fell off the list since the
    // last reset, so Overall is always the kept sessions plus Expired.
    while (sessions_.size() > kMaxSessions) {
        const Session &oldest = sessions_.front();
        expired_.Merge(oldest.totals);
        expiredMs_ += oldest.DurationMs(nowMs);
        expiredAny_ = true;
        sessions_.pop_front();
    }
    return sessions_.back().id;
}

void Meter::Close(int64_t nowMs) {
    if (!IsOpen())
        return;
    Session &s = sessions_.back();
    s.endMs = nowMs > s.startMs ? nowMs : s.startMs;
    s.open = false;
    overallClosedMs_ += s.DurationMs(nowMs);
}

bool Meter::IsOpen() const { return !sessions_.empty() && sessions_.back().open; }

const Session *Meter::Current() const { return sessions_.empty() ? nullptr : &sessions_.back(); }

const Session *Meter::Find(uint32_t sessionID) const {
    for (const Session &s : sessions_) {
        if (s.id == sessionID)
            return &s;
    }
    return nullptr;
}

void Meter::Add(int storage, const Actor &subject, const Actor &other, uint32_t spellID,
                const std::string &creatureName, double amount, double overkill, bool deadly,
                int64_t timeMs, int deathRecapID) {
    Totals *both[] = {&sessions_.back().totals, &overall_};
    for (Totals *totals : both) {
        Source &source = SourceIn(*totals, storage, subject.unit);
        if (storage == STORAGE_DEATHS) {
            // The source counts deaths; its spells are the hits that killed.
            source.total += 1;
            source.deathMs = timeMs;
            source.deathRecapID = deathRecapID;
        } else {
            source.total += amount;
        }
        AddSpell(source, spellID, creatureName, other.unit, amount, overkill, deadly);
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
    if (e.kind == EventKind::Damage && dst.key != 0 && dst.ally && e.amount > 0)
        lastHits_[dst.key] = LastHit{e.source, e.spellID, e.amount, e.overkill};
    if (!IsOpen())
        return 0;
    Name(e);

    uint32_t changed = 0;
    auto add = [&](int storage, const Actor &subject, const Actor &other, uint32_t spellID,
                   const std::string &creatureName, double amount, double overkill,
                   bool deadly) {
        Add(storage, subject, other, spellID, creatureName, amount, overkill, deadly, e.timeMs,
            e.deathRecapID);
        changed |= 1u << storage;
    };
    const std::string &minion = e.source.petName;
    switch (e.kind) {
    case EventKind::Damage:
        if (e.amount <= 0)
            break;
        if (src.key != 0 && src.ally && !dst.ally)
            add(STORAGE_DAMAGE, e.source, e.dest, e.spellID, minion, e.amount, e.overkill,
                e.overkill > 0);
        if (dst.key != 0 && dst.ally)
            add(STORAGE_DAMAGE_TAKEN, e.dest, e.source, e.spellID, minion, e.amount, e.overkill,
                e.overkill > 0);
        if (dst.key != 0 && dst.enemy && src.key != 0 && src.ally)
            add(STORAGE_ENEMY_DAMAGE_TAKEN, e.dest, e.source, e.spellID, minion, e.amount,
                e.overkill, e.overkill > 0);
        break;
    case EventKind::Heal: {
        const double effective = e.amount - e.overhealing;
        if (src.key != 0 && src.ally && effective > 0)
            add(STORAGE_HEALING_AND_ABSORBS, e.source, e.dest, e.spellID, minion, effective, 0,
                false);
        break;
    }
    case EventKind::Interrupt:
        if (src.key != 0 && src.ally)
            add(STORAGE_INTERRUPTS, e.source, e.dest, e.spellID, minion, 1, 0, false);
        break;
    case EventKind::Dispel:
        if (src.key != 0 && src.ally)
            add(STORAGE_DISPELS, e.source, e.dest, e.spellID, minion, 1, 0, false);
        break;
    case EventKind::Death: {
        if (dst.key == 0 || !dst.ally)
            break;
        // Our choice: a death's spell is the last hit the ally took. Retail
        // shows the death recap (deathRecapID) instead, which only the local
        // player's deaths have.
        const auto hit = lastHits_.find(dst.key);
        if (hit != lastHits_.end()) {
            const LastHit h = hit->second;
            lastHits_.erase(hit);
            add(STORAGE_DEATHS, e.dest, h.attacker, h.spellID, h.attacker.petName, h.amount,
                h.overkill, true);
        } else {
            add(STORAGE_DEATHS, e.dest, Actor{}, 0, std::string(), 0, 0, true);
        }
        break;
    }
    }
    return changed;
}

View Meter::ViewOfType(int sessionType, int64_t nowMs) const {
    View v;
    switch (sessionType) {
    case SESSION_OVERALL: {
        v.totals = &overall_;
        const Session *open = IsOpen() ? Current() : nullptr;
        // Our choice: Overall's duration is the time in sessions since the
        // reset, so its per-second amounts leave out the time between fights.
        v.seconds = static_cast<double>(overallClosedMs_ + (open ? open->DurationMs(nowMs) : 0)) *
                    0.001;
        v.hasDuration = overallAny_;
        return v;
    }
    case SESSION_CURRENT: {
        const Session *s = Current();
        return s != nullptr ? ViewOfSession(s->id, nowMs) : v;
    }
    case SESSION_EXPIRED:
        v.totals = &expired_;
        v.seconds = static_cast<double>(expiredMs_) * 0.001;
        v.hasDuration = expiredAny_;
        return v;
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
    v.seconds = static_cast<double>(s->DurationMs(nowMs)) * 0.001;
    v.hasDuration = true;
    v.timedDeaths = true;
    v.startMs = s->startMs;
    return v;
}

} // namespace DamageMeter
