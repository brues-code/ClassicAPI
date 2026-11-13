// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// `GetInventoryItemsForSlot(slot, returnTable [, transmogrify])` —
// walks the player's equipment and bags 0..4, and for each item
// whose `m_inventoryType` is compatible with `slot`, inserts
// `returnTable[location] = itemLink` where `location` is the packed
// ItemLocation bitfield (same encoding `C_EquipmentSet.GetItemLocations`
// uses).
//
// The compatibility check is a single bitwise AND against a static
// `invType → slotMask` table. 3.3.5 has the same table at
// `DAT_00A2D288` (read by its own `Script_GetInventoryItemsForSlot`
// at `0x005E95C0` via `FUN_007082B0`); we mirror it here with two
// vanilla-correct adjustments — 2H weapons / MAINHAND / OFFHAND
// types are confined to their literal slot rather than the looser
// "either hand" 3.3.5 allows (titanic-grip-era engine quirk that
// doesn't apply to 1.12).
//
// Bank items aren't included: vanilla's `GetItemBySlot` gates bank
// slots on a banker-GUID that's only set while the bank window is
// open, so walking them when closed returns nothing useful. Modern
// WoW behaves the same way (bank shows only when the window is
// open).
//
// The `transmogrify` arg is accepted and ignored. The stock 1.12
// client has no transmog system to query, and the modern flag's
// effect (broader mainhand/offhand cross-eligibility for visual
// swaps in retail) reduces to the regular equip check here. Some
// private servers (Turtle WoW etc.) layer their own transmog
// systems on top of vanilla; if any of those exposes its
// transmog-eligibility rules through addon-readable engine state,
// we'd plumb the flag through to it — for now we don't know of
// any such hook.

#include "Game.h"
#include "Offsets.h"
#include "equipmentset/Set.h"
#include "item/ID.h"
#include "item/Link.h"
#include "item/Location.h"
#include "item/Record.h"

#include <cstdint>
#include <vector>

namespace Item::InventoryItemsForSlot {

namespace {

// invType → slot bitmask. Bit N (0-based) means "this item type fits
// 1-based slot N+1". Values 0..28 cover every InventoryType
// vanilla's Item.dbc emits. Indices outside this range read as 0
// (no match) via the bounds check in `FitsSlot`.
constexpr uint32_t kSlotMaskByInvType[29] = {
    0x00000000, // 0  NON_EQUIP
    0x00000001, // 1  HEAD         → slot 1
    0x00000002, // 2  NECK         → slot 2
    0x00000004, // 3  SHOULDER     → slot 3
    0x00000008, // 4  BODY (shirt) → slot 4
    0x00000010, // 5  CHEST        → slot 5
    0x00000020, // 6  WAIST        → slot 6
    0x00000040, // 7  LEGS         → slot 7
    0x00000080, // 8  FEET         → slot 8
    0x00000100, // 9  WRIST        → slot 9
    0x00000200, // 10 HANDS        → slot 10
    0x00000C00, // 11 FINGER       → slots 11, 12
    0x00003000, // 12 TRINKET      → slots 13, 14
    0x00018000, // 13 WEAPON (1H)  → slots 16, 17 (dual-wield)
    0x00010000, // 14 SHIELD       → slot 17 (offhand)
    0x00020000, // 15 RANGED (bow) → slot 18
    0x00004000, // 16 CLOAK        → slot 15 (back)
    0x00008000, // 17 2HWEAPON     → slot 16 (mainhand only — vanilla)
    0x00780000, // 18 BAG          → slots 20..23 (bag slots)
    0x00040000, // 19 TABARD       → slot 19
    0x00000010, // 20 ROBE         → slot 5 (chest)
    0x00008000, // 21 WEAPONMAINHAND  → slot 16 (mainhand only)
    0x00010000, // 22 WEAPONOFFHAND   → slot 17 (offhand only)
    0x00010000, // 23 HOLDABLE     → slot 17 (offhand)
    0x00000000, // 24 AMMO (no equip slot in vanilla)
    0x00020000, // 25 THROWN       → slot 18
    0x00020000, // 26 RANGEDRIGHT  → slot 18 (gun/crossbow)
    0x00000000, // 27 QUIVER (no equip slot)
    0x00020000, // 28 RELIC        → slot 18
};

constexpr int kInvTypeMax = static_cast<int>(sizeof(kSlotMaskByInvType) /
                                              sizeof(kSlotMaskByInvType[0])) - 1;

bool FitsSlot(int slot1Based, uint32_t invType) {
    if (slot1Based < 1 || slot1Based > 32)
        return false;
    if (invType > static_cast<uint32_t>(kInvTypeMax))
        return false;
    return (kSlotMaskByInvType[invType] & (1u << (slot1Based - 1))) != 0;
}

// ── Proficiency gate ──────────────────────────────────────────────
// `FitsSlot` only checks invType→slot shape compatibility; without
// also gating on the player's proficiencies this would return (e.g.)
// mail helms for a warlock since both are `invType=HEAD`. Modern
// WoW's equipment-set picker filters by what the player can actually
// equip; we mirror that.
//
// Source of truth: the engine's per-itemClass proficiency bitmap at
// `[VAR_PROFICIENCY_TABLE]` (17 u32 entries indexed by `m_class`).
// SMSG_SET_PROFICIENCY's handler at `0x005E7B70` writes the bitmask
// for each class verbatim from the wire. Bit N of `table[class]` =
// "player can equip subclass N of this class". Same data the engine
// itself reads for the tooltip "Mail/Leather/Plate" red-text and the
// cursor-drop-on-paperdoll-slot check — covers both armor and weapon
// proficiencies in one lookup.
//
// 4.3.4's `Script_GetInventoryItemsForSlot` does the equivalent check
// via `FUN_00553AD0(itemClass) & (1 << subclass)`, reading from
// `DAT_00DD4D38` (its own copy of the same packet-fed table).

bool PlayerHasProficiency(uint32_t itemClass, uint32_t subclass) {
    if (itemClass > 16)
        return true; // table only covers 0..16; be permissive past that
    const uint32_t mask = *reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PROFICIENCY_TABLE) + itemClass * 4);
    if (mask == 0)
        return true; // table not populated yet (pre-SMSG_SET_PROFICIENCY)
                     // — fail open so empty-mask doesn't blank the list
    return (mask & (1u << subclass)) != 0;
}

