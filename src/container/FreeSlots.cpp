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
#include "item/BagFamily.h"
#include "item/ID.h"
#include "item/Location.h"
#include "item/Record.h"

#include <cstdint>
#include <vector>

namespace Container::FreeSlots {

namespace {

// Resolves the bag's metadata: total slot count + BagFamily bitfield.
//
// - bagID 0 (backpack) → (16, 0). Vanilla 1.12 backpack is fixed at 16
//   slots and unfamilied (0). 3.3.5's `Script_GetContainerNumFreeSlots`
//   hardcodes both values for the backpack path too.
// - bagID 1..4 → looks up the equipped bag at INVSLOT_BAGn, reads
//   `m_containerSlots` and `m_bagFamily` from the cached `ItemStats_C`
//   record. If no bag equipped or item not cached: (0, 0).
// - other bagIDs (bank, keyring) → (0, 0). Not supported in this
//   implementation.
struct BagInfo {
    int slotCount;
    int bagType;
};

BagInfo ResolveBagInfo(int bagID) {
    if (bagID == 0)
        return {Offsets::BACKPACK_NUM_SLOTS, 0};
    if (bagID < 1 || bagID > 4)
        return {0, 0};

    const int equipSlot = Offsets::INVSLOT_BAG1 + bagID - 1;
    auto *bagItem = Item::Location::ResolveEquipmentSlot(equipSlot);
    if (bagItem == nullptr)
        return {0, 0};

    const int itemID = Item::ID::FromCGItem(bagItem);
    if (itemID == 0)
        return {0, 0};

    auto *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr)
        return {0, 0};

    const int slots = static_cast<int>(*reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_CONTAINER_SLOTS));
    // BagFamily bitmask: the item's field if populated, else derived from
    // the container subclass — Turtle leaves bags' m_bagFamily at 0, so a
    // Soul Bag would otherwise report family 0 and get miscounted as a
    // general-purpose bag by CalculateTotalNumberOfFreeBagSlots. Shared
    // with C_Item.GetItemFamily so a bag and its contents agree. See
    // `Item::BagFamily`.
    const int family =
        static_cast<int>(Item::BagFamily::BitmaskForRecord(record));
    return {slots, family};
}

// Walks the bag and counts empty slots. Each `Item::Location::ResolveBag`
// call stomps Lua stack[1]/[2] (a requirement of the engine's
// `PackBagSlot` helper), but we own the stack here — Lua just called us
// — and the caller's bagID arg has already been read into a local. So
// it's safe to repeatedly clobber the stack across iterations.
int CountFreeSlotsInBag(void *L, int bagID, int slotCount) {
    int free = 0;
    for (int slot = 1; slot <= slotCount; slot++) {
        if (Item::Location::ResolveBag(L, bagID, slot) == nullptr)
            free++;
    }
    return free;
}

// `C_Container.GetContainerNumFreeSlots(bagID)` — returns
// `(numberOfFreeSlots, bagType)`. `bagID` is 0..4 (0 = backpack,
// 1..4 = equipped bag slots). For invalid or unsupported bag IDs
// (negative bank/keyring indices, out-of-range values), returns
// `(0, 0)` — matching 3.3.5's behavior of always pushing two values
// rather than nil/nothing.
//
// `bagType` is the `BagFamily` bitmask in modern encoding. Bags leave the
// raw family field empty in Turtle's data (Soul/Herb/quiver/ammo pouch/…
// all read 0), so it's derived from the bag's class + subclass via
// `Item::BagFamily`: a Soul Bag reports `0x4`, a quiver `0x1`, an ammo
// pouch `0x2` — matching what `C_Item.GetItemFamily` reports for both the
// bag and the items it holds. See that helper for the Turtle custom-family
// caveat (Meat/Fish/Leather/Mining, IDs 10–13).
int __fastcall Script_C_Container_GetContainerNumFreeSlots(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L,
            "Usage: C_Container.GetContainerNumFreeSlots(bagID)");
        return 0;
    }
    const int bagID = static_cast<int>(Game::Lua::ToNumber(L, 1));

    const BagInfo info = ResolveBagInfo(bagID);
    const int free = (info.slotCount > 0) ? CountFreeSlotsInBag(L, bagID, info.slotCount) : 0;

    Game::Lua::PushNumber(L, static_cast<double>(free));
    Game::Lua::PushNumber(L, static_cast<double>(info.bagType));
    return 2;
}

