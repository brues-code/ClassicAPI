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

#include <cstdint>

namespace Item::MerchantID {

// `GetMerchantItemID(index)` — itemID companion to
// `GetMerchantItemLink`. Reads the merchant inventory array directly.
// nil when no merchant window is open or the index is out of range.
static int __fastcall Script_GetMerchantItemID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetMerchantItemID(index)");
        return 0;
    }
    const int index = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    if (index < 0)
        return 0;

    const uint32_t guidLo = Game::Read<uint32_t>(Offsets::VAR_MERCHANT_NPC_GUID_LO);
    const uint32_t guidHi = Game::Read<uint32_t>(Offsets::VAR_MERCHANT_NPC_GUID_HI);
    if ((guidLo | guidHi) == 0)
        return 0;

    const int count = static_cast<int>(Game::Read<uint32_t>(
        Offsets::VAR_MERCHANT_COUNT));
    if (index >= count)
        return 0;

    auto *entry = reinterpret_cast<const uint8_t *>(
        Offsets::VAR_MERCHANT_ITEMS + index * Offsets::MERCHANT_STRIDE);
    const int itemID = static_cast<int>(Game::Read<uint32_t>(
        entry, Offsets::OFF_MERCHANT_ITEM_ID));
    if (itemID == 0)
        return 0;

    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    return 1;
}

// --- Documentation ----------------------------------------------------------

static const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("index", "luaIndex", "An item of the open merchant."),
};
static const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("itemID", "number", nullptr,
                   "Nil when no merchant window is open or the index is out of range."),
};
static const Game::Doc::Function kGetMerchantItemID{
    "Returns the item that the merchant sells at an index.", kArgs, kRets,
    "ItemGlobals"};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetMerchantItemID", &Script_GetMerchantItemID,
                                      &kGetMerchantItemID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::MerchantID
