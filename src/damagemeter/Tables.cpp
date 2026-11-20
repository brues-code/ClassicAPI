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

// The C_DamageMeter tables, as the 3.80.2 client builds them: the session
// (FUN_14191e340), the source (FUN_14191ebe0) and their lines (FUN_142987a80).
// Field names and types are retail's (DamageMeterDocumentation.lua). 1.12 has
// no specializations, so specIconID is 0, and no avoidable-spell list, so
// isAvoidable is false.
//
// Numbers:
//   - durations are whole seconds (ms / 1000), nil for 0 ms;
//   - a source's amountPerSecond is its amount over those whole seconds, 0
//     under a second; a line's is its amount over the exact seconds; both are
//     0 for Interrupts, Dispels and Deaths;
//   - deathTimeSeconds is the whole seconds from the session's start, -1 in
//     Overall.
//
// A source's lines, by the type's SpellDetailsDisplayType:
//   - SpellCasted: one line a spell; a minion's spells are lines of their own
//     with its name as creatureName; the details are empty.
//   - UnitSpecificSpellCasted: one line a spell and attacking unit, the
//     attacker in the details; isPet tells the source is a pet, isMob the
//     attacker is a creature.
//   - SpellAffected: one line a spell stopped or removed, its count.
//   - Deaths: one line a hit of the death, with its overkill and isDeadly and
//     the attacker's name.
//   - EnemyDamageTaken: one line an attacking unit, spellID 0.

#include "damagemeter/Tables.h"

#include "Game.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace DamageMeter::Tables {

namespace {

namespace Lua = Game::Lua;

// A creature has a creature record; a player or a pet is keyed by its GUID
// (3.80.2's "player-like" sources, unit details +0x54 clear).
bool PlayerLike(const MeterUnit &u) { return u.creatureID == 0; }

bool HasPerSecond(int type) {
    return type != TYPE_INTERRUPTS && type != TYPE_DISPELS && type != TYPE_DEATHS;
}

int64_t WholeSeconds(const View &view) { return view.durationMs / 1000; }

double SourcePerSecond(double amount, const View &view, int type) {
    const int64_t seconds = WholeSeconds(view);
    return HasPerSecond(type) && seconds > 0 ? amount / static_cast<double>(seconds) : 0;
}

double LinePerSecond(double amount, const View &view, int type) {
    return HasPerSecond(type) && view.durationMs > 0
               ? amount / (static_cast<double>(view.durationMs) * 0.001)
               : 0;
}

void SetDurationField(void *L, const View &view) {
    if (view.durationMs > 0)
        Lua::SetFieldNumber(L, "durationSeconds", static_cast<double>(WholeSeconds(view)));
}

// UnitClassification's word for a creature, "" for a player or a pet.
const char *ClassificationOf(const MeterUnit &u) {
    return PlayerLike(u) ? "" : u.classification.c_str();
}

// Opens `t[key] = {}` on the table at the top: leaves the key and the new
// table pushed for CloseField.
void OpenField(void *L, const char *key) {
    Lua::PushString(L, key);
    Lua::NewTable(L);
}

void CloseField(void *L) { Lua::SetTable(L, -3); }

// The sources in rank order: by amount, the most first; deaths in the order
// they came (3.80.2 walks its death map in key order and does not sort it).
std::vector<const Source *> Ranked(const View &view, int type) {
    std::vector<const Source *> out;
    const int storage = StorageOf(type);
    if (view.totals == nullptr || storage < 0)
        return out;
    if (storage == STORAGE_DEATHS) {
        for (const Source &death : view.totals->deaths)
            out.push_back(&death);
        return out;
    }
    for (const auto &entry : view.totals->storages[storage])
        out.push_back(&entry.second);
    std::sort(out.begin(), out.end(), [](const Source *a, const Source *b) {
        if (a->total != b->total)
            return a->total > b->total;
        return a->unit.name < b->unit.name;
    });
    return out;
}

struct Details {
    std::string unitName;
    std::string unitClassFilename;
    std::string classification;
    bool isPet = false;
    bool isMob = false;
    double amount = 0;
};

struct Line {
    uint32_t spellID = 0;
    double amount = 0;
    double perSecond = 0;
    std::string creatureName;
    double overkill = 0;
    bool deadly = false;
    Details details;
};

Details AttackerDetails(const MeterUnit &attacker, bool sourceIsPet, double amount) {
    Details d;
    d.unitName = attacker.name;
    d.unitClassFilename = attacker.classFilename;
    d.classification = ClassificationOf(attacker);
    d.isPet = sourceIsPet;
    d.isMob = !PlayerLike(attacker);
    d.amount = amount;
    return d;
}

// `byGuid`: the source was asked for by its GUID. 3.80.2's EnemyDamageTaken
// lines then carry no amountPerSecond and no class; asked for by creature ID
// (FUN_14298a680) they carry both.
std::vector<Line> Lines(const Source &source, const View &view, int type, bool byGuid) {
    std::vector<Line> out;
    switch (DetailsOf(type)) {
    case DETAILS_SPELL_CASTED:
        for (const auto &[key, spell] : source.spells) {
            Line line;
            line.spellID = key.spellID;
            line.amount = spell.amount;
            line.perSecond = LinePerSecond(spell.amount, view, type);
            line.creatureName = key.creatureName;
            out.push_back(std::move(line));
        }
        break;
    case DETAILS_UNIT_SPECIFIC_SPELL_CASTED:
        for (const auto &[key, spell] : source.spells) {
            for (const auto &[unitKey, target] : spell.targets) {
                Line line;
                line.spellID = key.spellID;
                line.amount = target.amount;
                line.perSecond = LinePerSecond(target.amount, view, type);
                line.details = AttackerDetails(target.unit, source.unit.isPet, target.amount);
                out.push_back(std::move(line));
            }
        }
        break;
    case DETAILS_SPELL_AFFECTED:
        for (const auto &[key, spell] : source.spells) {
            Line line;
            line.spellID = key.spellID;
            line.amount = spell.amount;
            out.push_back(std::move(line));
        }
        break;
    case DETAILS_DEATHS:
        for (const DeathHit &hit : source.hits) {
            if (hit.attacker.unit.key == 0)
                continue;
            Line line;
            line.spellID = hit.spellID;
            line.amount = hit.amount;
            line.overkill = hit.overkill;
            line.deadly = hit.deadly;
            line.details.unitName = hit.attacker.unit.name;
            out.push_back(std::move(line));
        }
        break;
    case DETAILS_ENEMY_DAMAGE_TAKEN: {
        std::map<uint64_t, std::pair<const MeterUnit *, double>> attackers;
        for (const auto &[key, spell] : source.spells) {
            for (const auto &[unitKey, target] : spell.targets) {
                auto &a = attackers[unitKey];
                a.first = &target.unit;
                a.second += target.amount;
            }
        }
        for (const auto &[unitKey, a] : attackers) {
            Line line;
            line.amount = a.second;
            line.details.unitName = a.first->name;
            if (!byGuid) {
                line.perSecond = LinePerSecond(a.second, view, type);
                line.details.unitClassFilename = a.first->classFilename;
            }
            out.push_back(std::move(line));
        }
        break;
    }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const Line &a, const Line &b) { return a.amount > b.amount; });
    return out;
}

