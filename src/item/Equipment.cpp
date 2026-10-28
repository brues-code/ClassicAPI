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
#include "item/Arg.h"
#include "item/Cursor.h"
#include "item/ID.h"
#include "item/Location.h"
#include "item/Record.h"
#include "item/Swap.h"
#include "item/TypeInfo.h"
#include "spell/Known.h"

#include <cstdint>

namespace Item::Equipment {

namespace {

// `IsDualWielding()` — a port of 4.3.4's `Script_IsDualWielding`
// (0x00452A50): true iff the off-hand slot holds a weapon-class item and the
// player can dual wield. A shield or held item is not weapon class. 4.3.4's
// `CGItem_C::GetClassID` reads the cached record and yields 0 when it is
// missing; the passive peek here is the same, no load fired. Dual wield comes
// from Spell::Known::CanDualWield (see there for why not the engine's).
int __fastcall Script_IsDualWielding(void *L) {
    const uint8_t *item = Item::Location::ResolveEquipmentSlot(Offsets::INVSLOT_OFFHAND);
    const int itemID = item != nullptr ? Item::ID::FromCGItem(item) : 0;
    const uint8_t *record =
        itemID > 0 ? Item::PeekRecord(static_cast<uint32_t>(itemID)) : nullptr;
    Game::Lua::PushBool(L, record != nullptr &&
                               Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_CLASS) ==
                                   Offsets::ITEM_CLASS_WEAPON &&
                               Spell::Known::CanDualWield());
    return 1;
}

// `IsEquippableItem(item)` / `C_Item.IsEquippableItem(item)` — whether the
// item goes in some equipment slot. A port of 3.3.5's
// `Script_IsEquippableItem`: any non-zero `m_inventoryType` counts
// (INVTYPE_NON_EQUIP = 0), so bags, ammo, thrown weapons and relics do too.
//
// The argument is resolved as 3.3.5's `GetItemIDByName` does: a number, an
// `item:` string or link, or the name of a carried item (`IDForName` stands
// in for 3.3.5's `s_itemByName`). An uncached item is not equippable and
// fires no load — call `C_Item.RequestLoadItemDataByID` first if needed.
//
// The global keeps 3.3.5's 1/nil; `C_Item.IsEquippableItem` returns a bool.
bool IsEquippable(void *L) {
    const int itemID = Item::Location::IDForArg(L, 1, Offsets::INVMGR_SEARCH_BANK);
    const uint8_t *record =
        itemID > 0 ? Item::PeekRecord(static_cast<uint32_t>(itemID)) : nullptr;
    return record != nullptr &&
           Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE) != 0;
}

int __fastcall Script_IsEquippableItem(void *L) {
    if (IsEquippable(L))
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

int __fastcall Script_C_Item_IsEquippableItem(void *L) {
    Game::Lua::PushBool(L, IsEquippable(L));
    return 1;
}

// `IsEquippedItemType(type)` — 1 when an equipped item is of `type`, else
// nil. A port of 4.3.4's `Script_IsEquippedItemType` (0x008DAC20). `type`
// is first looked up as an inventory type: its key ("INVTYPE_SHIELD") or that
// key's global string ("Off Hand"), ignoring case. A match searches for an
// equipped item of that inventory type (`CanEquipInSlotCallback`); anything
// else is an item class or subclass name ("Weapon", "Shields", "Sword"),
// matched by `FindItemClassTypeCallback`. Both run on the engine's
// inventory search over the equipment slots, so a broken item is not
// counted. Every compare is Storm's SStrCmpI (`Game::StrCmpI`); 4.3.4 uses
// SStrCmpUTF8I for the localized names.
int __fastcall MatchesInvType(const uint8_t *item, void *ctx) {
    const int itemID = Item::ID::FromCGItem(item);
    const uint8_t *record =
        itemID > 0 ? Item::PeekRecord(static_cast<uint32_t>(itemID)) : nullptr;
    return record != nullptr &&
           Game::Read<int>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE) ==
               *static_cast<const int *>(ctx);
}

int __fastcall MatchesClassType(const uint8_t *item, void *ctx) {
    const char *type = static_cast<const char *>(ctx);
    const int itemID = Item::ID::FromCGItem(item);
    const uint8_t *record =
        itemID > 0 ? Item::PeekRecord(static_cast<uint32_t>(itemID)) : nullptr;
    if (record == nullptr)
        return 0;
    const uint32_t classID = Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_CLASS);
    if (Game::StrCmpI(Item::TypeInfo::ClassName(classID), type) == 0)
        return 1;
    const uint8_t *sub = Item::TypeInfo::FindSubClass(
        classID, Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_SUBCLASS));
    return sub != nullptr && Item::TypeInfo::SubClassNamed(sub, type);
}

