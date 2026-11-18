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

// C_CombatLogReplay: the combat logs in the Logs folder, their encounters,
// and one stretch of a log loaded for replay. A ClassicAPI extension; no
// Blizzard client replays its logs.
//
// A raid night's log is 100 MB and more, so reading one runs on a worker
// thread: GetEncounters and LoadReplay start the read and an event says when
// it is done (polled each world tick). The worker only reads files; the
// results are handed over on the main thread.
//
// Times in a log are seconds from its first line; times in the replay are
// seconds from the loaded stretch's start.

#include "Game.h"
#include "Offsets.h"
#include "aura/Data.h"
#include "cache/Record.h"
#include "creature/Info.h"
#include "event/Custom.h"
#include "item/Data.h"
#include "item/Record.h"
#include "model/Outfit.h"
#include "model/TryOn.h"
#include "player/Info.h"
#include "replay/Catalog.h"
#include "replay/Recording.h"
#include "spell/Lookup.h"
#include "tick/WorldTick.h"
#include "unit/SelectionColor.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace Replay::Api {

namespace {

namespace Lua = Game::Lua;
using Game::Doc::Opt;
using Game::Doc::Req;

// A read on a worker thread. The thread keeps its own reference, so a job
// nobody waits for any more finishes and frees itself.
template <typename T> struct Job {
    std::atomic<bool> done{false};
    T result{};
};

template <typename T, typename Fn> std::shared_ptr<Job<T>> Run(Fn fn) {
    auto job = std::make_shared<Job<T>>();
    std::thread([job, fn]() {
        try {
            job->result = fn();
        } catch (...) {
            // Out of memory on a huge log: the result stays empty.
        }
        job->done.store(true, std::memory_order_release);
    }).detach();
    return job;
}

std::unordered_map<std::string, Catalog::Index> g_indexes; // by log name
std::unordered_map<std::string, std::shared_ptr<Job<Catalog::Index>>> g_indexing;
std::unique_ptr<Recording> g_recording;
std::shared_ptr<Job<std::unique_ptr<Recording>>> g_loading;
std::string g_loadingLog;

// --- Events -----------------------------------------------------------------

const Game::Doc::Field kEncountersReadyPayload[] = {
    Req("logName", "cstring"),
};
const Game::Doc::Event kEncountersReadyDoc{
    "CombatLogReplay",
    "Fires when C_CombatLogReplay.GetEncounters has read a log; call it again for the encounters.",
    kEncountersReadyPayload};
const Event::Custom::AutoReserve kEncountersReady{"COMBAT_LOG_REPLAY_ENCOUNTERS_READY",
                                                  &kEncountersReadyDoc};

const Game::Doc::Field kLoadedPayload[] = {
    Req("logName", "cstring"),
    Req("success", "bool", "False when the log could not be read; there is no replay then."),
};
const Game::Doc::Event kLoadedDoc{
    "CombatLogReplay", "Fires when the stretch C_CombatLogReplay.LoadReplay asked for is loaded.",
    kLoadedPayload};
const Event::Custom::AutoReserve kLoaded{"COMBAT_LOG_REPLAY_LOADED", &kLoadedDoc};

void OnWorldTick() {
    for (auto it = g_indexing.begin(); it != g_indexing.end();) {
        if (!it->second->done.load(std::memory_order_acquire)) {
            ++it;
            continue;
        }
        const std::string log = it->first;
        g_indexes[log] = std::move(it->second->result);
        it = g_indexing.erase(it);
        Event::Custom::Fire(kEncountersReady.Slot(), "%s", log.c_str());
    }
    if (g_loading != nullptr && g_loading->done.load(std::memory_order_acquire)) {
        g_recording = std::move(g_loading->result);
        g_loading = nullptr;
        void *L = Lua::State();
        if (L == nullptr)
            return;
        const int top = Lua::GetTop(L);
        Lua::PushString(L, g_loadingLog.c_str());
        Lua::PushBool(L, g_recording != nullptr);
        Event::Custom::FireValues(kLoaded.Slot(), L, top + 1, 2);
        Lua::SetTop(L, top);
    }
}

const Tick::WorldTick::AutoSubscribe _tick{&OnWorldTick};

// --- Arguments --------------------------------------------------------------

// The log named by argument `arg`, or empty after raising the usage error.
std::string LogArg(void *L, int arg, const char *usage) {
    const char *name = Lua::IsString(L, arg) ? Lua::ToString(L, arg) : nullptr;
    if (name == nullptr || !Catalog::ValidName(name)) {
        Lua::Error(L, "%s", usage);
        return {};
    }
    return name;
}

double Seconds(int64_t ms) { return static_cast<double>(ms) / 1000.0; }

int64_t Milliseconds(double seconds) { return static_cast<int64_t>(std::llround(seconds * 1000.0)); }

// The loaded replay's unit at argument 1 (1-based), or null. A `timed`
// function takes the time as argument 2.
const Unit *UnitArg(void *L, const char *usage, bool timed = true) {
    if (!Lua::IsNumber(L, 1) || (timed && !Lua::IsNumber(L, 2))) {
        Lua::Error(L, "%s", usage);
        return nullptr;
    }
    if (g_recording == nullptr)
        return nullptr;
    const auto index = static_cast<int64_t>(Lua::ToNumber(L, 1));
    if (index < 1 || index > static_cast<int64_t>(g_recording->units.size()))
        return nullptr;
    return &g_recording->units[static_cast<std::size_t>(index - 1)];
}

int32_t TimeArg(void *L) { return static_cast<int32_t>(Milliseconds(Lua::ToNumber(L, 2))); }

// --- Functions --------------------------------------------------------------

int __fastcall Script_GetLogs(void *L) {
    Lua::NewTable(L);
    int i = 0;
    for (const Catalog::Log &log : Catalog::List()) {
        Lua::NewTable(L);
        Lua::SetFieldString(L, "name", log.name.c_str());
        Lua::SetFieldNumber(L, "size", static_cast<double>(log.size));
        Lua::SetFieldNumber(L, "modified", static_cast<double>(log.modified));
        Lua::SetFieldBool(L, "hasMovement", log.movement);
        Lua::RawSetI(L, -2, ++i);
    }
    return 1;
}

int __fastcall Script_GetEncounters(void *L) {
    const std::string log = LogArg(L, 1, "Usage: C_CombatLogReplay.GetEncounters(logName)");
    if (log.empty())
        return 0;
    const auto found = g_indexes.find(log);
    // A log that grew since it was read (the one the game writes now) is
    // read again.
    if (found == g_indexes.end() || found->second.size != Catalog::Size(Catalog::CombatPath(log))) {
        if (g_indexing.count(log) == 0)
            g_indexing[log] = Run<Catalog::Index>([log]() { return Catalog::Build(log); });
        if (found == g_indexes.end()) {
            Lua::PushNil(L);
            return 1;
        }
    }
    const Catalog::Index &index = found->second;
    if (!index.ok) {
        Lua::PushNil(L);
        return 1;
    }
    Lua::NewTable(L);
    int i = 0;
    for (const Catalog::Encounter &e : index.encounters) {
        Lua::NewTable(L);
        Lua::SetFieldNumber(L, "encounterID", e.id);
        Lua::SetFieldString(L, "encounterName", e.name.c_str());
        Lua::SetFieldNumber(L, "difficultyID", e.difficulty);
        Lua::SetFieldNumber(L, "groupSize", e.groupSize);
        Lua::SetFieldNumber(L, "mapID", e.map);
        if (e.success >= 0)
            Lua::SetFieldBool(L, "success", e.success != 0);
        Lua::SetFieldNumber(L, "startTime", Seconds(e.startMs - index.firstMs));
        Lua::SetFieldNumber(L, "endTime", Seconds(e.endMs - index.firstMs));
        Lua::RawSetI(L, -2, ++i);
    }
    Lua::PushNumber(L, Seconds(index.lastMs - index.firstMs));
    return 2;
}

int __fastcall Script_LoadReplay(void *L) {
    constexpr char kUsage[] = "Usage: C_CombatLogReplay.LoadReplay(logName, startTime, endTime)";
    const std::string log = LogArg(L, 1, kUsage);
    if (log.empty())
        return 0;
    if (!Lua::IsNumber(L, 2) || !Lua::IsNumber(L, 3)) {
        Lua::Error(L, "%s", kUsage);
        return 0;
    }
    // The times count from the log's first line, which its read found.
    const auto found = g_indexes.find(log);
    if (found == g_indexes.end() || !found->second.ok) {
        Lua::PushBool(L, false);
        return 1;
    }
    const int64_t firstMs = found->second.firstMs;
    const int64_t startMs = firstMs + Milliseconds(Lua::ToNumber(L, 2));
    const int64_t endMs = firstMs + Milliseconds(Lua::ToNumber(L, 3));
    if (endMs < startMs) {
        Lua::PushBool(L, false);
        return 1;
    }
    g_loadingLog = log;
    g_loading = Run<std::unique_ptr<Recording>>(
        [log, startMs, endMs]() { return Load(log, startMs, endMs); });
    Lua::PushBool(L, true);
    return 1;
}

int __fastcall Script_GetReplayInfo(void *L) {
    if (g_recording == nullptr) {
        Lua::PushNil(L);
        return 1;
    }
    const Recording &r = *g_recording;
    Lua::NewTable(L);
    Lua::SetFieldString(L, "logName", r.log.c_str());
    Lua::SetFieldNumber(L, "startTime", Seconds(r.startMs - r.logStartMs));
    Lua::SetFieldNumber(L, "duration", Seconds(r.endMs - r.startMs));
    Lua::SetFieldNumber(L, "mapID", r.map);
    Lua::SetFieldBool(L, "hasMovement", r.movement);
    Lua::SetFieldNumber(L, "firstUnitTime", Seconds(r.firstUnitMs));
    Lua::SetFieldNumber(L, "numUnits", static_cast<double>(r.units.size()));
    return 1;
}

// The unit's name: the log's, else the client's for its GUID (a player's from
// the name caches, a creature's from the creature cache). A unit only the
// movement file shows has none in the log. A lookup only: the viewer asks
// for an uncached creature (C_CreatureInfo.RequestLoadCreatureByID).
std::string UnitName(const Unit &unit) {
    if (!unit.name.empty())
        return unit.name;
    if (!unit.cachedName.empty())
        return unit.cachedName;
    if (unit.player) {
        constexpr std::string_view kPrefix = "Player-0-";
        if (unit.guid.rfind(kPrefix, 0) != 0)
            return {};
        const uint64_t guid = std::strtoull(unit.guid.c_str() + kPrefix.size(), nullptr, 16);
        char name[64];
        if (guid != 0 && Player::Info::NameFromGuid(guid, name, sizeof name))
            return name;
    } else if (unit.creatureID != 0) {
        const uint8_t *record = Creature::Info::CacheRecord(unit.creatureID);
        const char *name =
            record != nullptr ? Game::Read<const char *>(record, Offsets::OFF_CREATURE_NAME) : nullptr;
        if (name != nullptr)
            return name;
    }
    return {};
}

int __fastcall Script_GetUnits(void *L) {
    Lua::NewTable(L);
    if (g_recording == nullptr)
        return 1;
    int i = 0;
    for (const Unit &unit : g_recording->units) {
        Lua::NewTable(L);
        Lua::SetFieldString(L, "guid", unit.guid.c_str());
        const std::string name = UnitName(unit);
        if (!name.empty())
            Lua::SetFieldString(L, "name", name.c_str());
        Lua::SetFieldBool(L, "isPlayer", unit.player);
        if (unit.creatureID != 0)
            Lua::SetFieldNumber(L, "creatureID", unit.creatureID);
        Lua::RawSetI(L, -2, ++i);
    }
    return 1;
}

int __fastcall Script_GetUnitPosition(void *L) {
    const Unit *unit = UnitArg(L, "Usage: C_CombatLogReplay.GetUnitPosition(unitIndex, time)");
    Place place;
    if (unit == nullptr || !PlaceAt(*g_recording, *unit, TimeArg(L), place)) {
        Lua::PushNil(L);
        return 1;
    }
    Lua::PushNumber(L, place.x);
    Lua::PushNumber(L, place.y);
    Lua::PushNumber(L, place.z);
    Lua::PushNumber(L, place.facing);
    Lua::PushNumber(L, place.speed);
    Lua::PushBool(L, place.falling);
    Lua::PushBool(L, place.jumped);
    return 7;
}

int __fastcall Script_GetUnitLook(void *L) {
    const Unit *unit = UnitArg(L, "Usage: C_CombatLogReplay.GetUnitLook(unitIndex, time)");
    const Look *look = unit != nullptr ? LookAt(*unit, TimeArg(L)) : nullptr;
    if (look == nullptr) {
        Lua::PushNil(L);
        return 1;
    }
    Lua::PushNumber(L, look->display);
    Lua::PushNumber(L, look->nativeDisplay);
    Lua::PushNumber(L, look->mountDisplay);
    Lua::PushNumber(L, look->scale);
    Lua::PushNumber(L, look->bytes1 & 0xFF);         // stand state
    Lua::PushNumber(L, (look->bytes1 >> 16) & 0xFF); // shapeshift form
    Lua::PushBool(L, (look->bytes1 & Offsets::UNIT_BYTES_1_STEALTH_FLAG) != 0);
    Lua::PushNumber(L, look->bytes2 & 0xFF); // sheath state
    Lua::PushBool(L, DeadAt(*unit, TimeArg(L)));
    for (const uint32_t display : look->heldDisplay)
        Lua::PushNumber(L, display);
    return 12;
}

// What a creature holds in one slot, as its unit's held lane gives it.
int __fastcall Script_GetUnitHeldItem(void *L) {
    const Unit *unit = UnitArg(L, "Usage: C_CombatLogReplay.GetUnitHeldItem(unitIndex, time, inventorySlot)");
    const Look *look = unit != nullptr ? LookAt(*unit, TimeArg(L)) : nullptr;
    const int slot = Lua::IsNumber(L, 3) ? static_cast<int>(Lua::ToNumber(L, 3)) : 0;
    const int hand = slot == Offsets::INVSLOT_MAINHAND ? 0
                     : slot == Offsets::INVSLOT_OFFHAND ? 1
                     : slot == Offsets::INVSLOT_RANGED  ? 2
                                                        : -1;
    if (look == nullptr || hand < 0 || look->heldDisplay[hand] == 0) {
        Lua::PushNil(L);
        return 1;
    }
    const uint32_t info = look->heldInfo[hand * 2];
    Lua::PushNumber(L, look->heldDisplay[hand]);
    Lua::PushNumber(L, info & 0xFF);         // class
    Lua::PushNumber(L, (info >> 8) & 0xFF);  // subclass
    Lua::PushNumber(L, (info >> 24) & 0xFF); // inventory type
    Lua::PushNumber(L, look->heldInfo[hand * 2 + 1] & 0xFF); // sheathe type
    return 5;
}

// The events at or after `startTime` and before `endTime`, in time order.
int __fastcall Script_GetEvents(void *L) {
    if (!Lua::IsNumber(L, 1) || !Lua::IsNumber(L, 2)) {
        Lua::Error(L, "Usage: C_CombatLogReplay.GetEvents(startTime, endTime)");
        return 0;
    }
    Lua::NewTable(L);
    if (g_recording == nullptr)
        return 1;
    const auto from = static_cast<int32_t>(Milliseconds(Lua::ToNumber(L, 1)));
    const auto to = static_cast<int32_t>(Milliseconds(Lua::ToNumber(L, 2)));
    const std::vector<CombatEvent> &events = g_recording->events;
    auto it = std::lower_bound(events.begin(), events.end(), from,
                               [](const CombatEvent &e, int32_t t) { return e.t < t; });
    int i = 0;
    for (; it != events.end() && it->t < to; ++it) {
        const CombatEvent &e = *it;
        Lua::NewTable(L);
        Lua::SetFieldNumber(L, "time", Seconds(e.t));
        Lua::SetFieldString(L, "event", e.type.c_str());
        if (e.source >= 0)
            Lua::SetFieldNumber(L, "sourceIndex", e.source + 1);
        if (e.dest >= 0)
            Lua::SetFieldNumber(L, "destIndex", e.dest + 1);
        if (e.spellID != 0) {
            Lua::SetFieldNumber(L, "spellID", e.spellID);
            Lua::SetFieldString(L, "spellName", e.spellName.c_str());
        }
        if (e.type.rfind("SPELL_AURA_", 0) == 0)
            Lua::SetFieldString(L, "auraType", e.debuff ? "DEBUFF" : "BUFF");
        if (e.castEnd >= 0) {
            Lua::SetFieldNumber(L, "castTime", Seconds(e.castEnd - e.t));
            Lua::SetFieldBool(L, "castFailed", e.castFailed);
        }
        if (e.endsCast)
            Lua::SetFieldBool(L, "endsCast", true);
        if (e.extraSpellID != 0) {
            Lua::SetFieldNumber(L, "extraSpellID", e.extraSpellID);
            Lua::SetFieldString(L, "extraSpellName", e.extraSpellName.c_str());
        }
        if (e.castTarget >= 0)
            Lua::SetFieldNumber(L, "castTargetIndex", e.castTarget + 1);
        if (e.hasAmount) {
            Lua::SetFieldNumber(L, "amount", e.amount);
            Lua::SetFieldBool(L, "critical", e.critical);
        }
        if (!e.missType.empty())
            Lua::SetFieldString(L, "missType", e.missType.c_str());
        if (e.offHand)
            Lua::SetFieldBool(L, "offHand", true);
        if (!e.text.empty())
            Lua::SetFieldString(L, "text", e.text.c_str());
        Lua::RawSetI(L, -2, ++i);
    }
    return 1;
}

// The item class and subclass a held item's record gives, or false.
bool ItemClass(uint32_t itemID, uint32_t &itemClass, uint32_t &subclass) {
    const uint8_t *record = itemID != 0 ? Item::PeekRecord(itemID) : nullptr;
    if (record == nullptr) {
        if (itemID != 0)
            Item::Data::WarmCache(itemID);
        return false;
    }
    itemClass = Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_CLASS);
    subclass = Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_SUBCLASS);
    return true;
}