bool Matches(const MeterUnit &u, bool byGuid, const char *guid, uint32_t creatureID) {
    return byGuid ? u.guid == guid : u.creatureID == creatureID;
}

// The source a SourceFrom* call names: by GUID, else by creature ID, else
// none. A death: the unit's latest in the view (our choice; 3.80.2's death
// lookup takes its map's key 0 whatever the GUID).
const Source *FindSource(const View &view, int storage, bool byGuid, const char *guid,
                         uint32_t creatureID) {
    if (view.totals == nullptr || storage < 0 || (!byGuid && creatureID == 0))
        return nullptr;
    if (storage == STORAGE_DEATHS) {
        const auto &deaths = view.totals->deaths;
        for (auto it = deaths.rbegin(); it != deaths.rend(); ++it) {
            if (Matches(it->unit, byGuid, guid, creatureID))
                return &*it;
        }
        return nullptr;
    }
    for (const auto &entry : view.totals->storages[storage]) {
        if (Matches(entry.second.unit, byGuid, guid, creatureID))
            return &entry.second;
    }
    return nullptr;
}

double DeathTimeSeconds(const Source &source, const View &view) {
    if (!view.timedDeaths)
        return -1;
    const int64_t ms = source.deathMs > view.startMs ? source.deathMs - view.startMs : 0;
    return static_cast<double>(ms / 1000);
}

int DisplayType(const MeterUnit &u) {
    if (!PlayerLike(u))
        return DISPLAY_NONE;
    if (u.ally)
        return DISPLAY_ALLY;
    return u.enemy ? DISPLAY_ENEMY : DISPLAY_NONE;
}

void PushDetails(void *L, const Details &d) {
    Lua::NewTable(L);
    Lua::SetFieldString(L, "unitName", d.unitName.c_str());
    Lua::SetFieldString(L, "unitClassFilename", d.unitClassFilename.c_str());
    Lua::SetFieldString(L, "classification", d.classification.c_str());
    Lua::SetFieldBool(L, "isPet", d.isPet);
    Lua::SetFieldBool(L, "isMob", d.isMob);
    Lua::SetFieldNumber(L, "amount", d.amount);
    Lua::SetFieldNumber(L, "specIconID", 0);
}