// `C_Container.GetContainerFreeSlots(bagID)` — which slots are empty,
// as an array of slot numbers in ascending order, rather than
// `GetContainerNumFreeSlots`'s count of them. A full bag returns an
// empty table; a bag that isn't there returns nothing, which is how
// callers tell "no free slots" from "no such bag".
//
// `bagID` is 0..4 like the rest of this file (0 = backpack, 1..4 =
// equipped bag slots); bank and keyring indices are the "no such bag"
// case.
int __fastcall Script_C_Container_GetContainerFreeSlots(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L,
            "Usage: C_Container.GetContainerFreeSlots(bagID)");
        return 0;
    }
    const int bagID = static_cast<int>(Game::Lua::ToNumber(L, 1));

    const BagInfo info = ResolveBagInfo(bagID);
    if (info.slotCount <= 0)
        return 0; // nothing — no such bag

    // Collect before building the table. Each `ResolveBag` stomps stack
    // slots 1 and 2, so the table cannot be on the stack during the walk.
    std::vector<int> freeSlots;
    freeSlots.reserve(static_cast<size_t>(info.slotCount));
    for (int slot = 1; slot <= info.slotCount; ++slot) {
        if (Item::Location::ResolveBag(L, bagID, slot) == nullptr)
            freeSlots.push_back(slot);
    }

    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    for (size_t i = 0; i < freeSlots.size(); ++i) {
        Game::Lua::PushNumber(L, static_cast<double>(i + 1));
        Game::Lua::PushNumber(L, static_cast<double>(freeSlots[i]));
        Game::Lua::RawSet(L, -3);
    }
    return 1;
}

// `C_Container.CalculateTotalNumberOfFreeBagSlots()` — the total number of
// free slots across the player's *general-purpose* bags (backpack + equipped
// bags 0..4 whose BagFamily is 0). Specialty bags (quivers, soul bags, herb
// bags, …) are excluded, mirroring FrameXML's implementation exactly:
//
//   for bag = BACKPACK .. lastBag:
//       free, family = C_Container.GetContainerNumFreeSlots(bag)
//       if family == 0 then total = total + free end
//
// Takes no arguments. Specialty bags are correctly excluded: their family
// comes from `Item::BagFamily` (container-subclass fallback), so a Soul
// Bag / Herb Bag / Meat Bag reports a non-zero family and its slots are
// left out of the general-purpose total — matching retail's FrameXML
// behavior. (An earlier version miscounted them because Turtle leaves the
// raw family field at 0 for bags.)
int __fastcall Script_C_Container_CalculateTotalNumberOfFreeBagSlots(void *L) {
    int total = 0;
    for (int bagID = 0; bagID <= 4; bagID++) {
        const BagInfo info = ResolveBagInfo(bagID);
        if (info.slotCount > 0 && info.bagType == 0)
            total += CountFreeSlotsInBag(L, bagID, info.slotCount);
    }
    Game::Lua::PushNumber(L, static_cast<double>(total));
    return 1;
}

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Container", "GetContainerNumFreeSlots",
                                     &Script_C_Container_GetContainerNumFreeSlots);
    Game::Lua::RegisterTableFunction("C_Container", "GetContainerFreeSlots",
                                     &Script_C_Container_GetContainerFreeSlots);
    Game::Lua::RegisterTableFunction("C_Container", "CalculateTotalNumberOfFreeBagSlots",
                                     &Script_C_Container_CalculateTotalNumberOfFreeBagSlots);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Container::FreeSlots