// What the unit holds in each hand, as the swing animation reads it: a
// player's main and off hand items (from the item cache, nil until cached),
// a creature's held item info.
int __fastcall Script_GetUnitWeapons(void *L) {
    const Unit *unit = UnitArg(L, "Usage: C_CombatLogReplay.GetUnitWeapons(unitIndex, time)");
    if (unit == nullptr)
        return 0;
    const int32_t t = TimeArg(L);
    uint32_t classes[2] = {}, subclasses[2] = {};
    bool held[2] = {};
    if (const Appearance *a = AppearanceAt(*unit, t)) {
        for (int hand = 0; hand < 2; ++hand)
            held[hand] = ItemClass(a->items[(hand == 0 ? Offsets::INVSLOT_MAINHAND : Offsets::INVSLOT_OFFHAND) - 1],
                                   classes[hand], subclasses[hand]);
    } else if (const Look *look = LookAt(*unit, t)) {
        for (int hand = 0; hand < 2; ++hand) {
            const uint32_t info = look->heldInfo[hand * 2];
            held[hand] = info != 0;
            classes[hand] = info & 0xFF;
            subclasses[hand] = (info >> 8) & 0xFF;
        }
    }
    for (int hand = 0; hand < 2; ++hand) {
        if (held[hand]) {
            Lua::PushNumber(L, classes[hand]);
            Lua::PushNumber(L, subclasses[hand]);
        } else {
            Lua::PushNil(L);
            Lua::PushNil(L);
        }
    }
    return 4;
}

