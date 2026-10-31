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

#include "Game.h"
#include "Offsets.h"
#include "item/Arg.h"
#include "item/Location.h"

#include <cstdint>

namespace Item::Exists {

// `C_Item.DoesItemExist(itemLocation)` — true iff the location resolves
// to a populated inventory slot on the active player. Equipment-slot
// locations check the character pane; bag-slot locations walk the
// player's inventory manager. Empty slots and invalid tables return
// false without raising.
static int __fastcall Script_C_Item_DoesItemExist(void *L) {
    if (!Item::Location::IsLocationArg(L, 1)) {
        Game::Lua::PushBool(L, 0);
        return 1;
    }
    const uint8_t *item = Item::Location::Resolve(L, 1);
    Game::Lua::PushBoolean(L, item != nullptr);
    return 1;
}

// `C_Item.DoesItemExistByID(itemInfo)` — true if `itemID` looks like a
// valid item identifier. Modern WoW answers from the client-side item
// database (Item-sparse.db2) and is independent of whether the network
// cache has been warmed yet. 1.12 has no client-side DB, so we
// optimistically accept any positive itemID; callers needing the
// "is the cache populated?" check should use `IsItemDataCachedByID`.
//
// This split matters for `ItemMixin:IsItemEmpty` (Util/Item.lua),
// which uses `DoesItemExistByID` to gate `ContinueOnItemLoad` — the
// whole point of `ContinueOnItemLoad` is to wait for an uncached item
// to load, so the existence check must succeed for items that aren't
// in the cache yet.
//
// We deliberately do NOT auto-warm the cache here. Existence is a pure
// query about the ID, not a load request — and during early-login
// (cold WDB, pfQuest's PLAYER_ENTERING_WORLD path) a spurious cache
// touch can race the engine's natural inventory prefetch and leave the
// item stuck. Callers wanting to actually load data should use
// `RequestLoadItemDataByID`; passive readers (`GetItemInfo`,
// `GetItemNameByID`) already warm on their own miss path.
//
// Accepts numeric itemID or `"item:NNN..."` string (including full
// chat links); never raises.
static int __fastcall Script_C_Item_DoesItemExistByID(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    Game::Lua::PushBoolean(L, itemID > 0);
    return 1;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "DoesItemExist", &Script_C_Item_DoesItemExist);
    Game::Lua::RegisterTableFunction("C_Item", "DoesItemExistByID", &Script_C_Item_DoesItemExistByID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::Exists
