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

#include "Location.h"

#include "../Game.h"
#include "../Offsets.h"
#include "../guid/Guid.h"
#include "../object/Resolve.h"
#include "../unit/Identity.h"
#include "Arg.h"
#include "CGItem.h"
#include "ID.h"
#include "Link.h"
#include "Record.h"

#include <cstring>

namespace Item::Location {

namespace {

using GetItemBySlot_t = void *(__thiscall *)(void *thisInvMgr, int slot);
using PackBagSlot_t = int(__fastcall *)(void *L, void **outInvMgr, int *outLinearSlot,
                                         int *outUnused);

// Reads `loc.fieldName` and returns it as an int. Returns false via the
// boolean result if the field is missing or non-numeric. Always leaves the
// Lua stack as it found it.
bool TryReadIntField(void *L, int locIdx, const char *fieldName, int *out) {
    Game::Lua::PushString(L, fieldName);
    Game::Lua::GetTable(L, locIdx); // pops key, pushes value
    const bool ok = Game::Lua::IsNumber(L, -1);
    if (ok)
        *out = static_cast<int>(Game::Lua::ToNumber(L, -1));
    Game::Lua::SetTop(L, -2); // pop value (or nil)
    return ok;
}

// PackBagSlot + GetItemBySlot on the (bagID, slotIndex) already at Lua
// stack[1] / stack[2]. PackBagSlot only `lua_tonumber`s those two indices
// (0x004F9820), so the stack is left as it was.
bool DetailFromArgs(void *L, BagSlotDetail *out) {
    void *container = nullptr;
    int linearSlot = 0;
    int unused = 0;
    auto PackBagSlot = reinterpret_cast<PackBagSlot_t>(Offsets::FUN_PACK_BAG_SLOT);
    if (!PackBagSlot(L, &container, &linearSlot, &unused) || container == nullptr)
        return false;

    auto GetItemBySlot = reinterpret_cast<GetItemBySlot_t>(
        Offsets::FUN_ITEMMGR_GET_ITEM_BY_SLOT);
    out->container = static_cast<const uint8_t *>(container);
    out->linearSlot = linearSlot;
    out->item = static_cast<const uint8_t *>(GetItemBySlot(container, linearSlot));
    return true;
}

const uint8_t *ResolveBagSlot(void *L, int bagID, int slotIndex) {
    BagSlotDetail d;
    if (!ResolveBagSlotDetail(L, bagID, slotIndex, &d))
        return nullptr;
    return d.item;
}

} // namespace

const uint8_t *ResolveEquipmentSlot(int slot1Based) {
    void *invMgr = const_cast<uint8_t *>(Unit::Identity::PlayerInventoryManager());
    if (invMgr == nullptr)
        return nullptr;
    // GetItemBySlot expects 0-based linearized slot indices. The built-in
    // Lua functions all do `dec eax` on the slot argument before calling —
    // see helper at 0x004C8520.
    auto GetItemBySlot = reinterpret_cast<GetItemBySlot_t>(
        Offsets::FUN_ITEMMGR_GET_ITEM_BY_SLOT);
    return static_cast<const uint8_t *>(GetItemBySlot(invMgr, slot1Based - 1));
}

void *ContainerInventory(const uint8_t *container) {
    if (container == nullptr)
        return nullptr;
    auto *vtable = *reinterpret_cast<const uint8_t *const *>(container);
    using GetInventory_t = void *(__thiscall *)(void *container);
    auto getInventory = *reinterpret_cast<const GetInventory_t *>(
        vtable + Offsets::OFF_CONTAINER_GET_INVENTORY);
    return getInventory(const_cast<uint8_t *>(container));
}

const uint8_t *BagContainer(int bagID) {
    if (bagID < Offsets::BAG_INDEX_BAG_FIRST || bagID > Offsets::BAG_INDEX_BANK_BAG_LAST)
        return nullptr;
    using GetBagGuid_t = uint64_t(__fastcall *)(uint32_t bagIndex0);
    auto getBagGuid =
        reinterpret_cast<GetBagGuid_t>(Offsets::FUN_GET_EQUIPPED_BAG_GUID);
    const uint64_t bagGuid = getBagGuid(static_cast<uint32_t>(bagID - 1));
    if (bagGuid == 0)
        return nullptr; // no bag there, or a bank bag with the bank closed
    return static_cast<const uint8_t *>(
        Object::ByGuid(Offsets::TYPEMASK_CONTAINER, bagGuid, "ItemMgr", 0x172));
}

void *BagInventory(int bagID) {
    return ContainerInventory(BagContainer(bagID));
}

// Backpack is fixed at 16, equipped bags read `m_containerSlots` off the
// cache record. Returns 0 for empty bag slots or out-of-range bagIDs.
int GetBagSlotCount(int bagID) {
    if (bagID == 0)
        return Offsets::BACKPACK_NUM_SLOTS;
    if (bagID < 1 || bagID > 4)
        return 0;
    auto *bagItem = ResolveEquipmentSlot(Offsets::INVSLOT_BAG1 + bagID - 1);
    if (bagItem == nullptr)
        return 0;
    const int bagItemID = Item::ID::FromCGItem(bagItem);
    if (bagItemID == 0)
        return 0;
    auto *record = Item::PeekRecord(static_cast<uint32_t>(bagItemID));
    if (record == nullptr)
        return 0;
    return static_cast<int>(*reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_CONTAINER_SLOTS));
}