// The unit's health at the time: the movement file's (UNIT_POWER, exact and
// whenever it changed), else the advanced blocks' (only on the lines that
// describe the unit).
int __fastcall Script_GetUnitHealth(void *L) {
    const Unit *unit = UnitArg(L, "Usage: C_CombatLogReplay.GetUnitHealth(unitIndex, time)");
    if (unit != nullptr) {
        const int32_t t = TimeArg(L);
        if (const Vitals *vitals = VitalsAt(*unit, t); vitals != nullptr && vitals->maxHealth > 0) {
            Lua::PushNumber(L, vitals->health);
            Lua::PushNumber(L, vitals->maxHealth);
            return 2;
        }
        if (const Health *health = HealthAt(*unit, t); health != nullptr && health->max > 0) {
            Lua::PushNumber(L, health->current);
            Lua::PushNumber(L, health->max);
            return 2;
        }
    }
    Lua::PushNil(L);
    return 1;
}

// The unit's power at the time, from the movement file.
int __fastcall Script_GetUnitPower(void *L) {
    const Unit *unit = UnitArg(L, "Usage: C_CombatLogReplay.GetUnitPower(unitIndex, time)");
    const Vitals *vitals = unit != nullptr ? VitalsAt(*unit, TimeArg(L)) : nullptr;
    if (vitals == nullptr) {
        Lua::PushNil(L);
        return 1;
    }
    Lua::PushNumber(L, vitals->powerType);
    Lua::PushNumber(L, vitals->power);
    Lua::PushNumber(L, vitals->maxPower);
    return 3;
}

void PushUnitIndex(void *L, int32_t index) {
    if (index >= 0)
        Lua::PushNumber(L, index + 1);
    else
        Lua::PushNil(L);
}

// Whom the unit targets, who owns it, its unit flags, level and faction
// template at the time, from the movement file; then the unit it swings at,
// its reaction toward the recording player as UnitReaction gives one (1 ..
// 8), and whether another player tapped it (UnitIsTapDenied).
int __fastcall Script_GetUnitState(void *L) {
    const Unit *unit = UnitArg(L, "Usage: C_CombatLogReplay.GetUnitState(unitIndex, time)");
    const UnitState *state = unit != nullptr ? StateAt(*unit, TimeArg(L)) : nullptr;
    if (state == nullptr) {
        Lua::PushNil(L);
        return 1;
    }
    PushUnitIndex(L, state->target);
    PushUnitIndex(L, state->owner);
    Lua::PushNumber(L, state->flags);
    Lua::PushNumber(L, state->level);
    Lua::PushNumber(L, state->faction);
    PushUnitIndex(L, state->attackTarget);
    Lua::PushNumber(L, state->reaction + 1);
    Lua::PushBool(L, (state->dynamicFlags & Offsets::UNIT_DYNFLAG_TAPPED) != 0 &&
                         (state->dynamicFlags & Offsets::UNIT_DYNFLAG_TAPPED_BY_PLAYER) == 0);
    return 8;
}

