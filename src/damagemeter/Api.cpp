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

// C_DamageMeter, retail's damage meter API (DamageMeterDocumentation.lua),
// over the live meter (damagemeter/Live.cpp). The tables are built by
// damagemeter/Tables.cpp. An unknown session, or a type that is not a
// DamageMeterType, gives an empty session (our choice: the documented
// returns are not nilable).

#include "Game.h"
#include "damagemeter/Live.h"
#include "damagemeter/Tables.h"

#include <cstdint>

namespace DamageMeter::Api {

namespace {

namespace Lua = Game::Lua;
using Game::Doc::Opt;
using Game::Doc::Req;

int IntArg(void *L, int index) { return static_cast<int>(Lua::CheckNumber(L, index)); }

const char *GuidArg(void *L, int index) {
    return Lua::Type(L, index) == Lua::TYPE_STRING ? Lua::ToString(L, index) : nullptr;
}

uint32_t CreatureArg(void *L, int index) {
    return Lua::IsNumber(L, index) ? static_cast<uint32_t>(Lua::ToNumber(L, index)) : 0;
}

View SessionView(int sessionType) {
    return Live::Sessions().ViewOfType(sessionType, Live::NowMs());
}

View IDView(int sessionID) {
    return Live::Sessions().ViewOfSession(static_cast<uint32_t>(sessionID), Live::NowMs());
}

int __fastcall Script_GetAvailableCombatSessions(void *L) {
    Tables::PushAvailableSessions(L, Live::Sessions(), Live::NowMs());
    return 1;
}

int __fastcall Script_GetCombatSessionFromID(void *L) {
    const int sessionID = IntArg(L, 1);
    const int type = IntArg(L, 2);
    Tables::PushCombatSession(L, IDView(sessionID), type);
    return 1;
}

int __fastcall Script_GetCombatSessionFromType(void *L) {
    const int sessionType = IntArg(L, 1);
    const int type = IntArg(L, 2);
    Tables::PushCombatSession(L, SessionView(sessionType), type);
    return 1;
}

int __fastcall Script_GetCombatSessionSourceFromID(void *L) {
    const int sessionID = IntArg(L, 1);
    const int type = IntArg(L, 2);
    Tables::PushCombatSessionSource(L, IDView(sessionID), type, GuidArg(L, 3), CreatureArg(L, 4));
    return 1;
}

int __fastcall Script_GetCombatSessionSourceFromType(void *L) {
    const int sessionType = IntArg(L, 1);
    const int type = IntArg(L, 2);
    Tables::PushCombatSessionSource(L, SessionView(sessionType), type, GuidArg(L, 3),
                                    CreatureArg(L, 4));
    return 1;
}

int __fastcall Script_GetSessionDurationSeconds(void *L) {
    const View view = SessionView(IntArg(L, 1));
    if (view.hasDuration)
        Lua::PushNumber(L, view.seconds);
    else
        Lua::PushNil(L);
    return 1;
}

int __fastcall Script_IsDamageMeterAvailable(void *L) {
    Lua::PushBool(L, true);
    Lua::PushString(L, "");
    return 2;
}

int __fastcall Script_ResetAllCombatSessions(void *) {
    Live::ResetAll();
    return 0;
}

// --- Documentation ----------------------------------------------------------
//
// Retail's names, types and summaries.

const Game::Doc::Field kAvailableFields[] = {
    Req("sessionID", "number"),
    Req("name", "cstring", "The enemy the session is named after; empty when none."),
    Opt("durationSeconds", "number"),
};
const Game::Doc::Structure kAvailable{"DamageMeterAvailableCombatSession", "DamageMeter",
                                      kAvailableFields,
                                      "Data for a combat session currently being tracked."};

const Game::Doc::Field kSessionFields[] = {
    Req("combatSources", "table", "An array of DamageMeterCombatSource, highest first."),
    Opt("maxAmount", "number", "0"),
    Opt("totalAmount", "number", "0"),
    Opt("durationSeconds", "number"),
};
const Game::Doc::Structure kSession{
    "DamageMeterCombatSession", "DamageMeter", kSessionFields,
    "Aggregated data for all sources (units) in a damage meter combat session."};

const Game::Doc::Field kSessionSourceFields[] = {
    Req("combatSpells", "table", "An array of DamageMeterCombatSpell, highest first."),
    Opt("maxAmount", "number", "0"),
    Opt("totalAmount", "number", "0"),
};
const Game::Doc::Structure kSessionSource{"DamageMeterCombatSessionSource", "DamageMeter",
                                          kSessionSourceFields,
                                          "Data for a single source (unit) in a combat session."};

const Game::Doc::Field kSourceFields[] = {
    Opt("sourceGUID", "WOWGUID", nullptr, "Nil for a creature."),
    Opt("sourceCreatureID", "number", nullptr, "Nil for a player or a pet."),
    Req("name", "cstring"),
    Req("classFilename", "cstring", "Empty for a creature."),
    Req("specIconID", "fileID", "Always 0."),
    Req("totalAmount", "number"),
    Req("amountPerSecond", "number"),
    Req("isLocalPlayer", "bool"),
    Req("deathRecapID", "number", "Always 0."),
    Req("deathTimeSeconds", "number"),
    Req("classification", "cstring"),
    Req("sourceDisplayType", "DamageMeterSourceDisplayType"),
    Opt("factionGroup", "cstring"),
};
const Game::Doc::Structure kSource{
    "DamageMeterCombatSource", "DamageMeter", kSourceFields,
    "Aggregated data for a single source (unit) in a damage meter combat session."};

const Game::Doc::Field kSpellFields[] = {
    Req("spellID", "number"),
    Req("totalAmount", "number"),
    Req("amountPerSecond", "number"),
    Req("creatureName", "cstring", "The pet or guardian that cast it; empty for the source's own."),
    Req("overkillAmount", "number"),
    Req("isAvoidable", "bool", "Always false."),
    Req("isDeadly", "bool"),
    Req("combatSpellDetails", "DamageMeterCombatSpellUnitDetails"),
};
const Game::Doc::Structure kSpell{"DamageMeterCombatSpell", "DamageMeter", kSpellFields,
                                  "Aggregated data for all spells of the same ID cast by a "
                                  "single source (unit) in a combat session."};

const Game::Doc::Field kDetailsFields[] = {
    Req("unitName", "cstring"),
    Req("unitClassFilename", "cstring"),
    Req("classification", "cstring"),
    Req("isPet", "bool"),
    Req("isMob", "bool"),
    Req("amount", "number"),
    Req("specIconID", "fileID", "Always 0."),
};
const Game::Doc::Structure kDetails{"DamageMeterCombatSpellUnitDetails", "DamageMeter",
                                    kDetailsFields,
                                    "Amount for a single target for all casts of the same "
                                    "spellID by a single source (unit) in a combat session."};

const Game::Doc::Field kAvailableRets[] = {
    Req("availableSessions", "table", "An array of DamageMeterAvailableCombatSession."),
};
const Game::Doc::Function kGetAvailable{
    "Returns a list of combat sessions currently being tracked.", {}, kAvailableRets};

const Game::Doc::Field kIDArgs[] = {
    Req("sessionID", "number"),
    Req("type", "DamageMeterType"),
};
const Game::Doc::Field kTypeArgs[] = {
    Req("sessionType", "DamageMeterSessionType"),
    Req("type", "DamageMeterType"),
};
const Game::Doc::Field kSessionRets[] = {
    Req("session", "DamageMeterCombatSession"),
};
const Game::Doc::Function kFromID{
    "Returns the data for the player's combat session with the specified ID.", kIDArgs,
    kSessionRets};
const Game::Doc::Function kFromType{"Returns the data for the player's current combat session.",
                                    kTypeArgs, kSessionRets};

const Game::Doc::Field kSourceIDArgs[] = {
    Req("sessionID", "number"),
    Req("type", "DamageMeterType"),
    Opt("sourceGUID", "WOWGUID"),
    Opt("sourceCreatureID", "number"),
};
const Game::Doc::Field kSourceTypeArgs[] = {
    Req("sessionType", "DamageMeterSessionType"),
    Req("type", "DamageMeterType"),
    Opt("sourceGUID", "WOWGUID"),
    Opt("sourceCreatureID", "number"),
};
const Game::Doc::Field kSourceRets[] = {
    Req("sessionSource", "DamageMeterCombatSessionSource"),
};
const Game::Doc::Function kSourceFromID{
    "Returns the data for a single source (unit) in the player's current combat session with "
    "the specified ID.",
    kSourceIDArgs, kSourceRets};
const Game::Doc::Function kSourceFromType{
    "Returns the data for a single source (unit) in the player's current combat session.",
    kSourceTypeArgs, kSourceRets};

const Game::Doc::Field kDurationArgs[] = {
    Req("sessionType", "DamageMeterSessionType"),
};
const Game::Doc::Field kDurationRets[] = {
    Opt("durationSeconds", "number"),
};
const Game::Doc::Function kDuration{"Returns the amount of time a combat session has lasted",
                                    kDurationArgs, kDurationRets};

const Game::Doc::Field kAvailableCheckRets[] = {
    Req("isAvailable", "bool"),
    Req("failureReason", "string"),
};
const Game::Doc::Function kIsAvailable{
    "Returns whether the player can enable and use the Damage Meter.", {}, kAvailableCheckRets};

const Game::Doc::Function kReset{"Clears the data for all the player's combat sessions.", {}, {}};

void RegisterLuaFunctions() {
    constexpr const char *kTable = "C_DamageMeter";
    Lua::RegisterTableFunction(kTable, "GetAvailableCombatSessions",
                               &Script_GetAvailableCombatSessions, &kGetAvailable);
    Lua::RegisterTableFunction(kTable, "GetCombatSessionFromID", &Script_GetCombatSessionFromID,
                               &kFromID);
    Lua::RegisterTableFunction(kTable, "GetCombatSessionFromType",
                               &Script_GetCombatSessionFromType, &kFromType);
    Lua::RegisterTableFunction(kTable, "GetCombatSessionSourceFromID",
                               &Script_GetCombatSessionSourceFromID, &kSourceFromID);
    Lua::RegisterTableFunction(kTable, "GetCombatSessionSourceFromType",
                               &Script_GetCombatSessionSourceFromType, &kSourceFromType);
    Lua::RegisterTableFunction(kTable, "GetSessionDurationSeconds",
                               &Script_GetSessionDurationSeconds, &kDuration);
    Lua::RegisterTableFunction(kTable, "IsDamageMeterAvailable", &Script_IsDamageMeterAvailable,
                               &kIsAvailable);
    Lua::RegisterTableFunction(kTable, "ResetAllCombatSessions", &Script_ResetAllCombatSessions,
                               &kReset);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace DamageMeter::Api