bool ResolveBagSlotDetail(void *L, int bagID, int slotIndex, BagSlotDetail *out) {
    // PackBagSlot reads bagID/slotIndex from Lua stack[1] and stack[2] (it's
    // designed to be called from a Lua-callback context like
    // Script_GetContainerItemInfo). Replace the stack so positions 1 and 2
    // hold the raw values for PackBagSlot's internal lua_tonumber reads.
    Game::Lua::SetTop(L, 0);
    Game::Lua::PushNumber(L, static_cast<double>(bagID));
    Game::Lua::PushNumber(L, static_cast<double>(slotIndex));
    return DetailFromArgs(L, out);
}

const uint8_t *ResolveBag(void *L, int bagID, int slotIndex) {
    return ResolveBagSlot(L, bagID, slotIndex);
}

const uint8_t *ResolveBagArgs(void *L) {
    BagSlotDetail d;
    return DetailFromArgs(L, &d) ? d.item : nullptr;
}

bool ParseGUIDString(const char *s, uint64_t *out) {
    return Guid::Parse(s, out);
}

const uint8_t *ResolveByGUID(uint64_t guid) {
    if (guid == 0)
        return nullptr;
    return static_cast<const uint8_t *>(
        Object::ByGuid(Offsets::TYPEMASK_ITEM, guid, "ItemMgr", 0x172));
}

bool FindByItemID(void *L, int itemID, ByGUIDResult *out) {
    if (itemID <= 0)
        return false;
    Item::Arg::Resolved arg{};
    arg.itemID = itemID;
    arg.name = nullptr;
    return FindByArg(L, arg, out);
}

bool MatchesArg(const uint8_t *cgItem, const Item::Arg::Resolved &arg) {
    const int id = Item::ID::FromCGItem(cgItem);
    if (id == 0)
        return false;
    if (arg.itemID > 0)
        return id == arg.itemID;
    if (arg.name == nullptr)
        return false;
    // Compare against the item's *decorated* display name (random suffix
    // included) — modern IsEquippedItem / find-by-name match the full name
    // only ("Foo of the Owl", not the base "Foo"; verified on the modern
    // client). NameFromCGItem falls back to the base name for unsuffixed
    // items, so plain items still match; uncached/unnameable → no match.
    char name[128];
    if (!Item::Link::NameFromCGItem(cgItem, name, sizeof(name)))
        return false;
    return _stricmp(name, arg.name) == 0;
}

namespace {

int __fastcall MatchesName(const uint8_t *item, void *ctx) {
    return MatchesArg(item, *static_cast<const Item::Arg::Resolved *>(ctx)) ? 1 : 0;
}

} // namespace

const uint8_t *Walk(WalkCallback cb, void *ctx, uint32_t flags) {
    using Walk_t = const uint8_t *(__thiscall *)(const void *invMgr, WalkCallback cb,
                                                 void *ctx, uint32_t flags);
    const uint8_t *invMgr = Unit::Identity::PlayerInventoryManager();
    if (invMgr == nullptr)
        return nullptr;
    return reinterpret_cast<Walk_t>(Offsets::FUN_INVMGR_WALK)(invMgr, cb, ctx, flags);
}

int IDForName(const char *name, uint32_t flags) {
    if (name == nullptr || name[0] == '\0')
        return 0;
    Item::Arg::Resolved arg{0, 0, name};
    const uint8_t *item = Walk(&MatchesName, &arg, flags);
    return item != nullptr ? Item::ID::FromCGItem(item) : 0;
}