// The unit's selection color at the time from the recording player's view,
// as UnitSelectionColor(unit) gave it then.
int __fastcall Script_GetUnitSelectionColor(void *L) {
    const Unit *unit = UnitArg(L, "Usage: C_CombatLogReplay.GetUnitSelectionColor(unitIndex, time)");
    const UnitState *state = unit != nullptr ? StateAt(*unit, TimeArg(L)) : nullptr;
    if (state == nullptr) {
        Lua::PushNil(L);
        return 1;
    }
    const uint32_t argb = ::Unit::SelectionColor::Color(static_cast<int>(state->selection));
    Lua::PushNumber(L, ((argb >> 16) & 0xFF) / 255.0);
    Lua::PushNumber(L, ((argb >> 8) & 0xFF) / 255.0);
    Lua::PushNumber(L, (argb & 0xFF) / 255.0);
    Lua::PushNumber(L, (argb >> 24) / 255.0);
    return 4;
}

// The auras on the unit at the time: the movement file's UNIT_AURAS lines
// with the combat log's changes between them, or only the log's without a
// movement file. The log's line names who applied one, when it saw it.
int __fastcall Script_GetUnitAuras(void *L) {
    const Unit *unit = UnitArg(L, "Usage: C_CombatLogReplay.GetUnitAuras(unitIndex, time)");
    Lua::NewTable(L);
    if (unit == nullptr)
        return 1;
    const int32_t t = TimeArg(L);
    int i = 0;
    for (const AuraSpan &a : unit->auras) {
        if (!(a.from <= t && t < a.to))
            continue;
        Lua::NewTable(L);
        Lua::SetFieldNumber(L, "spellID", a.spellID);
        Lua::SetFieldString(L, "auraType", a.debuff ? "DEBUFF" : "BUFF");
        // Script_UnitBuff / Script_UnitDebuff's spell gate (0x00519860). A
        // spell this client does not have (a log from another) is not hidden.
        const uint8_t *spell = Spell::Lookup::RecordForID(static_cast<int>(a.spellID));
        if (spell != nullptr && !::Aura::Data::IsVisible(spell))
            Lua::SetFieldBool(L, "isHidden", true);
        if (const uint32_t *stacks = StacksAt(a, t))
            Lua::SetFieldNumber(L, "applications", *stacks);
        if (a.caster >= 0)
            Lua::SetFieldNumber(L, "sourceIndex", a.caster + 1);
        Lua::SetFieldNumber(L, "applied", Seconds(a.from));
        if (a.ended) {
            Lua::SetFieldNumber(L, "expirationTime", Seconds(a.to));
            if (a.renewed != INT32_MIN)
                Lua::SetFieldNumber(L, "duration", (static_cast<double>(a.to) - a.renewed) / 1000.0);
        }
        Lua::RawSetI(L, -2, ++i);
    }
    return 1;
}

// The cast the unit began last by the time, with when it began, when it was
// due to end and when it stopped. A channel's end is its start and the
// spell's duration (Spell::Lookup::DurationMs, as UnitChannelInfo times
// another unit's channel), or its stop when it ran longer.
int __fastcall Script_GetUnitCast(void *L) {
    const Unit *unit = UnitArg(L, "Usage: C_CombatLogReplay.GetUnitCast(unitIndex, time)");
    const Cast *cast = unit != nullptr ? CastAt(*unit, TimeArg(L)) : nullptr;
    if (cast == nullptr) {
        Lua::PushNil(L);
        return 1;
    }
    int64_t end = cast->end;
    if (cast->channel) {
        const int duration = Spell::Lookup::DurationMs(Spell::Lookup::RecordForID(static_cast<int>(cast->spellID)), false);
        end = cast->start + static_cast<int64_t>(duration > 0 ? duration : 0);
        if (cast->stop != INT32_MAX && end < cast->stop)
            end = cast->stop;
    }
    Lua::PushNumber(L, cast->spellID);
    Lua::PushNumber(L, Seconds(cast->start));
    Lua::PushNumber(L, Seconds(end));
    if (cast->stop != INT32_MAX)
        Lua::PushNumber(L, Seconds(cast->stop));
    else
        Lua::PushNil(L);
    Lua::PushBool(L, cast->channel);
    Lua::PushBool(L, cast->interrupted);
    if (cast->interrupter >= 0)
        Lua::PushNumber(L, cast->interrupter + 1);
    else
        Lua::PushNil(L);
    return 7;
}

// The unit's raid target mark at the time, as GetRaidTargetIndex gives one.
int __fastcall Script_GetUnitRaidTargetIndex(void *L) {
    const Unit *unit = UnitArg(L, "Usage: C_CombatLogReplay.GetUnitRaidTargetIndex(unitIndex, time)");
    const int mark = unit != nullptr ? MarkAt(*unit, TimeArg(L)) : 0;
    if (mark == 0)
        Lua::PushNil(L);
    else
        Lua::PushNumber(L, mark);
    return 1;
}

// The times the unit died in the stretch (its UNIT_DIED lines), for a viewer
// that marks them once rather than reading every event.
int __fastcall Script_GetUnitDeaths(void *L) {
    const Unit *unit = UnitArg(L, "Usage: C_CombatLogReplay.GetUnitDeaths(unitIndex)", false);
    Lua::NewTable(L);
    if (unit == nullptr)
        return 1;
    int i = 0;
    for (const Span &death : unit->deaths) {
        if (death.from < 0)
            continue;
        Lua::PushNumber(L, Seconds(death.from));
        Lua::RawSetI(L, -2, ++i);
    }
    return 1;
}

// Each body the player had, from the time it took it: a list the viewer
// reads once, not a lookup per frame. The items are what its model shows,
// as CGPlayer_C::ApplyEquipmentDisplay (0x005ED700) leaves a hidden helm or
// cloak off.
int __fastcall Script_GetUnitAppearances(void *L) {
    const Unit *unit = UnitArg(L, "Usage: C_CombatLogReplay.GetUnitAppearances(unitIndex)", false);
    Lua::NewTable(L);
    if (unit == nullptr)
        return 1;
    int i = 0;
    for (const auto &[t, a] : unit->appearances) {
        Lua::NewTable(L);
        Lua::SetFieldNumber(L, "time", Seconds(t));
        Lua::SetFieldNumber(L, "raceID", a.race);
        Lua::SetFieldNumber(L, "classID", a.classId);
        Lua::SetFieldNumber(L, "sex", a.sex);
        Lua::SetFieldNumber(L, "skin", a.skin);
        Lua::SetFieldNumber(L, "face", a.face);
        Lua::SetFieldNumber(L, "hairStyle", a.hairStyle);
        Lua::SetFieldNumber(L, "hairColor", a.hairColor);
        Lua::SetFieldNumber(L, "facialHair", a.facialHair);
        if (std::find(std::begin(a.emblem), std::end(a.emblem), -1) == std::end(a.emblem)) {
            Lua::SetFieldNumber(L, "emblemStyle", a.emblem[0]);
            Lua::SetFieldNumber(L, "emblemColor", a.emblem[1]);
            Lua::SetFieldNumber(L, "emblemBorderStyle", a.emblem[2]);
            Lua::SetFieldNumber(L, "emblemBorderColor", a.emblem[3]);
            Lua::SetFieldNumber(L, "emblemBackgroundColor", a.emblem[4]);
        }
        Model::Outfit::Worn worn;
        for (int slot = 0; slot < Model::Outfit::kInventorySlots; ++slot)
            worn[slot] = {a.items[slot], a.enchants[slot]};
        if (a.hideHelm)
            worn[Offsets::INVSLOT_HEAD - 1] = {};
        if (a.hideCloak)
            worn[Offsets::INVSLOT_BACK - 1] = {};
        Lua::PushString(L, "items");
        Model::TryOn::PushTransmogInfoList(L, worn);
        Lua::SetTable(L, -3);
        Lua::RawSetI(L, -2, ++i);
    }
    return 1;
}

