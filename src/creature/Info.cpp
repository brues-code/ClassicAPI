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

// `C_CreatureInfo.GetCreatureInfoByID(creatureID)` -> table | nil
//
// Reads the client-side creature cache (creaturecache.wdb, fed by
// SMSG_CREATURE_QUERY_RESPONSE) for a creature/NPC by ID and returns:
//
//   {
//     creatureID = <id>,
//     name       = "Misthoof Stag",   -- primary name
//     subName    = "",                 -- title/subtitle ("" if none)
//     type       = 1,                  -- CreatureType (1=Beast, 7=Humanoid, …)
//     family     = 0,                  -- CreatureFamily
//     rank       = 0,                  -- 0=normal,1=elite,2=rareelite,3=worldboss,4=rare
//     displayID  = 10957,              -- model display ID
//   }
//
// `GetCreatureInfoByID` is a synchronous *peek*: returns the data only
// if the creature is already cached (seen this session, or loaded from
// creaturecache.wdb at login); nil otherwise. `RequestLoadCreatureByID`
// fetches an uncached creature — it issues the query and fires
// `CREATURE_DATA_LOAD_RESULT(creatureID, success)` when the response
// lands, via the shared `Cache::QueryLoad` dispatcher.
//
// Field offsets verified against the binary (rank read by the engine's
// classification helper as `[block+0x20]`) and real creaturecache.wdb
// rows. See Offsets.h `OFF_CREATURE_*`.
//
// This file also hosts the two static-DBC lookups on the same table:
//
//   C_CreatureInfo.GetRaceInfo(raceID)   -> { raceName, clientFileString, raceID }
//   C_CreatureInfo.GetClassInfo(classID) -> { className, classFile, classID }
//
// retail backports over `ChrRaces.dbc` / `ChrClasses.dbc`. Unlike the
// creature cache these DBCs are always loaded, localized, and synchronous —
// so a by-id lookup works for any race/class id at any time, with no WDB
// cold-cache problem. The file-string columns (`clientFileString` /
// `classFile`) are the same non-localized tokens `UnitRace`/`UnitClass`
// return as their 2nd value ("NightElf", "WARRIOR"), so they round-trip
// with the unit APIs. See Offsets.h `OFF_CHRRACES_*` / `OFF_CHRCLASSES_*`.

#include "Game.h"
#include "Offsets.h"
#include "cache/QueryLoad.h"
#include "creature/Info.h"
#include "dbc/Lookup.h"
#include "dbc/Names.h"
#include "event/Custom.h"

#include <cstdint>