void PushLine(void *L, const Line &line) {
    Lua::NewTable(L);
    Lua::SetFieldNumber(L, "spellID", line.spellID);
    Lua::SetFieldNumber(L, "totalAmount", line.amount);
    Lua::SetFieldNumber(L, "amountPerSecond", line.perSecond);
    Lua::SetFieldString(L, "creatureName", line.creatureName.c_str());
    Lua::SetFieldNumber(L, "overkillAmount", line.overkill);
    Lua::SetFieldBool(L, "isAvoidable", false);
    Lua::SetFieldBool(L, "isDeadly", line.deadly);
    Lua::PushString(L, "combatSpellDetails");
    PushDetails(L, line.details);
    Lua::SetTable(L, -3);
}

void PushCombatSource(void *L, const Source &source, const View &view, int type) {
    const MeterUnit &u = source.unit;
    const bool playerLike = PlayerLike(u);
    const bool deaths = type == TYPE_DEATHS;
    const int display = DisplayType(u);
    Lua::NewTable(L);
    if (playerLike && !u.guid.empty())
        Lua::SetFieldString(L, "sourceGUID", u.guid.c_str());
    if (u.creatureID != 0)
        Lua::SetFieldNumber(L, "sourceCreatureID", u.creatureID);
    Lua::SetFieldString(L, "name", u.name.c_str());
    Lua::SetFieldString(L, "classFilename", playerLike ? u.classFilename.c_str() : "");
    Lua::SetFieldNumber(L, "specIconID", 0);
    Lua::SetFieldNumber(L, "totalAmount", source.total);
    Lua::SetFieldNumber(L, "amountPerSecond", SourcePerSecond(source.total, view, type));
    Lua::SetFieldBool(L, "isLocalPlayer", playerLike && u.isLocalPlayer);
    Lua::SetFieldNumber(L, "deathRecapID", deaths ? source.deathRecapID : 0);
    Lua::SetFieldNumber(L, "deathTimeSeconds", deaths ? DeathTimeSeconds(source, view) : 0);
    Lua::SetFieldString(L, "classification", ClassificationOf(u));
    Lua::SetFieldNumber(L, "sourceDisplayType", display);
    if (display == DISPLAY_ENEMY && !u.factionGroup.empty())
        Lua::SetFieldString(L, "factionGroup", u.factionGroup.c_str());
}

} // namespace

void PushCombatSession(void *L, const View &view, int type) {
    const std::vector<const Source *> sources = Ranked(view, type);
    double total = 0;
    Lua::NewTable(L);
    OpenField(L, "combatSources");
    int i = 0;
    for (const Source *s : sources) {
        total += s->total;
        PushCombatSource(L, *s, view, type);
        Lua::RawSetI(L, -2, ++i);
    }
    CloseField(L);
    Lua::SetFieldNumber(L, "maxAmount", sources.empty() ? 0 : sources.front()->total);
    Lua::SetFieldNumber(L, "totalAmount", total);
    SetDurationField(L, view);
}

void PushCombatSessionSource(void *L, const View &view, int type, const char *sourceGUID,
                             uint32_t sourceCreatureID) {
    const bool byGuid = sourceGUID != nullptr && *sourceGUID != '\0';
    const Source *source =
        FindSource(view, StorageOf(type), byGuid, sourceGUID, sourceCreatureID);
    std::vector<Line> lines;
    if (source != nullptr)
        lines = Lines(*source, view, type, byGuid);
    double total = 0;
    Lua::NewTable(L);
    OpenField(L, "combatSpells");
    int i = 0;
    for (const Line &line : lines) {
        total += line.amount;
        PushLine(L, line);
        Lua::RawSetI(L, -2, ++i);
    }
    CloseField(L);
    Lua::SetFieldNumber(L, "maxAmount", lines.empty() ? 0 : lines.front().amount);
    Lua::SetFieldNumber(L, "totalAmount", total);
}

void PushAvailableSessions(void *L, const Meter &meter, int64_t nowMs) {
    Lua::NewTable(L);
    int i = 0;
    for (const Session &s : meter.Sessions()) {
        Lua::NewTable(L);
        Lua::SetFieldNumber(L, "sessionID", s.id);
        Lua::SetFieldString(L, "name", s.name.c_str());
        View view;
        view.durationMs = s.DurationMs(nowMs);
        SetDurationField(L, view);
        Lua::RawSetI(L, -2, ++i);
    }
}

} // namespace DamageMeter::Tables