// A game object's name: the recorder's, else the client's record for it.
std::string ObjectName(const GameObject &object) {
    if (!object.name.empty())
        return object.name;
    const uint8_t *record = object.objectID != 0
                                ? Cache::Record::Peek(Offsets::FUN_GAMEOBJECT_GET_RECORD,
                                                      Offsets::VAR_GAMEOBJECT_CACHE, object.objectID)
                                : nullptr;
    const char *name = record != nullptr ? Game::Read<const char *>(record, Offsets::OFF_GAMEOBJECT_NAME) : nullptr;
    return name != nullptr ? name : "";
}

int __fastcall Script_GetGameObjects(void *L) {
    Lua::NewTable(L);
    if (g_recording == nullptr)
        return 1;
    int i = 0;
    for (const GameObject &object : g_recording->objects) {
        Lua::NewTable(L);
        Lua::SetFieldString(L, "guid", object.guid.c_str());
        const std::string name = ObjectName(object);
        if (!name.empty())
            Lua::SetFieldString(L, "name", name.c_str());
        Lua::SetFieldNumber(L, "gameObjectID", object.objectID);
        Lua::RawSetI(L, -2, ++i);
    }
    return 1;
}

// Where a game object is, how it is turned, and what it shows at the time.
// The rotation is the quaternion the client turns the model by, as the
// angles of a ModelSceneActor: yaw about Z, then pitch about Y, then roll
// about X.
int __fastcall Script_GetGameObjectInfo(void *L) {
    constexpr char kUsage[] = "Usage: C_CombatLogReplay.GetGameObjectInfo(objectIndex, time)";
    if (!Lua::IsNumber(L, 1) || !Lua::IsNumber(L, 2)) {
        Lua::Error(L, "%s", kUsage);
        return 0;
    }
    const auto index = static_cast<int64_t>(Lua::ToNumber(L, 1));
    const ObjectLook *look = nullptr;
    if (g_recording != nullptr && index >= 1 && index <= static_cast<int64_t>(g_recording->objects.size()))
        look = ObjectLookAt(g_recording->objects[static_cast<std::size_t>(index - 1)], TimeArg(L));
    if (look == nullptr) {
        Lua::PushNil(L);
        return 1;
    }
    const double x = look->rotation[0], y = look->rotation[1], z = look->rotation[2], w = look->rotation[3];
    constexpr double kTwoPi = 6.283185307179586;
    double yaw = std::atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z));
    if (yaw < 0)
        yaw += kTwoPi;
    const double pitch = std::asin(std::clamp(2.0 * (w * y - z * x), -1.0, 1.0));
    const double roll = std::atan2(2.0 * (w * x + y * z), 1.0 - 2.0 * (x * x + y * y));
    Lua::PushNumber(L, look->x);
    Lua::PushNumber(L, look->y);
    Lua::PushNumber(L, look->z);
    Lua::PushNumber(L, yaw);
    Lua::PushNumber(L, pitch);
    Lua::PushNumber(L, roll);
    Lua::PushNumber(L, look->scale);
    Lua::PushNumber(L, look->display);
    Lua::PushNumber(L, look->type);
    Lua::PushNumber(L, look->state);
    Lua::PushNumber(L, look->flags);
    PushUnitIndex(L, look->owner);
    return 12;
}