int InvTypeForName(const char *type) {
    using GetText_t = const char *(__fastcall *)(const char *name, int edx, int gender);
    for (int invType = 1; invType <= Offsets::INVTYPE_TABLE_MAX_INDEX; ++invType) {
        const char *key = Item::TypeInfo::InvTypeKey(invType);
        if (key[0] == '\0')
            continue;
        if (Game::StrCmpI(type, key) == 0 ||
            Game::StrCmpI(type, reinterpret_cast<GetText_t>(
                                    Offsets::FUN_FRAMESCRIPT_GET_TEXT)(key, -1, 0)) == 0)
            return invType;
    }
    return 0;
}

int __fastcall Script_IsEquippedItemType(void *L) {
    const char *type = Game::Lua::IsString(L, 1) ? Game::Lua::ToString(L, 1) : nullptr;
    if (type == nullptr || type[0] == '\0') {
        Game::Lua::Error(L, "Usage: IsEquippedItemType(\"type\")");
        return 0;
    }
    int invType = InvTypeForName(type);
    const uint8_t *item =
        invType > 0
            ? Item::Location::Walk(&MatchesInvType, &invType, Offsets::INVMGR_SEARCH_EQUIPMENT)
            : Item::Location::Walk(&MatchesClassType, const_cast<char *>(type),
                                   Offsets::INVMGR_SEARCH_EQUIPMENT);
    if (item != nullptr)
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

// `C_Item.IsEquippedItem(item)` — true iff any character-pane equipment
// slot (1..19) currently holds an item matching `item`. Modern API
// accepts:
//   - itemID number → exact match against the equipped slot's itemID
//   - "item:N..." string → parse the link's first numeric field as the
//                          itemID and match the same way
//   - plain string → case-insensitive name match against the cached
//                    `m_name[0]` of each equipped item
//
// Returns false (never errors) for invalid input, an empty inventory,
// or an uncached match candidate. Walks slots in order and short-
// circuits on the first hit.
int __fastcall Script_C_Item_IsEquippedItem(void *L) {
    const auto arg = Item::Arg::Resolve(L, 1);
    if (arg.itemID <= 0 && arg.name == nullptr) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }

    // Equipment-first walk; itemID or decorated-name match via the shared
    // `Item::Location::MatchesArg` predicate (same logic the bag find-by-
    // name uses, so match semantics live in one place).
    for (int slot = Offsets::EQUIPMENT_SLOT_FIRST; slot <= Offsets::EQUIPMENT_SLOT_LAST; ++slot) {
        auto *item = Item::Location::ResolveEquipmentSlot(slot);
        if (item == nullptr) continue;
        if (Item::Location::MatchesArg(item, arg)) {
            Game::Lua::PushBoolean(L, 1);
            return 1;
        }
    }

    Game::Lua::PushBoolean(L, 0);
    return 1;
}

