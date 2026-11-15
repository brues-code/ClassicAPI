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

#include "../Game.h"
#include "../Offsets.h"
#include "../guid/Guid.h"
#include "../object/Resolve.h"
#include "Request.h"

#include <cstdint>

namespace Loot::Nearby {

namespace {

// Only `TYPEMASK_UNIT` carries lootable-mob state in 1.12 — dead
// creatures stay as CGCreature_C objects with the lootable bit flipped
// on, rather than transitioning to a separate corpse object. Dead
// players become CGCorpse_C (`TYPEMASK_CORPSE`) but those aren't
// lootable by other players in vanilla — no reason to walk them here.

// Appends `{ guid = "0x..." }` at `idx` of the table on top of the stack
// when `guid` is a unit that is lootable to us and inside click-loot range
// of `player`.
void AppendIfLootable(void *L, void *player, uint64_t guid, int &idx) {
    void *obj = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, nullptr, 0);
    if (obj == nullptr || !Request::IsLootable(obj) || !Request::InReach(player, obj))
        return;

    char guidBuf[Guid::STRING_SIZE];
    Game::Lua::PushNumber(L, static_cast<double>(idx));
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldString(L, "guid", Guid::FormatAsString(guid, guidBuf, sizeof guidBuf));
    Game::Lua::SetTable(L, -3);
    ++idx;
}

// `C_Loot.GetNearbyLootableUnits()` — returns a 1-indexed array of
// `{ guid = "0x..." }` subtables, one per visible unit that is (a)
// flagged DYNFLAG_LOOTABLE by the server and (b) inside the engine's
// own interact range (the distance threshold that lights up the
// right-click-loot cursor). Empty table pre-login, when no lootable
// units are in range, or when we don't have loot rights on visible
// corpses (the server only sets the bit for clients with rights).
int __fastcall Script_GetNearbyLootableUnits(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    // The token resolve below reads the object registry, NULL during glue /
    // character-select; bail with an empty table rather than crashing.
    if (!Object::RegistryReady())
        return 1;

    // Resolve the local player once — the canonical CGPlayer_C the
    // engine's own interact-range checks use, distinct from the
    // `VAR_LOCAL_PLAYER_PTR` global. Bail if unavailable.
    void *player = Game::ResolveUnitToken("player");
    if (player == nullptr)
        return 1;

    int idx = 1;
    Object::ForEachVisible([&](uint64_t guid) {
        AppendIfLootable(L, player, guid, idx);
        return true;
    });
    return 1;
}

const Game::Doc::Field kLootableUnitFields[] = {
    Game::Doc::Req("guid", "WOWGUID"),
};
const Game::Doc::Structure kLootableUnit{"LootableUnit", "Loot", kLootableUnitFields,
                                         "A unit the player can loot."};

const Game::Doc::Field kRets[] = {
    Game::Doc::Req("units", "table",
                   "An array of LootableUnit; empty when none is in range or before login."),
};
const Game::Doc::Function kGetNearbyLootableUnits{
    "Returns the units the player can loot that are within right-click loot range.", {}, kRets,
    nullptr, true};

void Register() {
    Game::Lua::RegisterTableFunction("C_Loot", "GetNearbyLootableUnits",
                                     &Script_GetNearbyLootableUnits, &kGetNearbyLootableUnits);
}

} // namespace

static const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace Loot::Nearby