bool PlayerCanEquip(const uint8_t *record) {
    if (record == nullptr)
        return true; // missing record — be permissive, don't hide the item
    const uint32_t classCode = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_CLASS);
    const uint32_t subclass = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_SUBCLASS);
    return PlayerHasProficiency(classCode, subclass);
}

const uint8_t *ItemRecordForCGItem(const uint8_t *cgItem) {
    const int itemID = Item::ID::FromCGItem(cgItem);
    if (itemID <= 0)
        return nullptr;
    return Item::PeekRecord(static_cast<uint32_t>(itemID));
}

uint32_t ReadInventoryType(const uint8_t *record) {
    if (record == nullptr)
        return 0;
    return *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);
}

struct Hit {
    int location;
    const uint8_t *cgItem;
};

void CollectIfFits(int slot, const uint8_t *cgItem, int location,
                   std::vector<Hit> &out) {
    if (cgItem == nullptr)
        return;
    const uint8_t *record = ItemRecordForCGItem(cgItem);
    if (!FitsSlot(slot, ReadInventoryType(record)))
        return;
    if (!PlayerCanEquip(record))
        return;
    out.push_back({location, cgItem});
}

// Registry key for anchoring `returnTable` across the bag-walk
// phase. `Item::Location::ResolveBag` internally calls
// `SetTop(L, 0)` to set up `PackBagSlot`'s expected stack shape, so
// the original arg at slot 2 doesn't survive the first bag deref.
// Stashing the table in the Lua registry keeps a stable handle on
// it until we rebuild the stack at the end.
constexpr const char *kRegistryKey = "_classicapi_GIIFS_returnTable";

int __fastcall Script_GetInventoryItemsForSlot(void *L) {
    if (!Game::Lua::IsNumber(L, 1) ||
        Game::Lua::Type(L, 2) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L,
            "Usage: GetInventoryItemsForSlot(slot, returnTable [, transmog])");
        return 0;
    }
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1));

    // Anchor the returnTable in the registry so it survives
    // ResolveBag's stack stomp.
    Game::Lua::PushString(L, kRegistryKey);
    Game::Lua::PushValue(L, 2);
    Game::Lua::SetTable(L, Game::Lua::REGISTRY_INDEX);

    std::vector<Hit> hits;

    if (slot >= Offsets::EQUIPMENT_SLOT_FIRST &&
        slot <= Offsets::EQUIPMENT_SLOT_LAST) {
        // Equipment paperdoll — ResolveEquipmentSlot doesn't touch
        // the Lua stack, but we still collect into the vector for a
        // uniform two-pass shape.
        for (int s = Offsets::EQUIPMENT_SLOT_FIRST;
             s <= Offsets::EQUIPMENT_SLOT_LAST; ++s) {
            CollectIfFits(slot, Item::Location::ResolveEquipmentSlot(s),
                          EquipmentSet::PackEquipped(s), hits);
        }
        // Bags 0..4 (backpack + 4 equipped bags). ResolveBag clobbers
        // the stack on every call — that's why we collect first and
        // push to the table after.
        for (int bag = 0; bag <= 4; ++bag) {
            const int slotCount = Item::Location::GetBagSlotCount(bag);
            for (int bs = 1; bs <= slotCount; ++bs) {
                CollectIfFits(slot, Item::Location::ResolveBag(L, bag, bs),
                              EquipmentSet::PackBag(bag, bs), hits);
            }
        }
    }

    // Rebuild stack: empty it, retrieve the anchored table, clear
    // the registry slot, then populate.
    Game::Lua::SetTop(L, 0);
    Game::Lua::PushString(L, kRegistryKey);
    Game::Lua::GetTable(L, Game::Lua::REGISTRY_INDEX);
    // Stack: [table]
    Game::Lua::PushString(L, kRegistryKey);
    Game::Lua::PushNil(L);
    Game::Lua::SetTable(L, Game::Lua::REGISTRY_INDEX);
    // Stack: [table]

    for (const auto &hit : hits) {
        const char *link = Item::Link::FromCGItem(hit.cgItem);
        if (link == nullptr)
            continue;
        Game::Lua::PushNumber(L, static_cast<double>(hit.location));
        Game::Lua::PushString(L, link);
        Game::Lua::SetTable(L, 1);
    }
    return 1;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("slot", "luaIndex", "An equipment slot, from 1 to 19."),
    Game::Doc::Req("returnTable", "table", "Gets the items added; nothing is removed from it."),
    Game::Doc::Opt("transmogrify", "bool", nullptr, "Has no effect."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("returnTable", "table",
                   "The same table, with item links keyed by packed item location."),
};
const Game::Doc::Function kGetInventoryItemsForSlot{
    "Adds each worn or carried item that the player can equip in the slot to a table; the bank "
    "is not searched.",
    kArgs, kRets, "ItemGlobals"};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetInventoryItemsForSlot",
                                       &Script_GetInventoryItemsForSlot,
                                       &kGetInventoryItemsForSlot);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::InventoryItemsForSlot