namespace Creature::Info {

namespace {

// Completion event for RequestLoadCreatureByID. Reserved at static-init
// so the engine allocates it in its event table; fired (creatureID,
// success) by the Cache::QueryLoad dispatcher.
constexpr const char *kCreatureDataLoadResult = "CREATURE_DATA_LOAD_RESULT";
const Game::Doc::Field kLoadResultPayload[] = {
    Game::Doc::Req("creatureID", "number"),
    Game::Doc::Opt("success", "number", nullptr,
                   "1 when the data loaded, nil when the server did not send it."),
};
const Game::Doc::Event kLoadResultDoc{
    "CreatureInfo",
    "Fires when a creature asked for with C_CreatureInfo.RequestLoadCreatureByID loads or "
    "fails to load.",
    kLoadResultPayload};
const Event::Custom::AutoReserve _reserveLoadResult{kCreatureDataLoadResult, &kLoadResultDoc};

// Generic cache `_GetRecord`. With callback=nullptr it only peeks the
// hash table (no network query) and returns the data block (entry+0x18)
// if the creature is loaded, else nullptr.
using GetCacheRecord_t = const uint8_t *(__thiscall *)(void *cache, uint32_t id,
                                                       const uint64_t *guid, void *callback,
                                                       void *userData, char dedup);

const uint8_t *PeekCreature(uint32_t creatureID) {
    auto fn = reinterpret_cast<GetCacheRecord_t>(Offsets::FUN_CREATURE_GET_RECORD);
    auto *cache = reinterpret_cast<void *>(Offsets::VAR_CREATURE_CACHE);
    const uint64_t zeroGuid = 0;
    return fn(cache, creatureID, &zeroGuid, nullptr, nullptr, 0);
}

int __fastcall Script_GetCreatureInfoByID(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0; // nil
    const int creatureID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (creatureID <= 0)
        return 0;

    const uint8_t *rec = PeekCreature(static_cast<uint32_t>(creatureID));
    if (rec == nullptr)
        return 0; // not cached -> nil

    const char *name = Game::Read<const char *>(rec, Offsets::OFF_CREATURE_NAME);
    const char *subName =
        Game::Read<const char *>(rec, Offsets::OFF_CREATURE_SUBNAME);
    const uint32_t type = Game::Read<uint32_t>(rec, Offsets::OFF_CREATURE_TYPE);
    const uint32_t family = Game::Read<uint32_t>(rec, Offsets::OFF_CREATURE_FAMILY);
    const uint32_t rank = Game::Read<uint32_t>(rec, Offsets::OFF_CREATURE_RANK);
    const uint32_t displayID =
        Game::Read<uint32_t>(rec, Offsets::OFF_CREATURE_DISPLAYID);

    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "creatureID", creatureID);
    Game::Lua::SetFieldString(L, "name", name);       // NULL coerced to ""
    Game::Lua::SetFieldString(L, "subName", subName);
    Game::Lua::SetFieldNumber(L, "type", type);
    Game::Lua::SetFieldNumber(L, "family", family);
    Game::Lua::SetFieldNumber(L, "rank", rank);
    Game::Lua::SetFieldNumber(L, "displayID", displayID);
    return 1;
}

// `C_CreatureInfo.RequestLoadCreatureByID(creatureID)` -> bool. Async
// fetch for an uncached creature: issues SMSG_CREATURE_QUERY and fires
// `CREATURE_DATA_LOAD_RESULT(creatureID, success)` when the response
// lands (or on timeout). Fires success synchronously if already cached.
// Returns true if the request was accepted (false for bad input or a
// full pending set) — mirrors C_Item.RequestLoadItemData(ByID).
int __fastcall Script_RequestLoadCreatureByID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const int creatureID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (creatureID <= 0) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    Game::Lua::PushBool(L, RequestLoad(static_cast<uint32_t>(creatureID)));
    return 1;
}

// `C_CreatureInfo.GetRaceInfo(raceID)` — RaceInfo table, or nil for a
// non-numeric / non-positive id or one with no ChrRaces row.
int __fastcall Script_GetRaceInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0; // nil
    const int raceID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const char *file =
        (raceID > 0) ? DBC::RaceToken(static_cast<uint32_t>(raceID)) : nullptr;
    if (file == nullptr)
        return 0; // unknown id -> nil
    const char *name = DBC::RaceName(static_cast<uint32_t>(raceID));

    Game::Lua::NewTable(L);
    Game::Lua::SetFieldString(L, "raceName", name); // NULL coerced to ""
    Game::Lua::SetFieldString(L, "clientFileString", file);
    Game::Lua::SetFieldNumber(L, "raceID", raceID);
    return 1;
}

// `C_CreatureInfo.GetClassInfo(classID)` — ClassInfo table, or nil for a
// non-numeric / non-positive id or one with no ChrClasses row.
int __fastcall Script_GetClassInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0; // nil
    const int classID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const char *file =
        (classID > 0) ? DBC::ClassToken(static_cast<uint32_t>(classID)) : nullptr;
    if (file == nullptr)
        return 0; // unknown id -> nil
    const char *name = DBC::ClassName(static_cast<uint32_t>(classID));

    Game::Lua::NewTable(L);
    Game::Lua::SetFieldString(L, "className", name); // NULL coerced to ""
    Game::Lua::SetFieldString(L, "classFile", file);
    Game::Lua::SetFieldNumber(L, "classID", classID);
    return 1;
}

