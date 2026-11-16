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
#include "creature/Info.h"
#include "event/Custom.h"
#include "item/Data.h"
#include "item/Record.h"
#include "model/Outfit.h"
#include "model/TryOn.h"
#include "player/Info.h"
#include "replay/Catalog.h"
#include "replay/Recording.h"
#include "tick/WorldTick.h"

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
    return 9;
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
        if (e.spellID != 0)
            Lua::SetFieldNumber(L, "spellID", e.spellID);
        if (e.type.rfind("SPELL_AURA_", 0) == 0)
            Lua::SetFieldString(L, "auraType", e.debuff ? "DEBUFF" : "BUFF");
        if (e.castEnd >= 0) {
            Lua::SetFieldNumber(L, "castTime", Seconds(e.castEnd - e.t));
            Lua::SetFieldBool(L, "castFailed", e.castFailed);
        }
        if (e.endsCast)
            Lua::SetFieldBool(L, "endsCast", true);
        const auto endsWith = [&](const char *end, size_t n) {
            return e.type.size() >= n && e.type.compare(e.type.size() - n, n, end) == 0;
        };
        if (endsWith("_DAMAGE", 7) || endsWith("_HEAL", 5)) {
            Lua::SetFieldNumber(L, "amount", e.amount);
            Lua::SetFieldBool(L, "critical", e.critical);
        }
        if (!e.missType.empty())
            Lua::SetFieldString(L, "missType", e.missType.c_str());
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
            held[hand] = look->heldInfo[hand] != 0;
            classes[hand] = look->heldInfo[hand] & 0xFF;
            subclasses[hand] = (look->heldInfo[hand] >> 8) & 0xFF;
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

// The unit's health at the time, as the log gives it.
int __fastcall Script_GetUnitHealth(void *L) {
    const Unit *unit = UnitArg(L, "Usage: C_CombatLogReplay.GetUnitHealth(unitIndex, time)");
    const Health *health = unit != nullptr ? HealthAt(*unit, TimeArg(L)) : nullptr;
    if (health == nullptr || health->max <= 0) {
        Lua::PushNil(L);
        return 1;
    }
    Lua::PushNumber(L, health->current);
    Lua::PushNumber(L, health->max);
    return 2;
}

// The auras on the unit at the time.
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
        if (a.caster >= 0)
            Lua::SetFieldNumber(L, "sourceIndex", a.caster + 1);
        Lua::SetFieldNumber(L, "applied", Seconds(a.from));
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
};
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
        "spell. Nil when the log has neither."),
    Opt("castFailed", "bool", nullptr, "For a SPELL_CAST_START with a castTime: whether the cast failed."),
    Opt("amount", "number", nullptr, "For a _DAMAGE or _HEAL event."),
    Opt("critical", "bool", nullptr, "For a _DAMAGE or _HEAL event."),
    Opt("missType", "cstring", nullptr, "For a _MISSED event: MISS, DODGE, PARRY, BLOCK, RESIST, ABSORB, IMMUNE, ..."),
    Opt("endsCast", "bool", nullptr, "True for a SPELL_CAST_SUCCESS or SPELL_CAST_FAILED that ends a SPELL_CAST_START."),
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
    "Returns a replay unit's health at a time, from the log's advanced data (the advancedCombatLogging setting).",
    kUnitTimeArgs, kGetUnitHealthRets, nullptr, true};
const Game::Doc::Field kAuraFields[] = {
    Req("spellID", "number"),
    Req("auraType", "cstring", "BUFF or DEBUFF."),
    Opt("sourceIndex", "number", nullptr, "Nil when the log does not name a unit that applied it."),
    Req("applied", "number", "When it was applied, in seconds from the replay's start; negative before it."),
};
const Game::Doc::Structure kAura{"CombatLogReplayAura", "CombatLogReplay", kAuraFields,
                                 "Only auras the log saw applied are known."};
const Game::Doc::Field kGetUnitAurasRets[] = {
    Req("auras", "table", "An array of CombatLogReplayAura."),
};
const Game::Doc::Function kGetUnitAuras{"Returns the auras on a replay unit at a time.", kUnitTimeArgs,
                                        kGetUnitAurasRets, nullptr, true};

const Game::Doc::Function kGetUnitLook{"Returns what a replay unit looks like at a time.",
                                       kUnitTimeArgs, kGetUnitLookRets, nullptr, true};

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
    Lua::RegisterTableFunction(kTable, "GetUnitAppearances", &Script_GetUnitAppearances,
                               &kGetUnitAppearances);
    Lua::RegisterTableFunction(kTable, "GetEvents", &Script_GetEvents, &kGetEvents);
    Lua::RegisterTableFunction(kTable, "GetUnitAuras", &Script_GetUnitAuras, &kGetUnitAuras);
    Lua::RegisterTableFunction(kTable, "GetUnitHealth", &Script_GetUnitHealth, &kGetUnitHealth);
    Lua::RegisterTableFunction(kTable, "GetUnitWeapons", &Script_GetUnitWeapons, &kGetUnitWeapons);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Replay::Api