// `C_Item.EquipItemByName(item [, dstSlot])` — equips `item`.
//
// `item` is an item reference (itemID, `item:N`, a link, or the name of an
// item the player carries — the first bag match is equipped) or an item
// location (`{bagID=B, slotIndex=S}`, `{equipmentSlotIndex=N}`, an item
// GUID), which names one exact item. See
// `Item::Location::FindItemArgOrLocation`, shared with the item-use APIs;
// this call wants the source coordinates too, since the swap packet carries
// them.
//
// Two paths after a shared cursor-clear:
//
// - **Explicit `dstSlot`** (1..19): cursor-free direct swap via the engine's
//   `FUN_INVENTORY_SWAP` primitive — atomic server-side, no pickup.
//   `Item::Swap::FromBag` for an item in a bag, `FromPaperdoll` for one
//   already equipped (an `{equipmentSlotIndex=N}` location, which is how a
//   ring 11↔12 or weapon 16↔17 move is expressed).
//
// - **No `dstSlot`** (engine auto-picks slot from INVTYPE): cursor-
//   pickup + `AutoEquipCursorItem`, because 1.12's auto-pick logic
//   reads its slot decision off cursor state. An already-equipped item has
//   nothing to auto-pick, so that combination does nothing.
//
// Both paths start with `ClearCursor()` to clear any preexisting
// cursor state. If something was on the cursor, it gets returned
// to its source slot (visual lock cleared, source globals reset)
// before our work runs. Without this, the engine's swap primitive
// ends each call with a cursor-state cleanup that skips the
// item-flag clear (`FUN_00495190(0, 1)`); a held item would stay
// visually locked until a relog refreshed inventory state.
//
// The item is resolved AFTER that clear, so a location naming the slot a
// held item came from resolves to it (its slot reads empty while held) and a
// name search sees the returned item too. `Script_ClearCursor` is
// `FUN_00495190(1, 1)` and nothing else — it takes no arguments and reads no
// Lua stack — so our own arguments survive it.
//
// Returns nothing. Silently no-ops on bad/missing input, an item the player
// doesn't have, or the engine refusing the swap (combat lockdown,
// item-locked flag, type mismatch).
int __fastcall Script_C_Item_EquipItemByName(void *L) {
    const bool hasDstSlot = Game::Lua::IsNumber(L, 2);
    const int dstSlot = hasDstSlot ? static_cast<int>(Game::Lua::ToNumber(L, 2)) : 0;

    // Return any held cursor item to its slot BEFORE resolving the target.
    // FUN_INVENTORY_SWAP's end-of-call cleanup at FUN_00495190(0, 1) clears
    // LOCAL cursor globals but skips the visual-lock-flag clear
    // (`item+0x314 & ~1`) — so a held item would stay visually locked until
    // a relog refreshed state. ClearCursor's FUN_00495190(1, 1) includes it.
    reinterpret_cast<int(__fastcall *)(void *)>(
        Offsets::FUN_SCRIPT_CLEAR_CURSOR)(L);

    Item::Location::ByGUIDResult found;
    if (!Item::Location::FindItemArgOrLocation(L, 1, &found)) {
        return 0;
    }

    if (hasDstSlot) {
        if (found.equipmentSlotIndex != 0)
            Item::Swap::FromPaperdoll(found.item, found.equipmentSlotIndex, dstSlot);
        else
            Item::Swap::FromBag(found.item, found.bagID, found.slotIndex, dstSlot);
        return 0;
    }
    if (found.equipmentSlotIndex != 0)
        return 0; // already equipped — no slot for the engine to pick

    // Auto-slot path: pick the item up onto the cursor, then call the
    // engine's `CGPlayer::AutoEquipCursorItem` helper directly
    // (`Script_AutoEquipCursorItem` is a thin wrapper that just resolves
    // the local player and calls this with flag=0). The pickup goes through
    // the shared `Item::Cursor::PickupBagItem`, which drives the engine's
    // cursor primitive directly (no Script_PickupContainerItem stack-fake);
    // its empty-cursor guard is satisfied by the ClearCursor above. Bail if
    // the pickup didn't take (locked item / busy cursor) — nothing to equip.
    if (!Item::Cursor::PickupBagItem(L, found.bagID, found.slotIndex))
        return 0;

    using AutoEquipCursor_t = void (__thiscall *)(void *player, int flag);
    if (auto *player = Game::ResolveUnitToken("player")) {
        auto equip = reinterpret_cast<AutoEquipCursor_t>(
            Offsets::FUN_AUTO_EQUIP_CURSOR_ITEM);
        equip(player, 0);
    }
    return 0;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kIsDualWieldingRets[] = {
    Game::Doc::Req("isDualWielding", "bool",
                   "True when the player can dual wield and has a weapon in the off hand."),
};
const Game::Doc::Function kIsDualWielding{
    "Whether the player is holding a weapon in each hand.", {}, kIsDualWieldingRets,
    "ItemGlobals"};

const Game::Doc::Field kIsEquippableLegacyArgs[] = {
    Game::Doc::Req("item", "number", "An item ID, an item link, or the name of a carried item."),
};
const Game::Doc::Field kIsEquippableLegacyRets[] = {
    Game::Doc::Opt("isEquippable", "number", nullptr,
                   "1 when the item goes in an equipment slot, else nil."),
};
const Game::Doc::Function kIsEquippableItem{
    "Whether the item goes in an equipment slot, bag slots and the ammo slot included.",
    kIsEquippableLegacyArgs, kIsEquippableLegacyRets, "ItemGlobals"};

const Game::Doc::Field kIsEquippedTypeArgs[] = {
    Game::Doc::Req("type", "string",
                   "An equip location (\"INVTYPE_SHIELD\" or its name), an item class, or an "
                   "item subclass name."),
};
const Game::Doc::Field kIsEquippedTypeRets[] = {
    Game::Doc::Opt("isEquipped", "number", nullptr,
                   "1 when an equipped item that is not broken is of that type, else nil."),
};
const Game::Doc::Function kIsEquippedItemType{
    "Whether the player has an item of the given type equipped.", kIsEquippedTypeArgs,
    kIsEquippedTypeRets, "ItemGlobals"};

const Game::Doc::Field kIsEquippableArgs[] = {
    Game::Doc::Req("item", "ItemInfo", "An item ID, an item link, or the name of a carried item."),
};
const Game::Doc::Field kIsEquippableRets[] = {
    Game::Doc::Req("isEquippable", "bool", "True when the item goes in an equipment slot."),
};
const Game::Doc::Function kCIsEquippableItem{
    "Whether the item goes in an equipment slot, bag slots and the ammo slot included.",
    kIsEquippableArgs, kIsEquippableRets};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsDualWielding", &Script_IsDualWielding, &kIsDualWielding);
    Game::Lua::RegisterGlobalFunction("IsEquippableItem", &Script_IsEquippableItem,
                                      &kIsEquippableItem);
    Game::Lua::RegisterTableFunction("C_Item", "IsEquippableItem",
                                     &Script_C_Item_IsEquippableItem, &kCIsEquippableItem);
    Game::Lua::RegisterTableFunction("C_Item", "IsEquippedItem", &Script_C_Item_IsEquippedItem);
    Game::Lua::RegisterGlobalFunction("IsEquippedItemType", &Script_IsEquippedItemType,
                                      &kIsEquippedItemType);
    Game::Lua::RegisterTableFunction("C_Item", "EquipItemByName", &Script_C_Item_EquipItemByName);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::Equipment
