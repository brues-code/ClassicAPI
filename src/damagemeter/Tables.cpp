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

// The C_DamageMeter tables. Field names and types are retail's
// (Blizzard_APIDocumentationGenerated/DamageMeterDocumentation.lua). 1.12 has
// no specializations or death recap, so specIconID and deathRecapID are 0, and
// no avoidable-spell list, so isAvoidable is false.
//
// How a source's spells are listed follows Blizzard_DamageMeter's use of them
// (DamageMeterEntry.lua): a spell names its unit (combatSpellDetails.unitName)
// "when another unit is the subject and the player is the object (e.g. damage
// taken)". So for the types where the source took something (damage taken,
// enemy damage taken, deaths) there is one entry for each spell and unit that
// dealt it; for the others, one entry for each spell with no unit (our
// choice: retail's C side is not visible).

#include "damagemeter/Tables.h"

#include "Game.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace DamageMeter::Tables {

namespace {

namespace Lua = Game::Lua;

bool ByUnit(int storage) {
    return storage == STORAGE_DAMAGE_TAKEN || storage == STORAGE_AVOIDABLE_DAMAGE_TAKEN ||
           storage == STORAGE_ENEMY_DAMAGE_TAKEN || storage == STORAGE_DEATHS;
}

// amountPerSecond: the amount over the session's duration (our choice; 0 for
// a session with no time yet).
double PerSecond(double amount, const View &view) {
    return view.seconds > 0 ? amount / view.seconds : 0;
}

// Opens `t[key] = {}` on the table at the top: leaves the key and the new
// table pushed for CloseField.
void OpenField(void *L, const char *key) {
    Lua::PushString(L, key);
    Lua::NewTable(L);
}

void CloseField(void *L) { Lua::SetTable(L, -3); }

// The sources in rank order: by amount, the most first; deaths by time, the
// first first (our choice).
std::vector<const Source *> Ranked(const View &view, int storage) {
    std::vector<const Source *> out;
    if (view.totals == nullptr || storage < 0)
        return out;
    for (const auto &entry : view.totals->storages[storage])
        out.push_back(&entry.second);
    const bool deaths = storage == STORAGE_DEATHS;
    std::sort(out.begin(), out.end(), [deaths](const Source *a, const Source *b) {
        if (deaths ? a->deathMs != b->deathMs : a->total != b->total)
            return deaths ? a->deathMs < b->deathMs : a->total > b->total;
        return a->unit.name < b->unit.name;
    });
    return out;
}

using Line = std::pair<const Spell *, const Target *>;

double Amount(const Line &line) {
    return line.second != nullptr ? line.second->amount : line.first->amount;
}

std::vector<Line> Lines(const Source &source, int storage) {
    std::vector<Line> out;
    const bool byUnit = ByUnit(storage);
    for (const auto &entry : source.spells) {
        const Spell &spell = entry.second;
        if (!byUnit) {
            out.emplace_back(&spell, nullptr);
            continue;
        }
        for (const auto &target : spell.targets)
            out.emplace_back(&spell, &target.second);
    }
    std::sort(out.begin(), out.end(), [](const Line &a, const Line &b) {
        if (Amount(a) != Amount(b))
            return Amount(a) > Amount(b);
        return a.first->key.spellID < b.first->key.spellID;
    });
    return out;
}

const Source *FindSource(const View &view, int storage, const char *guid, uint32_t creatureID) {
    if (view.totals == nullptr || storage < 0)
        return nullptr;
    const bool byGuid = guid != nullptr && *guid != '\0';
    for (const auto &entry : view.totals->storages[storage]) {
        const MeterUnit &u = entry.second.unit;
        if (byGuid ? u.guid == guid : (creatureID != 0 && u.creatureID == creatureID))
            return &entry.second;
    }
    return nullptr;
}

double DeathTimeSeconds(const Source &source, const View &view, int storage) {
    if (storage != STORAGE_DEATHS)
        return 0;
    if (!view.timedDeaths)
        return -1;
    return static_cast<double>(source.deathMs - view.startMs) * 0.001;
}

int DisplayType(const MeterUnit &u) {
    if (u.ally)
        return DISPLAY_ALLY;
    return u.enemy ? DISPLAY_ENEMY : DISPLAY_NONE;
}

} // namespace

void PushSpellUnitDetails(void *L, const MeterUnit *unit, double amount) {
    const bool has = unit != nullptr && unit->key != 0;
    Lua::NewTable(L);
    Lua::SetFieldString(L, "unitName", has ? unit->name.c_str() : "");
    Lua::SetFieldString(L, "unitClassFilename", has ? unit->classFilename.c_str() : "");
    Lua::SetFieldString(L, "classification", has ? unit->classification.c_str() : "");
    Lua::SetFieldBool(L, "isPet", has && unit->isPet);
    Lua::SetFieldBool(L, "isMob", has && !unit->isPlayer && !unit->isPet);
    Lua::SetFieldNumber(L, "amount", amount);
    Lua::SetFieldNumber(L, "specIconID", 0);
}

