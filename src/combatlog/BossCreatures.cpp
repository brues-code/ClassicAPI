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

// `C_InstanceEncounter.AddBossCreatureID` / `RemoveBossCreatureID` /
// `GetBossCreatureIDs` — a ClassicAPI extension. Blizzard clients learn their
// bosses from the server (its engage messages, aura 357) and the encounter
// from DungeonEncounter; 1.12 has neither, so CombatLog::Encounter takes the
// creature's rank 3 as the boss mark, and most dungeon bosses are only elite.
// These functions let an addon name more bosses by creature ID. A listed
// creature is a boss to every consumer of the encounter: the `bossN` tokens,
// INSTANCE_ENCOUNTER_ENGAGE_UNIT, the combat log file's ENCOUNTER_START / END
// lines and the damage meter's encounter sessions.
//
// No table ships with the DLL: which creatures are bosses depends on the
// server. The list lives as long as the Lua state: each load of the in-game
// script functions (login, /reload) starts it empty, so an addon adds its IDs
// again at load, and an ID it no longer names does not stay behind.

#include "combatlog/BossCreatures.h"

#include "Game.h"
#include "lua/Arg.h"

#include <cstdint>
#include <set>

namespace CombatLog::BossCreatures {

namespace {

namespace Arg = Game::Lua::Arg;

std::set<uint32_t> g_ids; // ordered, so GetBossCreatureIDs lists them ascending

int __fastcall Script_AddBossCreatureID(void *L) {
    uint32_t id = 0;
    if (Arg::UInt32(L, 1, "Usage: C_InstanceEncounter.AddBossCreatureID(creatureID)", id))
        g_ids.insert(id);
    return 0;
}

int __fastcall Script_RemoveBossCreatureID(void *L) {
    uint32_t id = 0;
    if (Arg::UInt32(L, 1, "Usage: C_InstanceEncounter.RemoveBossCreatureID(creatureID)", id))
        g_ids.erase(id);
    return 0;
}

int __fastcall Script_GetBossCreatureIDs(void *L) {
    Game::Lua::NewTable(L);
    int i = 0;
    for (const uint32_t id : g_ids) {
        Game::Lua::PushNumber(L, static_cast<double>(id));
        Game::Lua::RawSetI(L, -2, ++i);
    }
    return 1;
}

const Game::Doc::Field kIdArgs[] = {
    Game::Doc::Req("creatureID", "number", "The creature ID, as UnitCreatureID returns it."),
};
const Game::Doc::Field kGetRets[] = {
    Game::Doc::Req("creatureIDs", "table", "The listed creature IDs, in ascending order."),
};
const Game::Doc::Function kAdd{
    "Makes the creatures with this ID bosses, so they get bossN tokens and start encounters "
    "until the next reload.",
    kIdArgs, {}, nullptr, true};
const Game::Doc::Function kRemove{
    "Takes a creature ID off the boss list; bosses already in the encounter keep their tokens.",
    kIdArgs, {}, nullptr, true};
const Game::Doc::Function kGet{"Returns the creature IDs that addons have made bosses.", {},
                               kGetRets, nullptr, true};

void RegisterLuaFunctions() {
    g_ids.clear(); // a new Lua state: the addons that filled the list are gone
    Game::Lua::RegisterTableFunction("C_InstanceEncounter", "AddBossCreatureID",
                                     &Script_AddBossCreatureID, &kAdd);
    Game::Lua::RegisterTableFunction("C_InstanceEncounter", "RemoveBossCreatureID",
                                     &Script_RemoveBossCreatureID, &kRemove);
    Game::Lua::RegisterTableFunction("C_InstanceEncounter", "GetBossCreatureIDs",
                                     &Script_GetBossCreatureIDs, &kGet);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

bool Listed(uint32_t creatureID) { return g_ids.count(creatureID) != 0; }

} // namespace CombatLog::BossCreatures