int __fastcall Script_GetGroundAreas(void *L) {
    Lua::NewTable(L);
    if (g_recording == nullptr)
        return 1;
    int i = 0;
    for (const GroundArea &area : g_recording->areas) {
        Lua::NewTable(L);
        Lua::SetFieldNumber(L, "spellID", area.spellID);
        if (area.caster >= 0)
            Lua::SetFieldNumber(L, "sourceIndex", area.caster + 1);
        Lua::SetFieldNumber(L, "x", area.x);
        Lua::SetFieldNumber(L, "y", area.y);
        Lua::SetFieldNumber(L, "z", area.z);
        Lua::SetFieldNumber(L, "facing", area.facing);
        Lua::SetFieldNumber(L, "radius", area.radius);
        Lua::SetFieldNumber(L, "startTime", Seconds(area.span.from));
        if (area.span.to != INT32_MAX)
            Lua::SetFieldNumber(L, "endTime", Seconds(area.span.to));
        Lua::RawSetI(L, -2, ++i);
    }
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kLogInfoFields[] = {
    Req("name", "cstring", "The file name without .txt; the other functions take it."),
    Req("size", "number", "In bytes."),
    Req("modified", "time_t", "When it was last written."),
    Req("hasMovement", "bool", "Whether it has a movement file, which places the units."),
};
const Game::Doc::Structure kLogInfo{"CombatLogReplayLogInfo", "CombatLogReplay", kLogInfoFields};
const Game::Doc::Field kGetLogsRets[] = {
    Req("logs", "table", "An array of CombatLogReplayLogInfo, newest first."),
};
const Game::Doc::Function kGetLogs{"Returns the combat log files in the Logs folder.", {},
                                   kGetLogsRets, nullptr, true};

const Game::Doc::Field kEncounterFields[] = {
    Req("encounterID", "number"),
    Req("encounterName", "cstring"),
    Req("difficultyID", "number"),
    Req("groupSize", "number"),
    Req("mapID", "number"),
    Opt("success", "bool", nullptr, "Nil when the log has no end for it."),
    Req("startTime", "number", "Seconds from the log's start."),
    Req("endTime", "number", "Seconds from the log's start."),
};
const Game::Doc::Structure kEncounter{"CombatLogReplayEncounterInfo", "CombatLogReplay",
                                      kEncounterFields};
const Game::Doc::Field kLogArgs[] = {
    Req("logName", "cstring"),
};
const Game::Doc::Field kGetEncountersRets[] = {
    Opt("encounters", "table", nullptr,
        "An array of CombatLogReplayEncounterInfo; nil while the log is read, which "
        "COMBAT_LOG_REPLAY_ENCOUNTERS_READY ends."),
    Opt("duration", "number", nullptr, "The log's length in seconds."),
};
const Game::Doc::Function kGetEncounters{
    "Returns the encounters in a combat log, and starts reading it when it is not read yet.",
    kLogArgs, kGetEncountersRets, nullptr, true};

const Game::Doc::Field kLoadReplayArgs[] = {
    Req("logName", "cstring"),
    Req("startTime", "number", "Seconds from the log's start."),
    Req("endTime", "number", "Seconds from the log's start."),
};
const Game::Doc::Field kLoadReplayRets[] = {
    Req("started", "bool",
        "False when GetEncounters has not read the log yet or the times are reversed."),
};
const Game::Doc::Function kLoadReplay{
    "Starts loading a stretch of a combat log for replay; COMBAT_LOG_REPLAY_LOADED says when "
    "it is loaded.",
    kLoadReplayArgs, kLoadReplayRets, nullptr, true};

const Game::Doc::Field kReplayInfoFields[] = {
    Req("logName", "cstring"),
    Req("startTime", "number", "Seconds from the log's start."),
    Req("duration", "number", "In seconds."),
    Req("mapID", "number", "-1 when the log does not say."),
    Req("hasMovement", "bool"),
    Req("firstUnitTime", "number",
        "When the first unit is in view, in seconds: 0, or a moment later for a replay that begins with the log, "
        "whose movement file starts a frame after it."),
    Req("numUnits", "number"),
};
const Game::Doc::Structure kReplayInfo{"CombatLogReplayInfo", "CombatLogReplay",
                                       kReplayInfoFields};
const Game::Doc::Field kGetReplayInfoRets[] = {
    Opt("info", "CombatLogReplayInfo", nullptr, "Nil when no replay is loaded."),
};
const Game::Doc::Function kGetReplayInfo{"Returns the loaded replay.", {}, kGetReplayInfoRets,
                                         nullptr, true};

const Game::Doc::Field kUnitFields[] = {
    Req("guid", "WOWGUID", "As the combat log writes it."),
    Opt("name", "cstring", nullptr,
        "The combat log's, else the client's for the GUID (its player name or creature cache). Nil when neither has "
        "one: C_CreatureInfo.RequestLoadCreatureByID loads an uncached creature's."),
    Req("isPlayer", "bool"),
    Opt("creatureID", "number"),
};
const Game::Doc::Structure kUnit{"CombatLogReplayUnitInfo", "CombatLogReplay", kUnitFields};
const Game::Doc::Field kGetUnitsRets[] = {
    Req("units", "table",
        "An array of CombatLogReplayUnitInfo; a unit's index in it is its unitIndex."),
};
const Game::Doc::Function kGetUnits{"Returns the units in the loaded replay.", {}, kGetUnitsRets,
                                    nullptr, true};

const Game::Doc::Field kUnitTimeArgs[] = {
    Req("unitIndex", "number"),
    Req("time", "number", "Seconds from the replay's start."),
};
const Game::Doc::Field kGetUnitPositionRets[] = {
    Opt("x", "number", nullptr, "Nil while the unit is not in the recorder's view."),
    Opt("y", "number"),
    Opt("z", "number"),
    Opt("facing", "number", nullptr, "In radians."),
    Opt("speed", "number", nullptr, "In yards a second over the ground; 0 while it stands."),
    Opt("isFalling", "bool", nullptr, "In the air: jumping or falling."),
    Opt("isJumping", "bool", nullptr, "In the air from a jump, not a fall."),
};
const Game::Doc::Function kGetUnitPosition{"Returns where a replay unit is at a time.",
                                           kUnitTimeArgs, kGetUnitPositionRets, nullptr, true};

const Game::Doc::Field kGetUnitLookRets[] = {
    Opt("displayID", "number", nullptr, "Nil before the movement file describes the unit."),
    Opt("nativeDisplayID", "number"),
    Opt("mountDisplayID", "number", nullptr, "0 when it is not mounted."),
    Opt("scale", "number"),
    Opt("standState", "number"),
    Opt("form", "number", nullptr, "The shapeshift form, 0 for none."),
    Opt("stealthed", "bool"),
    Opt("sheathState", "number"),
    Opt("isDead", "bool", nullptr, "From the unit's death until it next casts or attacks, is resurrected, or leaves view."),
    Opt("mainHandDisplayID", "number", nullptr,
        "A creature's held items (ItemDisplayInfo ids), 0 for none; a player's are its items."),
    Opt("offHandDisplayID", "number"),
    Opt("rangedDisplayID", "number"),
};
const Game::Doc::Field kGetUnitHeldItemArgs[] = {
    Req("unitIndex", "number"),
    Req("time", "number", "Seconds from the replay's start."),
    Req("inventorySlot", "number", "16 main hand, 17 off hand, 18 ranged."),
};
const Game::Doc::Field kGetUnitHeldItemRets[] = {
    Opt("itemDisplayID", "number", nullptr, "Nil when the creature holds nothing there."),
    Opt("itemClass", "number"),
    Opt("itemSubclass", "number"),
    Opt("inventoryType", "number"),
    Opt("sheatheType", "number"),
};
const Game::Doc::Function kGetUnitHeldItem{
    "Returns what a replay creature holds in a slot at a time, as its unit's held lane gives it: the "
    "arguments ModelSceneActor:SetHeldItem takes after the slot.",
    kGetUnitHeldItemArgs, kGetUnitHeldItemRets, nullptr, true};
const Game::Doc::Field kAppearanceFields[] = {
    Req("time", "number", "Seconds from the replay's start; negative when it began before it."),
    Req("raceID", "number"),
    Req("classID", "number"),
    Req("sex", "number", "0 male, 1 female."),
    Req("skin", "number"),
    Req("face", "number"),
    Req("hairStyle", "number"),
    Req("hairColor", "number"),
    Req("facialHair", "number"),
    Opt("emblemStyle", "number", nullptr,
        "The player's guild's tabard emblem, as the recorder's client had it: all five or none."),
    Opt("emblemColor", "number"),
    Opt("emblemBorderStyle", "number"),
    Opt("emblemBorderColor", "number"),
    Opt("emblemBackgroundColor", "number"),
    Req("items", "table",
        "ItemTransmogInfo for inventory slots 1 to 19, what the model shows: a hidden helm or "
        "cloak has none."),
};
const Game::Doc::Structure kAppearance{
    "CombatLogReplayAppearance", "CombatLogReplay", kAppearanceFields,
    "A player's body and what it wears. It fits ModelSceneActor:SetModelByCharacterAppearance."};
const Game::Doc::Field kUnitIndexArg[] = {Req("unitIndex", "number")};
const Game::Doc::Field kGetUnitAppearancesRets[] = {
    Req("appearances", "table",
        "An array of CombatLogReplayAppearance in time order, each until the next; empty for a "
        "creature."),
};
const Game::Doc::Field kGetUnitDeathsRets[] = {
    Req("times", "table", "An array of seconds from the replay's start, in order: each UNIT_DIED of the unit."),
};
const Game::Doc::Function kGetUnitDeaths{"Returns when a replay unit died.", kUnitIndexArg, kGetUnitDeathsRets,
                                         nullptr, true};
const Game::Doc::Function kGetUnitAppearances{
    "Returns each body a replay player had and what it wore.", kUnitIndexArg,
    kGetUnitAppearancesRets, nullptr, true};

const Game::Doc::Field kEventFields[] = {
    Req("time", "number", "Seconds from the replay's start."),
    Req("event", "cstring", "The subevent as the log names it, for example SPELL_CAST_START or SWING_DAMAGE."),
    Opt("sourceIndex", "number", nullptr, "Nil when the source is not a unit."),
    Opt("destIndex", "number", nullptr, "Nil when the destination is not a unit."),
    Opt("spellID", "number", nullptr, "For a SPELL_, RANGE_ or DAMAGE_ event."),
    Opt("auraType", "cstring", nullptr, "BUFF or DEBUFF, for a SPELL_AURA_ event."),
    Opt("castTime", "number", nullptr,
        "For a SPELL_CAST_START: seconds until the caster's SPELL_CAST_SUCCESS or SPELL_CAST_FAILED of that "
        "spell, a SPELL_INTERRUPT of it, or the caster's death, else until the movement file shows the cast "
        "stopped. Nil when the logs say none of these."),
    Opt("castFailed", "bool", nullptr,
        "For a SPELL_CAST_START with a castTime: whether the cast failed or was interrupted."),
    Opt("amount", "number", nullptr, "For a _DAMAGE or _HEAL event, DAMAGE_SHIELD and DAMAGE_SPLIT."),
    Opt("critical", "bool", nullptr, "For a _DAMAGE or _HEAL event."),
    Opt("missType", "cstring", nullptr, "For a _MISSED event: MISS, DODGE, PARRY, BLOCK, RESIST, ABSORB, IMMUNE, ..."),
    Opt("offHand", "bool", nullptr, "True for a _MISSED event that the off hand made. The log does not say this for a hit."),
    Opt("endsCast", "bool", nullptr, "True for a SPELL_CAST_SUCCESS or SPELL_CAST_FAILED that ends a SPELL_CAST_START."),
    Opt("spellName", "cstring", nullptr, "The spell's name as the log writes it, with spellID."),
    Opt("extraSpellID", "number", nullptr, "For a SPELL_INTERRUPT: the spell it interrupted."),
    Opt("extraSpellName", "cstring", nullptr, "Its name as the log writes it, with extraSpellID."),
    Opt("castTargetIndex", "number", nullptr,
        "For a SPELL_CAST_START: the destination of the SPELL_CAST_SUCCESS that ended it. Nil when it did not "
        "go off or had no unit as its destination."),
    Opt("text", "cstring", nullptr, "For an EMOTE: the raid boss emote's text, as the chat frame showed it."),
};
const Game::Doc::Structure kEvent{"CombatLogReplayEvent", "CombatLogReplay", kEventFields};
const Game::Doc::Field kTimeRangeArgs[] = {
    Req("startTime", "number", "Seconds from the replay's start; events at it are included."),
    Req("endTime", "number", "Events at it are not included."),
};
const Game::Doc::Field kGetEventsRets[] = {
    Req("events", "table", "An array of CombatLogReplayEvent in time order."),
};
const Game::Doc::Function kGetEvents{"Returns the combat log events of the loaded replay between two times.",
                                     kTimeRangeArgs, kGetEventsRets, nullptr, true};
const Game::Doc::Field kGetUnitWeaponsRets[] = {
    Opt("mainHandClassID", "number", nullptr,
        "Nil when the hand holds nothing, or for a player's item not yet in the item cache (asked for)."),
    Opt("mainHandSubclassID", "number"),
    Opt("offHandClassID", "number"),
    Opt("offHandSubclassID", "number"),
};
const Game::Doc::Function kGetUnitWeapons{
    "Returns the item class and subclass a replay unit holds in each hand at a time: a player's main and "
    "off hand items, a creature's held items.",
    kUnitTimeArgs, kGetUnitWeaponsRets, nullptr, true};
const Game::Doc::Field kGetUnitHealthRets[] = {
    Opt("health", "number", nullptr,
        "Nil before the log first gives it. A player's as a percent of 100, a creature's in hit points, as the "
        "log's advanced data gives them; 0 from the unit's death."),
    Opt("maxHealth", "number"),
};
const Game::Doc::Function kGetUnitHealth{
    "Returns a replay unit's health at a time: the movement file's when it has the unit's, else the log's advanced "
    "data (the advancedCombatLogging setting).",
    kUnitTimeArgs, kGetUnitHealthRets, nullptr, true};
const Game::Doc::Field kAuraFields[] = {
    Req("spellID", "number"),
    Req("auraType", "cstring", "BUFF or DEBUFF."),
    Opt("sourceIndex", "number", nullptr, "Nil when the log does not name a unit that applied it."),
    Req("applied", "number", "When it was applied, in seconds from the replay's start; negative before it."),
    Opt("applications", "number", nullptr, "Its stacks, from the movement file or a dose event."),
    Opt("isHidden", "bool", nullptr,
        "True for an aura that UnitBuff and UnitDebuff do not list, as the client hides it. Its visual still shows."),
    Opt("expirationTime", "number", nullptr,
        "When it ended in the log, in seconds from the replay's start: it expired, or was removed early. Nil when "
        "it did not end in the loaded stretch while the unit was in view."),
    Opt("duration", "number", nullptr,
        "Seconds from its last application, refresh or added stack to expirationTime. Nil when either is not "
        "known."),
};
const Game::Doc::Structure kAura{"CombatLogReplayAura", "CombatLogReplay", kAuraFields,
                                 "All of a unit's auras with the movement file, else only the ones the log saw applied."};
const Game::Doc::Field kGetUnitAurasRets[] = {
    Req("auras", "table", "An array of CombatLogReplayAura."),
};
const Game::Doc::Function kGetUnitAuras{"Returns the auras on a replay unit at a time.", kUnitTimeArgs,
                                        kGetUnitAurasRets, nullptr, true};

const Game::Doc::Field kGetUnitCastRets[] = {
    Opt("spellID", "number", nullptr, "Nil when the unit began no cast by the time."),
    Opt("startTime", "number", nullptr, "When the cast or the channel began."),
    Opt("endTime", "number", nullptr,
        "When it was due to end: a cast's cast time from its start, a channel's duration from its start."),
    Opt("stopTime", "number", nullptr,
        "When it stopped: at its end, or before it when it was interrupted or the channel was stopped. Nil "
        "when the log does not say."),
    Opt("isChannel", "bool"),
    Opt("interrupted", "bool", nullptr, "True for a cast that stopped before its end and did not go off."),
    Opt("interrupterIndex", "number", nullptr, "The unit whose interrupt stopped the cast: its SPELL_INTERRUPT, or the success of its interrupt spell on the caster as the cast stopped. Nil for none."),
};
const Game::Doc::Function kGetUnitCast{
    "Returns the cast or channel a replay unit began last by a time. Interrupted casts and the end of a "
    "channel come from the movement file.",
    kUnitTimeArgs, kGetUnitCastRets, nullptr, true};

const Game::Doc::Field kGetUnitRaidTargetIndexRets[] = {
    Opt("index", "number", nullptr,
        "1 star, 2 circle, 3 diamond, 4 triangle, 5 moon, 6 square, 7 cross, 8 skull; nil for none."),
};
const Game::Doc::Function kGetUnitRaidTargetIndex{
    "Returns the raid target mark a replay unit wore at a time, from the raid flags of the log's lines. The log "
    "has marks only while its recorder was in a group, and a change shows from the unit's next line.",
    kUnitTimeArgs, kGetUnitRaidTargetIndexRets, nullptr, true};

const Game::Doc::Field kGetUnitPowerRets[] = {
    Opt("powerType", "number", nullptr, "Nil when the movement file has no power for the unit at the time."),
    Opt("power", "number", nullptr, "In stored units: rage is in tenths."),
    Opt("maxPower", "number"),
};
const Game::Doc::Function kGetUnitPower{"Returns a replay unit's power at a time, from the movement file.",
                                        kUnitTimeArgs, kGetUnitPowerRets, nullptr, true};
const Game::Doc::Field kGetUnitStateRets[] = {
    Opt("targetIndex", "number", nullptr, "The unit it targets; nil for none, or when the movement file has no state for it."),
    Opt("ownerIndex", "number", nullptr, "Its charmer or creator: a pet's, guardian's or totem's owner."),
    Opt("flags", "number", nullptr, "UNIT_FIELD_FLAGS: stunned 0x40000, in combat 0x80000, confused 0x400000, fleeing 0x800000."),
    Opt("level", "number"),
    Opt("factionTemplate", "number", nullptr, "Its FactionTemplate.dbc id."),
    Opt("attackTargetIndex", "number", nullptr, "The unit it swings at with melee attacks; nil for none."),
    Opt("reaction", "number", nullptr,
        "Its reaction toward the player who recorded the log, as UnitReaction gives it: 1 hated to 8 exalted."),
    Opt("isTapDenied", "bool", nullptr, "Another player tapped it, as UnitIsTapDenied."),
};
const Game::Doc::Function kGetUnitState{
    "Returns whom a replay unit targets, who owns it, its unit flags, level, faction and reaction at a time, from the "
    "movement file.",
    kUnitTimeArgs, kGetUnitStateRets, nullptr, true};
const Game::Doc::Field kColorRets[] = {
    Opt("red", "number"),
    Opt("green", "number"),
    Opt("blue", "number"),
    Opt("alpha", "number"),
};
const Game::Doc::Function kGetUnitSelectionColor{
    "Returns a replay unit's selection color at a time, as UnitSelectionColor gave it to the player who recorded the "
    "log; nil when the movement file has no state for it.",
    kUnitTimeArgs, kColorRets, nullptr, true};

const Game::Doc::Function kGetUnitLook{"Returns what a replay unit looks like at a time.",
                                       kUnitTimeArgs, kGetUnitLookRets, nullptr, true};

const Game::Doc::Field kGameObjectFields[] = {
    Req("guid", "WOWGUID", "As the combat log writes it."),
    Opt("name", "cstring", nullptr,
        "The recorder's name for it, else the client's game object cache. Nil when neither has one."),
    Req("gameObjectID", "number"),
};
const Game::Doc::Structure kGameObject{"CombatLogReplayGameObjectInfo", "CombatLogReplay", kGameObjectFields};
const Game::Doc::Field kGetGameObjectsRets[] = {
    Req("gameObjects", "table",
        "An array of CombatLogReplayGameObjectInfo; an object's index in it is its objectIndex. Empty without a "
        "movement file."),
};
const Game::Doc::Function kGetGameObjects{
    "Returns the game objects in view during the loaded replay: doors, chests, traps, portals and more.", {},
    kGetGameObjectsRets, nullptr, true};
const Game::Doc::Field kObjectTimeArgs[] = {
    Req("objectIndex", "number"),
    Req("time", "number", "Seconds from the replay's start."),
};
const Game::Doc::Field kGetGameObjectInfoRets[] = {
    Opt("x", "number", nullptr, "Nil while the object is not in the recorder's view."),
    Opt("y", "number"),
    Opt("z", "number"),
    Opt("yaw", "number", nullptr,
        "How the model is turned, in radians, as ModelSceneActor:SetYaw, SetPitch and SetRoll take it."),
    Opt("pitch", "number"),
    Opt("roll", "number"),
    Opt("scale", "number"),
    Opt("displayID", "number", nullptr, "Its GameObjectDisplayInfo.dbc id."),
    Opt("gameObjectType", "number", nullptr,
        "0 door, 3 chest, 6 trap, 8 spell focus, 18 summoning ritual, and more."),
    Opt("state", "number", nullptr, "0 active (a door open), 1 ready (a door closed), 2 alternative."),
    Opt("flags", "number"),
    Opt("ownerIndex", "number", nullptr, "The unit that made it, such as a hunter's trap; nil for none."),
};
const Game::Doc::Function kGetGameObjectInfo{
    "Returns where a replay game object is, how it is turned, and what it shows at a time.", kObjectTimeArgs,
    kGetGameObjectInfoRets, nullptr, true};
const Game::Doc::Field kGroundAreaFields[] = {
    Req("spellID", "number"),
    Opt("sourceIndex", "number", nullptr, "The unit that cast it; nil when it is not a unit."),
    Req("x", "number"),
    Req("y", "number"),
    Req("z", "number"),
    Req("facing", "number", "In radians."),
    Req("radius", "number", "In yards."),
    Req("startTime", "number",
        "When it came into view, in seconds from the replay's start; negative before it."),
    Opt("endTime", "number", nullptr, "When it left view; nil when it stayed past the replay's end."),
};
const Game::Doc::Structure kGroundArea{
    "CombatLogReplayGroundArea", "CombatLogReplay", kGroundAreaFields,
    "The area that a spell such as Blizzard or Consecration leaves on the ground. "
    "ModelSceneActor:SetModelBySpellArea shows it."};
const Game::Doc::Field kGetGroundAreasRets[] = {
    Req("areas", "table", "An array of CombatLogReplayGroundArea in the order they came into view."),
};
const Game::Doc::Function kGetGroundAreas{"Returns the spells' ground areas in view during the loaded replay.",
                                          {}, kGetGroundAreasRets, nullptr, true};

void RegisterLuaFunctions() {
    constexpr char kTable[] = "C_CombatLogReplay";
    Lua::RegisterTableFunction(kTable, "GetLogs", &Script_GetLogs, &kGetLogs);
    Lua::RegisterTableFunction(kTable, "GetEncounters", &Script_GetEncounters, &kGetEncounters);
    Lua::RegisterTableFunction(kTable, "LoadReplay", &Script_LoadReplay, &kLoadReplay);
    Lua::RegisterTableFunction(kTable, "GetReplayInfo", &Script_GetReplayInfo, &kGetReplayInfo);
    Lua::RegisterTableFunction(kTable, "GetUnits", &Script_GetUnits, &kGetUnits);
    Lua::RegisterTableFunction(kTable, "GetUnitPosition", &Script_GetUnitPosition,
                               &kGetUnitPosition);
    Lua::RegisterTableFunction(kTable, "GetUnitLook", &Script_GetUnitLook, &kGetUnitLook);
    Lua::RegisterTableFunction(kTable, "GetUnitHeldItem", &Script_GetUnitHeldItem, &kGetUnitHeldItem);
    Lua::RegisterTableFunction(kTable, "GetUnitAppearances", &Script_GetUnitAppearances,
                               &kGetUnitAppearances);
    Lua::RegisterTableFunction(kTable, "GetEvents", &Script_GetEvents, &kGetEvents);
    Lua::RegisterTableFunction(kTable, "GetUnitAuras", &Script_GetUnitAuras, &kGetUnitAuras);
    Lua::RegisterTableFunction(kTable, "GetUnitDeaths", &Script_GetUnitDeaths, &kGetUnitDeaths);
    Lua::RegisterTableFunction(kTable, "GetUnitCast", &Script_GetUnitCast, &kGetUnitCast);
    Lua::RegisterTableFunction(kTable, "GetUnitRaidTargetIndex", &Script_GetUnitRaidTargetIndex,
                               &kGetUnitRaidTargetIndex);
    Lua::RegisterTableFunction(kTable, "GetUnitHealth", &Script_GetUnitHealth, &kGetUnitHealth);
    Lua::RegisterTableFunction(kTable, "GetUnitPower", &Script_GetUnitPower, &kGetUnitPower);
    Lua::RegisterTableFunction(kTable, "GetUnitState", &Script_GetUnitState, &kGetUnitState);
    Lua::RegisterTableFunction(kTable, "GetUnitSelectionColor", &Script_GetUnitSelectionColor,
                               &kGetUnitSelectionColor);
    Lua::RegisterTableFunction(kTable, "GetUnitWeapons", &Script_GetUnitWeapons, &kGetUnitWeapons);
    Lua::RegisterTableFunction(kTable, "GetGameObjects", &Script_GetGameObjects, &kGetGameObjects);
    Lua::RegisterTableFunction(kTable, "GetGameObjectInfo", &Script_GetGameObjectInfo,
                               &kGetGameObjectInfo);
    Lua::RegisterTableFunction(kTable, "GetGroundAreas", &Script_GetGroundAreas, &kGetGroundAreas);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Replay::Api
