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
// Returns `actionType, id, subType` for the slot, or nil if empty.
//
// The vanilla 1.12.1 engine stores action descriptors as packed
// uint32s in `VAR_ACTION_TABLE` (`0x00BC6980`, 120 slots). Top 4 bits
// are the type tag; the `0x40000000` tag is ambiguous between
// macros and bag-items and is disambiguated by checking the per-
// character macro-slot map at `VAR_MACRO_SLOT_MAP` (36 entries).
//
//   empty slot:      nil
//   spell action:    "spell", spellID, "spell"
//   macro:           "macro", macroIndex (1-based)
//   bag item:        "item", nil           (see note below)
//   item-by-id:      "item", itemID
//
// The subType is always "spell" for spell-type entries — the engine
// helper hardcodes pet=0 for entries on this table. Pet-bar actions
// live in a separate table; that's `PetHasActionBar` + the pet-action
// helpers, not this function.
//
// The macro discriminator is the same trick SuperWoWhook uses in its
// modified `GetActionText`: walk the 36-entry macro-slot map and check
// whether `entry & 0xBFFFFFFF` matches one of those macro IDs. If yes,
// the slot holds a macro; if no, it's a bag-item.
//
// **Bag-item itemID note.** For real bag items (top-4 bits = `0x4`
// and the payload isn't in the macro-slot map), the engine stores a
// bag-instance key, not the itemID. Resolving to itemID requires
// walking the per-character action-item hash table at `[0x00BDCC54]`
// and reading a struct field whose offset we haven't fully derived
// yet. For now we report the action *type* ("item") but return nil
// for the id. Callers that need it can fall back to
// `GetActionTexture` + Lua-side item lookups.

#include "Game.h"
#include "Offsets.h"
#include "action/Slot.h"

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
    if (slot0 < 0 || slot0 >= Offsets::ACTION_TABLE_MAX_SLOTS) {
        Game::Lua::PushNil(L);
        return 1;
    }

    const uint32_t entry = ReadEntry(slot0);
    if (entry == 0) {
        Game::Lua::PushNil(L);
        return 1;
    }

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
        if (spellID == 0) {
            Game::Lua::PushNil(L);
            return 1;
        }
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
        // Not a macro — it's a bag item. We know the type but extracting
        // the itemID from the bag-instance hash needs more reversing;
        // see the file-level comment.
        Game::Lua::PushString(L, "item");
        Game::Lua::PushNil(L);
        return 2;
    }

    if (type == Offsets::ACTION_TYPE_ITEM_BY_ID) {
        const uint32_t itemID = entry & Offsets::ACTION_PAYLOAD_MASK_ITEM_BY_ID;
        Game::Lua::PushString(L, "item");
        Game::Lua::PushNumber(L, static_cast<double>(itemID));
        return 2;
    }

    // Unknown type tag — surface as nil so callers don't get garbage.
    Game::Lua::PushNil(L);
    return 1;
}

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetActionInfo", &Script_GetActionInfo);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Action::Info
