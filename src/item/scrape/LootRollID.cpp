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

namespace Item::LootRollID {

namespace {

using LootRollFromID_t = const uint8_t *(__fastcall *)(int rollID);

} // namespace

// `GetLootRollItemID(rollID)` — itemID companion to
// `GetLootRollItemLink`. Walks the engine's in-progress-group-roll
// list (intrusive next-pointer chain) for an entry whose rollID
// matches; returns its itemID. nil for unknown rollIDs / completed
// rolls.
static int __fastcall Script_GetLootRollItemID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetLootRollItemID(rollID)");
        return 0;
    }
    const int rollID = static_cast<int>(Game::Lua::ToNumber(L, 1));

    auto resolver = reinterpret_cast<LootRollFromID_t>(Offsets::FUN_LOOT_ROLL_FROM_ID);
    auto *entry = resolver(rollID);
    if (entry == nullptr)
        return 0;

    const int itemID = static_cast<int>(*reinterpret_cast<const uint32_t *>(
        entry + Offsets::OFF_LOOT_ROLL_ITEM_ID));
    if (itemID == 0)
        return 0;

    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    return 1;
}

// --- Documentation ----------------------------------------------------------

static const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("rollID", "number", "A roll ID from START_LOOT_ROLL."),
};
static const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("itemID", "number", nullptr,
                   "Nil when no roll in progress has the ID."),
};
static const Game::Doc::Function kGetLootRollItemID{
    "Returns the item of a group loot roll.", kArgs, kRets,
    "ItemGlobals", true};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetLootRollItemID", &Script_GetLootRollItemID,
                                      &kGetLootRollItemID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::LootRollID