int IDForArg(void *L, int idx, uint32_t flags) {
    const Item::Arg::Resolved arg = Item::Arg::Resolve(L, idx);
    return arg.itemID > 0 ? arg.itemID : IDForName(arg.name, flags);
}

bool FindByArgInBags(void *L, const Item::Arg::Resolved &arg, ByGUIDResult *out) {
    if (arg.itemID <= 0 && arg.name == nullptr)
        return false;

    for (int bagID = 0; bagID <= 4; ++bagID) {
        const int slotCount = GetBagSlotCount(bagID);
        for (int slotIndex = 1; slotIndex <= slotCount; ++slotIndex) {
            auto *item = ResolveBag(L, bagID, slotIndex);
            if (item == nullptr)
                continue;
            if (!MatchesArg(item, arg))
                continue;

            out->equipmentSlotIndex = 0;
            out->bagID = bagID;
            out->slotIndex = slotIndex;
            out->item = item;
            return true;
        }
    }
    return false;
}

bool FindByArg(void *L, const Item::Arg::Resolved &arg, ByGUIDResult *out) {
    if (arg.itemID <= 0 && arg.name == nullptr)
        return false;

    // Equipment slots first (no Lua stack interaction, cheaper), matching
    // `FindByItemID`'s equipment-before-bags order; then fall through to the
    // bag walk. Both compare with the shared `MatchesArg` predicate.
    for (int slot = Offsets::EQUIPMENT_SLOT_FIRST;
         slot <= Offsets::EQUIPMENT_SLOT_LAST; ++slot) {
        auto *item = ResolveEquipmentSlot(slot);
        if (item != nullptr && MatchesArg(item, arg)) {
            out->equipmentSlotIndex = slot;
            out->bagID = 0;
            out->slotIndex = 0;
            out->item = item;
            return true;
        }
    }
    return FindByArgInBags(L, arg, out);
}

const uint8_t *ResolveBagSlotNoLua(int bagID, int slotIndex) {
    if (slotIndex < 1)
        return nullptr;
    auto GetItemBySlot = reinterpret_cast<GetItemBySlot_t>(
        Offsets::FUN_ITEMMGR_GET_ITEM_BY_SLOT);
    if (bagID == 0) {
        if (slotIndex > Offsets::BACKPACK_NUM_SLOTS)
            return nullptr;
        void *invMgr = const_cast<uint8_t *>(Unit::Identity::PlayerInventoryManager());
        if (invMgr == nullptr)
            return nullptr;
        return static_cast<const uint8_t *>(
            GetItemBySlot(invMgr, Offsets::BACKPACK_LINEAR_BASE + slotIndex - 1));
    }
    void *bagInv = BagInventory(bagID);
    if (bagInv == nullptr)
        return nullptr;
    const int slotCount = static_cast<int>(*reinterpret_cast<const uint32_t *>(bagInv));
    if (slotIndex > slotCount)
        return nullptr;
    return static_cast<const uint8_t *>(GetItemBySlot(bagInv, slotIndex - 1));
}

bool FindByArgNoLua(const Item::Arg::Resolved &arg, ByGUIDResult *out) {
    if (arg.itemID <= 0 && arg.name == nullptr)
        return false;

    for (int slot = Offsets::EQUIPMENT_SLOT_FIRST;
         slot <= Offsets::EQUIPMENT_SLOT_LAST; ++slot) {
        auto *item = ResolveEquipmentSlot(slot);
        if (item != nullptr && MatchesArg(item, arg)) {
            out->equipmentSlotIndex = slot;
            out->bagID = 0;
            out->slotIndex = 0;
            out->item = item;
            return true;
        }
    }
    for (int bagID = 0; bagID <= 4; ++bagID) {
        const int slotCount = GetBagSlotCount(bagID);
        for (int slotIndex = 1; slotIndex <= slotCount; ++slotIndex) {
            auto *item = ResolveBagSlotNoLua(bagID, slotIndex);
            if (item == nullptr || !MatchesArg(item, arg))
                continue;
            out->equipmentSlotIndex = 0;
            out->bagID = bagID;
            out->slotIndex = slotIndex;
            out->item = item;
            return true;
        }
    }
    return false;
}

bool BankerOpen() {
    return Game::Read<uint64_t>(Offsets::VAR_BANK_GATE_GUID) != 0;
}

