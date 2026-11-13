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
#include "item/ID.h"

#include <cstdint>

namespace Item::AuctionSellID {

namespace {

// Engine's typed object lookup. Pass the typeCode in ECX and the
// 64-bit GUID split into low/high on the stack. The third stack arg
// is a `__LINE__`-style debug hint that the function reads only for
// asserts — anything works at runtime.
using GetObjectByGUID_t = void *(__fastcall *)(int typeCode, void *unused,
                                                uint32_t guidLo, uint32_t guidHi,
                                                int debugLine);

} // namespace

// `GetAuctionSellItemID()` — itemID companion to the engine's
// `GetAuctionSellItemInfo` (which returns name/texture/count/quality
// for the item currently sitting in the auction-sell slot, but does
// NOT return itemID). nil when the sell slot is empty.
static int __fastcall Script_GetAuctionSellItemID(void *L) {
    const uint32_t guidLo = *reinterpret_cast<const uint32_t *>(
        Offsets::VAR_AUCTION_SELL_GUID_LO);
    const uint32_t guidHi = *reinterpret_cast<const uint32_t *>(
        Offsets::VAR_AUCTION_SELL_GUID_HI);
    if ((guidLo | guidHi) == 0)
        return 0;

    auto lookup = reinterpret_cast<GetObjectByGUID_t>(Offsets::FUN_GET_OBJECT_BY_GUID);
    auto *item = static_cast<uint8_t *>(
        lookup(Offsets::OBJECT_TYPE_ITEM, nullptr, guidLo, guidHi, 0));
    if (item == nullptr)
        return 0;

    const int itemID = Item::ID::FromCGItem(item);
    if (itemID == 0)
        return 0;

    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    return 1;
}

static const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("itemID", "number", nullptr, "Nil when the sell slot is empty."),
};
static const Game::Doc::Function kGetAuctionSellItemID{
    "Returns the item ID of the item in the auction house sell slot.", {}, kRets,
    "ItemGlobals", true};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetAuctionSellItemID", &Script_GetAuctionSellItemID,
                                      &kGetAuctionSellItemID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::AuctionSellID