// `C_CreatureInfo.GetCreatureFamilyInfo(creatureFamilyID)` — CreatureFamilyInfo
// table for a `CreatureFamily.dbc` row (1 = Wolf, 27 = Wind Serpent, …), or
// nil for a non-numeric / non-positive / unused id. `iconFile` is a texture
// path string here (retail exposes a numeric fileID; vanilla's DBC stores the
// path) — empty "" for families with no icon (e.g. warlock pets).
int __fastcall Script_GetCreatureFamilyInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0; // nil
    const int familyID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const char *name =
        (familyID > 0) ? DBC::LocalizedField(Offsets::VAR_CREATUREFAMILY_RECORDS,
                                             Offsets::VAR_CREATUREFAMILY_COUNT,
                                             static_cast<uint32_t>(familyID),
                                             Offsets::OFF_CREATUREFAMILY_NAMES)
                       : nullptr;
    if (name == nullptr)
        return 0; // unused / OOR id -> nil
    const char *icon = DBC::StringField(Offsets::VAR_CREATUREFAMILY_RECORDS,
                                        Offsets::VAR_CREATUREFAMILY_COUNT,
                                        static_cast<uint32_t>(familyID),
                                        Offsets::OFF_CREATUREFAMILY_ICON);

    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "id", familyID);
    Game::Lua::SetFieldString(L, "name", name);
    Game::Lua::SetFieldString(L, "iconFile", icon); // NULL/empty coerced to ""
    return 1;
}

// `C_CreatureInfo.GetCreatureFamilyIDs()` — array of every populated
// `CreatureFamily.dbc` id (sparse: skips the unused rows). Round-trips with
// GetCreatureFamilyInfo for each element.
int __fastcall Script_GetCreatureFamilyIDs(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    const int count =
        Game::Read<int>(Offsets::VAR_CREATUREFAMILY_COUNT);
    int n = 0;
    for (int id = 1; id <= count; ++id) {
        const char *name = DBC::LocalizedField(Offsets::VAR_CREATUREFAMILY_RECORDS,
                                               Offsets::VAR_CREATUREFAMILY_COUNT,
                                               static_cast<uint32_t>(id),
                                               Offsets::OFF_CREATUREFAMILY_NAMES);
        if (name == nullptr)
            continue; // unused row
        ++n;
        Game::Lua::PushNumber(L, static_cast<double>(n));
        Game::Lua::PushNumber(L, static_cast<double>(id));
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

// `C_CreatureInfo.GetFactionInfo(raceID)` — faction-group info for a race:
// { name = localized ("Alliance"/"Horde"/…), groupTag = english }. Mirrors
// the player branch of `Script_UnitFactionGroup` (`0x00516630`): resolve the
// race's FactionTemplate group mask, then scan `FactionGroup.dbc` for the row
// whose bit the mask sets and whose localized name is non-empty (which is what
// skips the always-set "Player" / the nameless "Monster" rows and lands on
// Alliance / Horde). nil for a non-numeric / non-positive / unknown race or a
// race with no named group. See Offsets.h for the full chain.
int __fastcall Script_GetFactionInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0; // nil
    const int raceID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const char *english = nullptr;
    const char *localized = nullptr;
    if (!FactionGroupForRace(raceID, &english, &localized))
        return 0; // unknown race or no named group -> nil

    Game::Lua::NewTable(L);
    Game::Lua::SetFieldString(L, "name", localized);
    Game::Lua::SetFieldString(L, "groupTag", english);
    return 1;
}

// `C_CreatureInfo.GetCreatureTypeInfo(creatureTypeID)` — CreatureTypeInfo
// table for a `CreatureType.dbc` row (1 = Beast … 11 = Totem), or nil for a
// non-numeric / non-positive / unused id.
int __fastcall Script_GetCreatureTypeInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0; // nil
    const int typeID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const char *name =
        (typeID > 0) ? DBC::LocalizedField(Offsets::VAR_CREATURETYPE_RECORDS,
                                           Offsets::VAR_CREATURETYPE_COUNT,
                                           static_cast<uint32_t>(typeID),
                                           Offsets::OFF_CREATURETYPE_NAMES)
                     : nullptr;
    if (name == nullptr)
        return 0; // unused / OOR id -> nil

    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "id", typeID);
    Game::Lua::SetFieldString(L, "name", name);
    return 1;
}

