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
#include "item/Location.h"
#include "item/Spell.h"
#include "item/Use.h"

#include <cstdint>

namespace Container::Hearthstone {

namespace {

// Tests whether `itemID` is a hearthstone — either the vanilla one
// (6948) or a custom-server variant that reuses Hearthstone's
// on-use spell (8690). The itemID check is the fast path; the
// on-use-spell check requires the item's ItemStats record to be in
// the local cache, which is true for any item currently in the
// player's bags (the engine pre-fills the cache during bag sync).
bool IsHearthstoneItem(uint32_t itemID) {
    if (itemID == Offsets::HEARTHSTONE_ITEM_ID)
        return true;
    return Item::Spell::OnUseSpellIDForItemID(itemID) ==
           Offsets::HEARTHSTONE_SPELL_ID;
}

// Walks the player's bags 0..4 looking for a hearthstone-equivalent
// item. Returns the matched `CGItem *` and its itemID via out params,
// or `(nullptr, 0)` if none found. Stops on first match — we don't
// care about duplicates.
//
// The `outItemID` lets `PlayerHasHearthstone` return the actual
// matched itemID (which for a custom-server hearthstone won't be
// `HEARTHSTONE_ITEM_ID`). `outItem` is what `UseHearthstone` needs
// to hand to the engine's by-pointer use primitive.
//
// Each `ResolveBag` call clobbers Lua stack[1]/[2], but we own the
// stack inside our callback context. `GetBagSlotCount` is stack-free.
bool FindHearthstone(void *L, const uint8_t **outItem, int *outItemID) {
    for (int bag = 0; bag <= 4; bag++) {
        const int slotCount = Item::Location::GetBagSlotCount(bag);
        for (int slot = 1; slot <= slotCount; slot++) {
            const uint8_t *item = Item::Location::ResolveBag(L, bag, slot);
            if (item == nullptr)
                continue;
            const int itemID = Item::ID::FromCGItem(item);
            if (itemID > 0 && IsHearthstoneItem(static_cast<uint32_t>(itemID))) {
                *outItem = item;
                *outItemID = itemID;
                return true;
            }
        }
    }
    *outItem = nullptr;
    *outItemID = 0;
    return false;
}

// `C_Container.PlayerHasHearthstone()` — returns the itemID of any
// hearthstone-equivalent item in the player's bags (0..4), or `nil`
// if none found. "Hearthstone-equivalent" = itemID 6948 (vanilla) OR
// any item whose on-use spell is 8690 (catches custom-server
// hearthstone reskins; see `IsHearthstoneItem` above).
//
// Returns the **actual** matched itemID, not a hardcoded constant —
// matches modern WoW's behavior where multiple hearthstone-toy
// itemIDs are recognized and the specific one found gets returned.
//
// Walks the bags via `Item::Location::ResolveBag` — same path
// `C_Container.GetContainerItemID` uses internally. Stops on first
// match.
int __fastcall Script_C_Container_PlayerHasHearthstone(void *L) {
    const uint8_t *item = nullptr;
    int itemID = 0;
    if (!FindHearthstone(L, &item, &itemID))
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    return 1;
}

// `C_Container.UseHearthstone()` — finds and uses the hearthstone in
// the player's bags. Returns `true` on success (hearthstone found and
// the use call dispatched), `false` if no hearthstone is in bags.
//
// "Used" matches modern semantics — we don't probe whether the cast
// actually started (cooldown, in-combat, moving, etc. would fail
// downstream). The return is "did we have one to try with."
//
// Implementation: locate the hearthstone and hand the resulting CGItem
// pointer to the engine's by-pointer use primitive `FUN_ITEM_USE`.
// Same path `C_Item.UseItemByName` takes — see comments there for
// why we skip Script_UseContainerItem's cursor-mode dispatcher.
int __fastcall Script_C_Container_UseHearthstone(void *L) {
    const uint8_t *item = nullptr;
    int itemID = 0;
    if (!FindHearthstone(L, &item, &itemID)) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }

    Item::Use::UseItem(item, 0);

    Game::Lua::PushBoolean(L, 1);
    return 1;
}

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Container", "PlayerHasHearthstone",
                                     &Script_C_Container_PlayerHasHearthstone);
    Game::Lua::RegisterTableFunction("C_Container", "UseHearthstone",
                                     &Script_C_Container_UseHearthstone);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Container::Hearthstone
