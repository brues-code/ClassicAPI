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

// `GetActionInfo(slot)` — modern-WoW-style action descriptor lookup.
// Returns `actionType, id, subType` for the slot, or nothing if the slot
// holds nothing the descriptor can describe (4.3.4 returns nothing there
// too, rather than a nil).
//
// The vanilla 1.12.1 engine stores action descriptors as packed
// uint32s in `VAR_ACTION_TABLE` (`0x00BC6980`, 120 slots). Top 4 bits
// are the type tag — the server's ACTION_BUTTON_* values shifted into the
// top byte: 0x00 spell, 0x40 macro, 0x80 item.
//
//   spell action:    "spell", spellID, "spell"
//   macro:           "macro", macroIndex (1-based)
//   item:            "item", itemID
//   equipment set:   "equipmentset", setName
//
// The subType is always "spell" for spell-type entries — the engine
// helper hardcodes pet=0 for entries on this table. Pet-bar actions
// live in a separate table; that's `PetHasActionBar` + the pet-action
// helpers, not this function.
//
// A 0x40 entry is a macro only while its macroID is in the macro-slot map
// (`FUN_ACTION_IS_MACRO`, which every engine reader gates its macro branch
// on). One that is not — a macro deleted out from under its button — is
// nothing, as in 4.3.4: its IsItem and IsMacro are both false, so it falls
// through to an empty return. There is no bag-item tag: items are always
// by-itemID (0x80), and an earlier reading of the 0x40 "miss" as a bag item
// was wrong.

#include "Game.h"
#include "Offsets.h"
#include "action/Slot.h"
#include "equipmentset/Data.h"

#include <cstdint>

namespace Action::Info {

namespace {

using SlotToSpell_t = uint32_t(__fastcall *)(uint32_t slot0, uint32_t *outPetFlag);

uint32_t ReadEntry(int slot0) {
    auto *base = reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_ACTION_TABLE));
    return base[slot0];
}

int __fastcall Script_GetActionInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetActionInfo(slot)");
        return 0;
    }

    const int slot0 = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    if (slot0 < 0 || slot0 >= Offsets::ACTION_TABLE_MAX_SLOTS)
        return 0;

    const uint32_t entry = ReadEntry(slot0);
    if (entry == 0)
        return 0;

    const uint32_t type = entry & Offsets::ACTION_TYPE_MASK;

    if (type == Offsets::ACTION_TYPE_SPELL) {
        // For spell entries the engine helper just returns the entry as
        // spellID; we still call through it so the petFlag behavior
        // stays in sync with what other action readers see (the helper
        // also handles the case where the slot is somehow invalid).
        auto fn = reinterpret_cast<SlotToSpell_t>(
            static_cast<uintptr_t>(Offsets::FUN_ACTION_SLOT_TO_SPELL));
        uint32_t petFlag = 0;
        const uint32_t spellID = fn(static_cast<uint32_t>(slot0), &petFlag);
        if (spellID == 0)
            return 0;
        (void)petFlag; // hardcoded 0 in the engine helper for this table
        Game::Lua::PushString(L, "spell");
        Game::Lua::PushNumber(L, static_cast<double>(spellID));
        Game::Lua::PushString(L, "spell");
        return 3;
    }

    if (type == Offsets::ACTION_TYPE_BAG_OR_MACRO) {
        const uint32_t key = entry & Offsets::ACTION_PAYLOAD_MASK_BAG_OR_MACRO;
        const int macroSlot = Action::Slot::MacroSlotForID(key);
        if (macroSlot > 0) {
            Game::Lua::PushString(L, "macro");
            Game::Lua::PushNumber(L, static_cast<double>(macroSlot));
            return 2;
        }
        return 0; // a macro button whose macro is gone — see the file comment
    }

    if (type == Offsets::ACTION_TYPE_ITEM_BY_ID) {
        const uint32_t itemID = entry & Offsets::ACTION_PAYLOAD_MASK_ITEM_BY_ID;
        Game::Lua::PushString(L, "item");
        Game::Lua::PushNumber(L, static_cast<double>(itemID));
        return 2;
    }

    if (type == Offsets::ACTION_TYPE_EQUIPMENT_SET) {
        // 3.3.5 `Script_GetActionInfo` (`FUN_005a8f10`): the type and the
        // set's name.
        const EquipmentSet::Set *set = EquipmentSet::Data::FindByID(
            entry & ~static_cast<uint32_t>(Offsets::ACTION_TYPE_EQUIPMENT_SET));
        if (set == nullptr)
            return 0;
        Game::Lua::PushString(L, "equipmentset");
        Game::Lua::PushString(L, set->name.c_str());
        return 2;
    }

    // Unknown type tag — nothing, so callers don't get garbage.
    return 0;
}

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetActionInfo", &Script_GetActionInfo);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Action::Info