// `C_CreatureInfo.GetCreatureTypeIDs()` — array of every populated
// `CreatureType.dbc` id (contiguous 1..N in vanilla). Round-trips with
// GetCreatureTypeInfo for each element.
int __fastcall Script_GetCreatureTypeIDs(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    const int count =
        Game::Read<int>(Offsets::VAR_CREATURETYPE_COUNT);
    int n = 0;
    for (int id = 1; id <= count; ++id) {
        const char *name = DBC::LocalizedField(Offsets::VAR_CREATURETYPE_RECORDS,
                                               Offsets::VAR_CREATURETYPE_COUNT,
                                               static_cast<uint32_t>(id),
                                               Offsets::OFF_CREATURETYPE_NAMES);
        if (name == nullptr)
            continue;
        ++n;
        Game::Lua::PushNumber(L, static_cast<double>(n));
        Game::Lua::PushNumber(L, static_cast<double>(id));
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kCreatureIDArgs[] = {
    Game::Doc::Req("creatureID", "number"),
};

const Game::Doc::Field kCreatureDataFields[] = {
    Game::Doc::Req("creatureID", "number"),
    Game::Doc::Req("name", "string"),
    Game::Doc::Req("subName", "string", "The title under the name; \"\" when there is none."),
    Game::Doc::Req("type", "number", "A creature type ID, such as 1 for Beast."),
    Game::Doc::Req("family", "number", "A creature family ID; 0 for none."),
    Game::Doc::Req("rank", "number", "0 normal, 1 elite, 2 rare elite, 3 world boss, 4 rare."),
    Game::Doc::Req("displayID", "number"),
};
const Game::Doc::Structure kCreatureData{"CreatureData", "CreatureInfo", kCreatureDataFields,
                                         "A creature as the creature cache holds it."};
const Game::Doc::Field kCreatureInfoRets[] = {
    Game::Doc::Opt("creatureInfo", "CreatureData", nullptr,
                   "Nil when the creature is not cached."),
};
const Game::Doc::Function kGetCreatureInfoByID{
    "Returns a cached creature's name, type, family, rank and model; it never asks the server.",
    kCreatureIDArgs, kCreatureInfoRets, nullptr, true};

const Game::Doc::Field kRequestRets[] = {
    Game::Doc::Req("accepted", "bool", "False for bad input or too many pending requests."),
};
const Game::Doc::Function kRequestLoadCreatureByID{
    "Asks the server for a creature that is not cached; CREATURE_DATA_LOAD_RESULT fires with "
    "the answer.",
    kCreatureIDArgs, kRequestRets, nullptr, true};

const Game::Doc::Field kRaceIDArgs[] = {
    Game::Doc::Req("raceID", "number"),
};
const Game::Doc::Field kRaceInfoFields[] = {
    Game::Doc::Req("raceName", "string"),
    Game::Doc::Req("clientFileString", "string", "The token UnitRace returns second."),
    Game::Doc::Req("raceID", "number"),
};
const Game::Doc::Structure kRaceInfo{"RaceInfo", "CreatureInfo", kRaceInfoFields};
const Game::Doc::Field kRaceInfoRets[] = {
    Game::Doc::Opt("raceInfo", "RaceInfo", nullptr, "Nil for an unknown race ID."),
};
const Game::Doc::Function kGetRaceInfo{
    "Returns the localized name and file name of a race.", kRaceIDArgs, kRaceInfoRets};

const Game::Doc::Field kClassIDArgs[] = {
    Game::Doc::Req("classID", "number"),
};
const Game::Doc::Field kClassInfoFields[] = {
    Game::Doc::Req("className", "string"),
    Game::Doc::Req("classFile", "string", "The token UnitClass returns second."),
    Game::Doc::Req("classID", "number"),
};
const Game::Doc::Structure kClassInfo{"ClassInfo", "CreatureInfo", kClassInfoFields};
const Game::Doc::Field kClassInfoRets[] = {
    Game::Doc::Opt("classInfo", "ClassInfo", nullptr, "Nil for an unknown class ID."),
};
const Game::Doc::Function kGetClassInfo{
    "Returns the localized name and file name of a class.", kClassIDArgs, kClassInfoRets};

const Game::Doc::Field kFamilyIDArgs[] = {
    Game::Doc::Req("creatureFamilyID", "number"),
};
const Game::Doc::Field kFamilyInfoFields[] = {
    Game::Doc::Req("id", "number"),
    Game::Doc::Req("name", "cstring"),
    Game::Doc::Req("iconFile", "string", "Icon texture path; \"\" for a family without one."),
};
const Game::Doc::Structure kCreatureFamilyInfo{"CreatureFamilyInfo", "CreatureInfo",
                                               kFamilyInfoFields};
const Game::Doc::Field kFamilyInfoRets[] = {
    Game::Doc::Opt("creatureFamilyInfo", "CreatureFamilyInfo", nullptr,
                   "Nil for an unknown family ID."),
};
const Game::Doc::Function kGetCreatureFamilyInfo{
    "Returns the name and icon of a creature family, such as Wolf.", kFamilyIDArgs,
    kFamilyInfoRets};

const Game::Doc::Field kFamilyIDsRets[] = {
    Game::Doc::Req("creatureFamilyIDs", "table", "An array of numbers."),
};
const Game::Doc::Function kGetCreatureFamilyIDs{
    "Returns the ID of every creature family.", {}, kFamilyIDsRets};

const Game::Doc::Field kFactionInfoFields[] = {
    Game::Doc::Req("name", "cstring", "The localized faction group name."),
    Game::Doc::Req("groupTag", "cstring",
                   "\"Alliance\" or \"Horde\", the same in every locale."),
};
const Game::Doc::Structure kFactionInfo{"FactionInfo", "CreatureInfo", kFactionInfoFields};
const Game::Doc::Field kFactionInfoRets[] = {
    Game::Doc::Opt("factionInfo", "FactionInfo", nullptr,
                   "Nil for an unknown race or a race without a faction group."),
};
const Game::Doc::Function kGetFactionInfo{
    "Returns the faction group a race belongs to.", kRaceIDArgs, kFactionInfoRets};

const Game::Doc::Field kTypeIDArgs[] = {
    Game::Doc::Req("creatureTypeID", "number"),
};
const Game::Doc::Field kTypeInfoFields[] = {
    Game::Doc::Req("id", "number"),
    Game::Doc::Req("name", "cstring"),
};
const Game::Doc::Structure kCreatureTypeInfo{"CreatureTypeInfo", "CreatureInfo",
                                             kTypeInfoFields};
const Game::Doc::Field kTypeInfoRets[] = {
    Game::Doc::Opt("creatureTypeInfo", "CreatureTypeInfo", nullptr,
                   "Nil for an unknown type ID."),
};
const Game::Doc::Function kGetCreatureTypeInfo{
    "Returns the localized name of a creature type, such as Beast.", kTypeIDArgs,
    kTypeInfoRets};

const Game::Doc::Field kTypeIDsRets[] = {
    Game::Doc::Req("creatureTypeIDs", "table", "An array of numbers."),
};
const Game::Doc::Function kGetCreatureTypeIDs{
    "Returns the ID of every creature type.", {}, kTypeIDsRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_CreatureInfo", "GetCreatureInfoByID",
                                     &Script_GetCreatureInfoByID, &kGetCreatureInfoByID);
    Game::Lua::RegisterTableFunction("C_CreatureInfo", "RequestLoadCreatureByID",
                                     &Script_RequestLoadCreatureByID,
                                     &kRequestLoadCreatureByID);
    Game::Lua::RegisterTableFunction("C_CreatureInfo", "GetRaceInfo",
                                     &Script_GetRaceInfo, &kGetRaceInfo);
    Game::Lua::RegisterTableFunction("C_CreatureInfo", "GetClassInfo",
                                     &Script_GetClassInfo, &kGetClassInfo);
    Game::Lua::RegisterTableFunction("C_CreatureInfo", "GetCreatureFamilyInfo",
                                     &Script_GetCreatureFamilyInfo, &kGetCreatureFamilyInfo);
    Game::Lua::RegisterTableFunction("C_CreatureInfo", "GetCreatureFamilyIDs",
                                     &Script_GetCreatureFamilyIDs, &kGetCreatureFamilyIDs);
    Game::Lua::RegisterTableFunction("C_CreatureInfo", "GetFactionInfo",
                                     &Script_GetFactionInfo, &kGetFactionInfo);
    Game::Lua::RegisterTableFunction("C_CreatureInfo", "GetCreatureTypeInfo",
                                     &Script_GetCreatureTypeInfo, &kGetCreatureTypeInfo);
    Game::Lua::RegisterTableFunction("C_CreatureInfo", "GetCreatureTypeIDs",
                                     &Script_GetCreatureTypeIDs, &kGetCreatureTypeIDs);
    Cache::QueryLoad::Register(reinterpret_cast<void *>(Offsets::VAR_CREATURE_CACHE),
                               &_reserveLoadResult, Offsets::FUN_CREATURE_GET_RECORD);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

bool FactionGroupForRace(int raceID, const char **english, const char **localized) {
    if (raceID <= 0)
        return false;

    const uint8_t *crRec = DBC::Record(Offsets::VAR_CHRRACES_RECORDS,
                                       Offsets::VAR_CHRRACES_COUNT,
                                       static_cast<uint32_t>(raceID));
    if (crRec == nullptr)
        return false; // unknown race
    const int ftId = Game::Read<int>(
        crRec, Offsets::OFF_CHRRACES_FACTION_TEMPLATE);
    if (ftId <= 0)
        return false;

    const uint8_t *ftRec = DBC::Record(Offsets::VAR_FACTIONTEMPLATE_RECORDS,
                                       Offsets::VAR_FACTIONTEMPLATE_COUNT,
                                       static_cast<uint32_t>(ftId));
    if (ftRec == nullptr)
        return false;
    const uint32_t mask = Game::Read<uint32_t>(
        ftRec, Offsets::OFF_FACTIONTEMPLATE_GROUP_MASK);

    // FactionGroup is stored contiguously (records-base + i*stride), not as a
    // pointer array — iterate by hand rather than via DBC::Record.
    auto *fgBase = Game::Read<const uint8_t *>(
        Offsets::VAR_FACTIONGROUP_RECORDS);
    const int fgCount =
        Game::Read<int>(Offsets::VAR_FACTIONGROUP_COUNT);
    const int locale = Game::Read<int>(Offsets::VAR_LOCALE_INDEX);
    if (fgBase == nullptr)
        return false;

    for (int i = 0; i < fgCount; ++i) {
        const uint8_t *row = fgBase + i * Offsets::FACTIONGROUP_STRIDE;
        const int bit = Game::Read<int>(
            row, Offsets::OFF_FACTIONGROUP_BIT);
        if ((mask & (1u << (bit & 0x1F))) == 0)
            continue;
        const char *loc = Game::Read<const char *>(
            row, Offsets::OFF_FACTIONGROUP_NAMES + locale * 4);
        if (loc == nullptr || *loc == '\0')
            continue; // "Player" / "Monster" have empty localized names
        *english = Game::Read<const char *>(
            row, Offsets::OFF_FACTIONGROUP_ENGLISH);
        *localized = loc;
        return true;
    }
    return false; // no named group
}

const uint8_t *CacheRecord(uint32_t creatureID) {
    return creatureID != 0 ? PeekCreature(creatureID) : nullptr;
}

bool RequestLoad(uint32_t creatureID) {
    return Cache::QueryLoad::RequestLoad(reinterpret_cast<void *>(Offsets::VAR_CREATURE_CACHE),
                                         creatureID);
}

} // namespace Creature::Info