void PushCombatSpell(void *L, const Spell &spell, const Target *target, const View &view) {
    const double amount = target != nullptr ? target->amount : spell.amount;
    Lua::NewTable(L);
    Lua::SetFieldNumber(L, "spellID", spell.key.spellID);
    Lua::SetFieldNumber(L, "totalAmount", amount);
    Lua::SetFieldNumber(L, "amountPerSecond", PerSecond(amount, view));
    Lua::SetFieldString(L, "creatureName", spell.key.creatureName.c_str());
    Lua::SetFieldNumber(L, "overkillAmount", target != nullptr ? target->overkill : spell.overkill);
    Lua::SetFieldBool(L, "isAvoidable", false);
    Lua::SetFieldBool(L, "isDeadly", target != nullptr ? target->deadly : spell.deadly);
    Lua::PushString(L, "combatSpellDetails");
    PushSpellUnitDetails(L, target != nullptr ? &target->unit : nullptr, amount);
    Lua::SetTable(L, -3);
}

void PushCombatSource(void *L, const Source &source, const View &view, int type) {
    const int storage = StorageOf(type);
    const MeterUnit &u = source.unit;
    Lua::NewTable(L);
    if (!u.guid.empty())
        Lua::SetFieldString(L, "sourceGUID", u.guid.c_str());
    if (u.creatureID != 0)
        Lua::SetFieldNumber(L, "sourceCreatureID", u.creatureID);
    Lua::SetFieldString(L, "name", u.name.c_str());
    Lua::SetFieldString(L, "classFilename", u.classFilename.c_str());
    Lua::SetFieldNumber(L, "specIconID", 0);
    Lua::SetFieldNumber(L, "totalAmount", source.total);
    Lua::SetFieldNumber(L, "amountPerSecond", PerSecond(source.total, view));
    Lua::SetFieldBool(L, "isLocalPlayer", u.isLocalPlayer);
    Lua::SetFieldNumber(L, "deathRecapID", 0);
    Lua::SetFieldNumber(L, "deathTimeSeconds", DeathTimeSeconds(source, view, storage));
    Lua::SetFieldString(L, "classification", u.classification.c_str());
    Lua::SetFieldNumber(L, "sourceDisplayType", DisplayType(u));
    if (!u.factionGroup.empty())
        Lua::SetFieldString(L, "factionGroup", u.factionGroup.c_str());
}

void PushCombatSession(void *L, const View &view, int type) {
    const std::vector<const Source *> sources = Ranked(view, StorageOf(type));
    double max = 0;
    double total = 0;
    Lua::NewTable(L);
    OpenField(L, "combatSources");
    int i = 0;
    for (const Source *s : sources) {
        max = std::max(max, s->total);
        total += s->total;
        PushCombatSource(L, *s, view, type);
        Lua::RawSetI(L, -2, ++i);
    }
    CloseField(L);
    Lua::SetFieldNumber(L, "maxAmount", max);
    Lua::SetFieldNumber(L, "totalAmount", total);
    if (view.hasDuration)
        Lua::SetFieldNumber(L, "durationSeconds", view.seconds);
}

void PushCombatSessionSource(void *L, const View &view, int type, const char *sourceGUID,
                             uint32_t sourceCreatureID) {
    const int storage = StorageOf(type);
    const Source *source = FindSource(view, storage, sourceGUID, sourceCreatureID);
    double max = 0;
    double total = 0;
    Lua::NewTable(L);
    OpenField(L, "combatSpells");
    if (source != nullptr) {
        int i = 0;
        for (const Line &line : Lines(*source, storage)) {
            // Our choice: the source's total is its lines' sum, so a death's
            // killing hits add up to the window's whole (the source counts
            // deaths in the session table).
            max = std::max(max, Amount(line));
            total += Amount(line);
            PushCombatSpell(L, *line.first, line.second, view);
            Lua::RawSetI(L, -2, ++i);
        }
    }
    CloseField(L);
    Lua::SetFieldNumber(L, "maxAmount", max);
    Lua::SetFieldNumber(L, "totalAmount", total);
}

void PushAvailableSessions(void *L, const Meter &meter, int64_t nowMs) {
    Lua::NewTable(L);
    int i = 0;
    for (const Session &s : meter.Sessions()) {
        Lua::NewTable(L);
        Lua::SetFieldNumber(L, "sessionID", s.id);
        Lua::SetFieldString(L, "name", s.name.c_str());
        Lua::SetFieldNumber(L, "durationSeconds",
                            static_cast<double>(s.DurationMs(nowMs)) * 0.001);
        Lua::RawSetI(L, -2, ++i);
    }
}

} // namespace DamageMeter::Tables
