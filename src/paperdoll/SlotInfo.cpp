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

// `C_PaperDollInfo.GetInventorySlotInfoForInvSlot(invSlot)` — the stock
// `GetInventorySlotInfo(slotName)` looked up the other way: from a slot
// number to its paper-doll row. It walks the same PaperDollItemFrame.dbc rows
// the engine's own function walks (Script_GetInventorySlotInfo, 0x004C81B0),
// in file order, takes the first row with that SlotNumber, and returns what
// the engine returns for it plus the row's name:
//
//   invSlot      the SlotNumber
//   slotTexture  the SlotIcon path as stored (this client has no file IDs)
//   checkRelic   true for the ranged slot, the engine's own relic test
//   slotName     the ItemSlotName ("HeadSlot", "MainHandSlot", ...)
//
// A slot number with no row returns nothing.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace PaperDoll::SlotInfo {

namespace {

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("invSlotValue", "number", "An inventory slot number, not a luaIndex."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("invSlot", "number"),
    Game::Doc::Req("slotTexture", "string", "The empty-slot texture path."),
    Game::Doc::Req("checkRelic", "bool"),
    Game::Doc::Req("slotName", "cstring"),
};
const Game::Doc::Function kGetInventorySlotInfoForInvSlot{
    "Returns the paper doll slot for an inventory slot number, or nothing if there is none.",
    kArgs, kRets};

int __fastcall Script_GetInventorySlotInfoForInvSlot(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_PaperDollInfo.GetInventorySlotInfoForInvSlot(invSlotValue)");
        return 0;
    }
    const int invSlot = static_cast<int>(Game::Lua::ToNumber(L, 1));

    const auto *records = Game::Read<const uint8_t *>(Offsets::VAR_PAPERDOLLITEMFRAME_RECORDS);
    const int count = Game::Read<int>(Offsets::VAR_PAPERDOLLITEMFRAME_COUNT);
    if (records == nullptr)
        return 0;

    for (int i = 0; i < count; ++i) {
        const uint8_t *row = records + i * Offsets::OFF_PAPERDOLLITEMFRAME_RECORD_STRIDE;
        const int slotNumber = Game::Read<int>(row, Offsets::OFF_PAPERDOLLITEMFRAME_SLOT_NUMBER);
        if (slotNumber != invSlot)
            continue;
        Game::Lua::PushNumber(L, static_cast<double>(slotNumber));
        Game::Lua::PushString(L, Game::Read<const char *>(row, Offsets::OFF_PAPERDOLLITEMFRAME_SLOT_ICON));
        Game::Lua::PushBool(L, slotNumber == Offsets::PAPERDOLLITEMFRAME_RELIC_SLOT_NUMBER);
        Game::Lua::PushString(L, Game::Read<const char *>(row, Offsets::OFF_PAPERDOLLITEMFRAME_SLOT_NAME));
        return 4;
    }
    return 0;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_PaperDollInfo", "GetInventorySlotInfoForInvSlot",
                                     &Script_GetInventorySlotInfoForInvSlot,
                                     &kGetInventorySlotInfoForInvSlot);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace PaperDoll::SlotInfo