namespace {

// 1-based slot of `guid` in an inventory list's GUID array, or 0.
int SlotInList(const uint8_t *invMgr, uint64_t guid) {
    if (invMgr == nullptr)
        return 0;
    const int count = static_cast<int>(
        Game::Read<uint32_t>(invMgr, Offsets::OFF_INVMGR_SLOT_COUNT));
    auto *guids = Game::Read<const uint64_t *>(invMgr, Offsets::OFF_INVMGR_GUID_ARRAY);
    if (guids == nullptr)
        return 0;
    for (int i = 0; i < count; ++i)
        if (guids[i] == guid)
            return i + 1;
    return 0;
}

// 1-based slot of `guid` inside the bag whose GUID is `bagGuid`, or 0.
int SlotInBag(uint64_t bagGuid, uint64_t guid) {
    if (bagGuid == 0)
        return 0;
    const auto *bag = static_cast<const uint8_t *>(
        Object::ByGuid(Offsets::TYPEMASK_CONTAINER, bagGuid, "ItemMgr", 0x172));
    return SlotInList(static_cast<const uint8_t *>(ContainerInventory(bag)), guid);
}

} // namespace

bool FindByGUID(uint64_t guid, bool includeBank, ByGUIDResult *out) {
    *out = ByGUIDResult{};
    if (guid == 0)
        return false;
    const uint8_t *invMgr = Unit::Identity::PlayerInventoryManager();
    if (invMgr == nullptr)
        return false;
    auto *guids = Game::Read<const uint64_t *>(invMgr, Offsets::OFF_INVMGR_GUID_ARRAY);
    if (guids == nullptr)
        return false;

    // The player's own list, read the way FUN_INVMGR_WALK reads it, as one
    // linear array: equipment and bag slots, backpack, bank, bank bag
    // slots, buyback, keyring (Offsets.h, "Slot ranges").
    const int linear = SlotInList(invMgr, guid) - 1;
    if (linear >= 0) {
        const bool bankRange = linear >= Offsets::INVMGR_BANK_MAIN_FIRST_SLOT &&
                               linear <= Offsets::INVMGR_BANK_BAG_LAST_SLOT;
        if (bankRange && !includeBank)
            return false;
        if (linear >= Offsets::INVMGR_BACKPACK_FIRST_SLOT &&
            linear <= Offsets::INVMGR_BACKPACK_LAST_SLOT) {
            out->place = PLACE_BAGS;
            out->bagID = Offsets::BAG_INDEX_BACKPACK;
            out->slotIndex = linear - Offsets::INVMGR_BACKPACK_FIRST_SLOT + 1;
        } else if (linear >= Offsets::INVMGR_BANK_MAIN_FIRST_SLOT &&
                   linear <= Offsets::INVMGR_BANK_MAIN_LAST_SLOT) {
            out->place = PLACE_BANK;
            out->bagID = Offsets::BAG_INDEX_BANK;
            out->slotIndex = linear - Offsets::INVMGR_BANK_MAIN_FIRST_SLOT + 1;
        } else if (linear >= Offsets::INVMGR_BUYBACK_FIRST_SLOT &&
                   linear <= Offsets::INVMGR_BUYBACK_LAST_SLOT) {
            out->place = PLACE_BUYBACK; // sold: no location names it
        } else if (linear >= Offsets::INVMGR_KEYRING_FIRST_SLOT) {
            out->place = PLACE_KEYRING;
            out->bagID = Offsets::BAG_INDEX_KEYRING;
            out->slotIndex = linear - Offsets::INVMGR_KEYRING_FIRST_SLOT + 1;
        } else {
            // Equipment 0..18, the equipped bags 19..22, the bank bags
            // 63..68: an inventory slot, `linear + 1` (INVSLOT_BAG1 = 20).
            out->place = bankRange ? PLACE_BANK : PLACE_EQUIPPED;
            out->equipmentSlotIndex = linear + 1;
        }
        out->item = ResolveByGUID(guid);
        return out->item != nullptr;
    }

    // Bag contents: each equipped bag (bagIDs 1..4) and bank bag (5..10)
    // keeps its own list, reached through the bag's container object.
    auto inBag = [&](int bagID, uint64_t bagGuid, Place place) {
        const int slot = SlotInBag(bagGuid, guid);
        if (slot == 0)
            return false;
        out->place = place;
        out->bagID = bagID;
        out->slotIndex = slot;
        out->item = ResolveByGUID(guid);
        return out->item != nullptr;
    };
    for (int bagID = Offsets::BAG_INDEX_BAG_FIRST; bagID <= Offsets::BAG_INDEX_BAG_LAST;
         ++bagID)
        if (inBag(bagID, guids[Offsets::INVSLOT_BAG1 - 1 + bagID - 1], PLACE_BAGS))
            return true;
    if (!includeBank)
        return false;
    for (int bagID = Offsets::BAG_INDEX_BANK_BAG_FIRST;
         bagID <= Offsets::BAG_INDEX_BANK_BAG_LAST; ++bagID)
        if (inBag(bagID,
                  guids[Offsets::INVMGR_BANK_BAG_FIRST_SLOT + bagID -
                        Offsets::BAG_INDEX_BANK_BAG_FIRST],
                  PLACE_BANK))
            return true;
    return false;
}

