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

// `IsCurrentItem(item)` — 1 while the player is casting with the item, else
// nil. 4.3.4's `Script_IsCurrentItem` (0x008C2A10) asks
// `Spell_C_IsCurrentItem`, which matches the item entry of each pending cast
// that came from an item. 1.12 keeps one in-flight cast item, and its own
// `IsCurrentAction` (0x004E53A0) checks an item slot exactly this way: the
// item at `VAR_CAST_ITEM_GUID` must have the slot's itemID. This mirrors
// that branch, so `IsCurrentItem` and the action button's checked state agree.

#include "Game.h"
#include "Offsets.h"
#include "item/ID.h"
#include "item/Location.h"
#include "object/Resolve.h"

#include <cstdint>

namespace Item::Current {

namespace {

bool IsCastItem(int itemID) {
    const uint64_t guid = *reinterpret_cast<const uint64_t *>(
        static_cast<uintptr_t>(Offsets::VAR_CAST_ITEM_GUID));
    if (itemID <= 0 || guid == 0)
        return false;
    const auto *item =
        static_cast<const uint8_t *>(Object::ByGuid(Offsets::TYPEMASK_ITEM, guid));
    return item != nullptr && Item::ID::FromCGItem(item) == itemID;
}

int __fastcall Script_IsCurrentItem(void *L) {
    if (IsCastItem(Item::Location::IDForArg(L, 1, Offsets::INVMGR_SEARCH_BANK)))
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("item", "number", "An item ID, an item link, or the name of a carried item."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("isCurrent", "number", nullptr,
                   "1 while the player is casting the item's use spell, else nil."),
};
const Game::Doc::Function kIsCurrentItem{
    "Whether the player is now using the item.", kArgs, kRets, "ItemGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsCurrentItem", &Script_IsCurrentItem, &kIsCurrentItem);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::Current
