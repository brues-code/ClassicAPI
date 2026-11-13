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

namespace Item::LootSlotID {

// `GetLootSlotItemID(slot)` — itemID companion to `GetLootSlotLink`.
// Reads the engine's loot-slot array directly. nil when no loot
// window is open, slot is out of range, or slot is empty (coin / sub-
// item slots).
//
// Matches the engine's slot validation:
//   - `(GUID_LO | GUID_HI) != 0`  — a loot source is active.
//   - `slot >= 1 && slot <= LOOT_MAX_SLOTS` (1-based input).
//   - If `[VAR_LOOT_LOOTABLE] != 0`, the engine pre-decrements the
//     slot a second time (lootable-corpse view uses 1-based slots
//     externally and 0-based internally one extra hop). We mirror
//     that to stay in lockstep with `GetLootSlotLink`.
//   - `entry+0` (itemID) must be non-zero — coin slots are zeroed.
static int __fastcall Script_GetLootSlotItemID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetLootSlotItemID(slot)");
        return 0;
    }
    int slot = static_cast<int>(Game::Lua::ToNumber(L, 1));

    const uint32_t guidLo = Game::Read<uint32_t>(Offsets::VAR_LOOT_GUID_LO);
    const uint32_t guidHi = Game::Read<uint32_t>(Offsets::VAR_LOOT_GUID_HI);
    if ((guidLo | guidHi) == 0)
        return 0;

    slot -= 1;
    if (Game::Read<uint32_t>(Offsets::VAR_LOOT_LOOTABLE) != 0) {
        if (slot <= 0)
            return 0;
        slot -= 1;
    }
    if (slot < 0 || slot >= Offsets::LOOT_MAX_SLOTS)
        return 0;

    auto *entry = reinterpret_cast<const uint8_t *>(
        Offsets::VAR_LOOT_SLOTS + slot * Offsets::LOOT_SLOT_STRIDE);
    const int itemID = static_cast<int>(*reinterpret_cast<const uint32_t *>(entry));
    if (itemID == 0)
        return 0;

    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    return 1;
}

// --- Documentation ----------------------------------------------------------

static const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("slot", "luaIndex", "A slot of the open loot window."),
};
static const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("itemID", "number", nullptr,
                   "Nil when no loot window is open, the slot is empty, or it holds money."),
};
static const Game::Doc::Function kGetLootSlotItemID{
    "Returns the item in a slot of the loot window.", kArgs, kRets,
    "ItemGlobals", true};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetLootSlotItemID", &Script_GetLootSlotItemID,
                                      &kGetLootSlotItemID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::LootSlotID
