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

namespace Item::TradeTargetID {

// `GetTradeTargetItemID(slot)` — itemID companion to
// `GetTradeTargetItemLink`. Mirrors the target side of the trade
// window (the items the other player has dragged in). Same 1..7
// slot range as the player side, but the storage is wider — each
// slot is an `{?, itemID}` pair with the itemID at `+4`.
static int __fastcall Script_GetTradeTargetItemID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetTradeTargetItemID(slot)");
        return 0;
    }
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    if (slot < 0 || slot >= Offsets::TRADE_MAX_SLOTS)
        return 0;

    auto *entry = reinterpret_cast<const uint8_t *>(
        Offsets::VAR_TRADE_TARGET_SLOTS + slot * Offsets::TRADE_TARGET_STRIDE);
    const int itemID = static_cast<int>(*reinterpret_cast<const uint32_t *>(
        entry + Offsets::OFF_TRADE_TARGET_ITEM_ID));
    if (itemID == 0)
        return 0;

    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    return 1;
}

// --- Documentation ----------------------------------------------------------

static const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("slot", "luaIndex", "From 1 to 7; slot 7 is the item that is not traded."),
};
static const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("itemID", "number", nullptr,
                   "Nil when the slot is empty."),
};
static const Game::Doc::Function kGetTradeTargetItemID{
    "Returns the item in a slot of the other player's side of the trade window.", kArgs, kRets,
    "ItemGlobals", true};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetTradeTargetItemID", &Script_GetTradeTargetItemID,
                                      &kGetTradeTargetItemID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::TradeTargetID
