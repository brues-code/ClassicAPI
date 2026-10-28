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

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdint>

// CanCancelAuction(index) — 4.3.4's Script_CanCancelAuction (0x00975180):
// nothing without an auctioneer, nil for an owner-list index with no row or a
// sold row, else 1. The gates are 1.12's own Script_CancelAuction (0x004D0260):
// auctioneer GUID set, ftol(index) - 1 below the owner count (unsigned, so 0 and
// negatives miss), non-null record. 1.12's owner records carry no sold flag, so
// that 4.3.4 gate has nothing to read. The cancel-fee money check stays in
// CancelAuction, as in 4.3.4.

namespace AuctionHouse::Cancel {

namespace {

int __fastcall Script_CanCancelAuction(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: CanCancelAuction(index)");
        return 0;
    }
    if (Game::Read<uint32_t>(Offsets::VAR_AUCTION_AUCTIONEER_GUID_LO) == 0 &&
        Game::Read<uint32_t>(Offsets::VAR_AUCTION_AUCTIONEER_GUID_HI) == 0)
        return 0;

    const uint32_t index = static_cast<uint32_t>(static_cast<int64_t>(Game::Lua::ToNumber(L, 1))) - 1;
    const auto *entries = reinterpret_cast<void *const *>(Offsets::VAR_AUCTION_OWNER_ENTRIES);
    if (index < Game::Read<uint32_t>(Offsets::VAR_AUCTION_OWNER_COUNT) && entries[index] != nullptr)
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("index", "number", "Row in the \"owner\" auction list, from 1."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("canCancel", "number", nullptr,
                   "1 when the row is one of the player's auctions, else nil. Nothing when the auction house is closed."),
};
const Game::Doc::Function kCanCancelAuction{
    "Returns whether CancelAuction can cancel a row of the owner auction list.", kArgs, kRets,
    "AuctionHouseGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("CanCancelAuction", &Script_CanCancelAuction, &kCanCancelAuction);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace AuctionHouse::Cancel