bool FindLocationByGUID(uint64_t guid, ByGUIDResult *out) {
    if (FindByGUID(guid, BankerOpen(), out) && out->place != PLACE_BUYBACK)
        return true;
    *out = ByGUIDResult{};
    return false;
}

bool IsLocationArg(void *L, int idx) {
    const int t = Game::Lua::Type(L, idx);
    return t == Game::Lua::TYPE_TABLE || t == Game::Lua::TYPE_STRING;
}

// The location forms, reporting WHERE the item is as well as which one.
// `Resolve` is this minus the coordinates; the argument resolvers below add
// the item-reference forms on top.
static bool ResolveLocationDetail(void *L, int locIdx, ByGUIDResult *out) {
    *out = ByGUIDResult{};
    const int t = Game::Lua::Type(L, locIdx);

    if (t == Game::Lua::TYPE_STRING) {
        uint64_t guid = 0;
        if (!ParseGUIDString(Game::Lua::ToString(L, locIdx), &guid))
            return false;
        return FindLocationByGUID(guid, out);
    }

    if (t != Game::Lua::TYPE_TABLE)
        return false;

    int eqSlot = 0;
    if (TryReadIntField(L, locIdx, "equipmentSlotIndex", &eqSlot)) {
        out->equipmentSlotIndex = eqSlot;
        out->item = ResolveEquipmentSlot(eqSlot);
        return out->item != nullptr;
    }

    int bagID = 0, slotIndex = 0;
    if (TryReadIntField(L, locIdx, "bagID", &bagID) &&
        TryReadIntField(L, locIdx, "slotIndex", &slotIndex)) {
        out->bagID = bagID;
        out->slotIndex = slotIndex;
        out->item = ResolveBagSlot(L, bagID, slotIndex);
        return out->item != nullptr;
    }

    return false;
}

const uint8_t *Resolve(void *L, int locIdx) {
    // A GUID names the item itself, so it resolves wherever the player
    // holds it — buyback included, which no location can name.
    if (Game::Lua::Type(L, locIdx) == Game::Lua::TYPE_STRING) {
        uint64_t guid = 0;
        if (!ParseGUIDString(Game::Lua::ToString(L, locIdx), &guid))
            return nullptr;
        ByGUIDResult found;
        return FindByGUID(guid, BankerOpen(), &found) ? found.item : nullptr;
    }
    ByGUIDResult found;
    return ResolveLocationDetail(L, locIdx, &found) ? found.item : nullptr;
}

bool FindItemArgOrLocation(void *L, int idx, ByGUIDResult *out) {
    *out = ByGUIDResult{};
    const int t = Game::Lua::Type(L, idx);

    // A table is only ever a location, and so is a string that parses as an
    // item GUID — a strict `0x` + 16 hex digits, which no item link or item
    // name can look like. Decide from the argument alone, and commit: a
    // location resolve STOMPS the Lua stack (`PackBagSlot` overwrites the
    // first stack slots with its own arguments), so falling back to the
    // reference forms after one has run would re-read the argument as
    // whatever it left behind — a bag index read as an itemID, which names
    // an unrelated item.
    if (t == Game::Lua::TYPE_TABLE)
        return ResolveLocationDetail(L, idx, out);
    if (t == Game::Lua::TYPE_STRING) {
        uint64_t guid = 0;
        if (ParseGUIDString(Game::Lua::ToString(L, idx), &guid))
            return FindLocationByGUID(guid, out);
    }

    const Item::Arg::Resolved arg = Item::Arg::Resolve(L, idx);
    if (arg.itemID <= 0 && arg.name == nullptr)
        return false;
    return FindByArgInBags(L, arg, out);
}

const uint8_t *ResolveItemArgOrLocation(void *L, int idx) {
    ByGUIDResult found;
    return FindItemArgOrLocation(L, idx, &found) ? found.item : nullptr;
}

} // namespace Item::Location
