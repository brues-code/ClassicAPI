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

// `C_EquipmentSet.*` — backports the modern equipment-set API on top
// of a client-side persistent store. Vanilla 1.12 has no server-side
// equipment-set storage (no CMSG_EQUIPMENT_SET_SAVE opcode), so we
// persist each character's sets to
// `WTF\Account\<acct>\<realm>\<char>\ClassicAPI_EquipmentSets.txt`
// the same way `VanillaMinimapTracking` keeps its tracking config.
//
// Identity is by item GUID — `SaveEquipmentSet` snapshots the GUIDs
// currently worn, and `UseEquipmentSet` searches the player's bags (and
// the bank, while it is open) for those GUIDs at use time. Behavior
// follows 4.3.4's native equipment manager (`CGEquipmentManager`).
//
// Modern Classic Era's signatures use a numeric `iconFileID`; we
// accept icon paths (e.g. `"INV_Shield_06"`) instead because vanilla
// has no fileDataID system. Same string-or-default fallback semantic
// as 4.3.4's native `SaveEquipmentSet`.

#include "Action.h"
#include "Data.h"
#include "Game.h"
#include "Locations.h"
#include "Offsets.h"
#include "Pending.h"
#include "Set.h"
#include "Text.h"
#include "item/CGItem.h"
#include "item/Location.h"
#include "item/Swap.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace EquipmentSet::Api {

namespace {

// Pulls `setID` off Lua stack[idx]. Accepts numbers only — modern
// signatures take numeric IDs, not names, for the by-ID functions.
// Returns 0 if missing/non-numeric (callers treat 0 as "no such set").
uint32_t ArgSetID(void *L, int idx) {
    if (!Game::Lua::IsNumber(L, idx))
        return 0;
    const double d = Game::Lua::ToNumber(L, idx);
    if (d < 1.0)
        return 0;
    return static_cast<uint32_t>(d);
}

int ArgInt(void *L, int idx, int fallback = 0) {
    if (!Game::Lua::IsNumber(L, idx))
        return fallback;
    return static_cast<int>(Game::Lua::ToNumber(L, idx));
}

const char *ArgString(void *L, int idx) {
    if (!Game::Lua::IsString(L, idx))
        return nullptr;
    return Game::Lua::ToString(L, idx);
}

int ResolveItemID(const uint8_t *cgItem) {
    if (cgItem == nullptr)
        return 0;
    auto *instance = Item::InstanceBlock(cgItem);
    if (instance == nullptr)
        return 0;
    return static_cast<int>(*reinterpret_cast<const uint32_t *>(
        instance + Offsets::OFF_INSTANCE_BLOCK_ITEM_ID));
}

void PushTableEntry(void *L, int key, double value) {
    Game::Lua::PushNumber(L, static_cast<double>(key));
    Game::Lua::PushNumber(L, value);
    Game::Lua::SetTable(L, -3);
}

// 4.3.4 `CanUseEquipmentSets`: false while a swap is in flight.
int __fastcall Script_CanUseEquipmentSets(void *L) {
    Game::Lua::PushBool(L, Pending::CanUse());
    return 1;
}

int __fastcall Script_GetNumEquipmentSets(void *L) {
    const auto &all = Data::All();
    Game::Lua::PushNumber(L, static_cast<double>(all.size()));
    return 1;
}

// Modern returns a numeric-keyed array of setIDs. Order matches the
// list's storage order (insertion order; no implicit alphabetical
// sort). Empty list returns an empty table.
int __fastcall Script_GetEquipmentSetIDs(void *L) {
    const auto &all = Data::All();
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    int key = 1;
    for (const auto &s : all) {
        Game::Lua::PushNumber(L, static_cast<double>(key++));
        Game::Lua::PushNumber(L, static_cast<double>(s.setID));
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

int __fastcall Script_GetEquipmentSetID(void *L) {
    const char *name = ArgString(L, 1);
    if (name == nullptr)
        return 0;
    const Set *s = Data::FindByName(name);
    if (s == nullptr)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(s->setID));
    return 1;
}

// Returns the 9-tuple Blizzard ships:
//   name, icon, setID, isEquipped, numItems, numEquipped,
//   numInInventory, numMissing, numIgnored
// — 4.3.4 `Script_GetEquipmentSetInfo`: the counts are
// `Locations::GetStatus`, `isEquipped` is the strict
// `Locations::IsEquipped`.
int __fastcall Script_GetEquipmentSetInfo(void *L) {
    const uint32_t setID = ArgSetID(L, 1);
    const Set *s = Data::FindByID(setID);
    if (s == nullptr)
        return 0;

    const Locations::Status st = Locations::GetStatus(*s);
    Game::Lua::PushString(L, s->name.c_str());
    Game::Lua::PushString(L, s->icon.c_str());
    Game::Lua::PushNumber(L, static_cast<double>(s->setID));
    Game::Lua::PushBoolean(L, Locations::IsEquipped(*s) ? 1 : 0);
    Game::Lua::PushNumber(L, static_cast<double>(st.total));
    Game::Lua::PushNumber(L, static_cast<double>(st.equipped));
    Game::Lua::PushNumber(L, static_cast<double>(st.inInventory));
    Game::Lua::PushNumber(L, static_cast<double>(st.missing));
    Game::Lua::PushNumber(L, static_cast<double>(st.ignored));
    return 9;
}

int __fastcall Script_GetIgnoredSlots(void *L) {
    const uint32_t setID = ArgSetID(L, 1);
    const Set *s = Data::FindByID(setID);
    if (s == nullptr)
        return 0;
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    int key = 1;
    for (int i = 0; i < SLOT_COUNT; ++i) {
        if (s->items[i] == GUID_IGNORED) {
            Game::Lua::PushNumber(L, static_cast<double>(key++));
            Game::Lua::PushNumber(L, static_cast<double>(i + 1));
            Game::Lua::SetTable(L, -3);
        }
    }
    return 1;
}

// 4.3.4 `Script_GetEquipmentSetItemIDs`: { [slot] = value } for all 19
// slots — 0 empty, 1 ignored, else the itemID, or -1 when it can't be
// resolved. An item no longer in the client still has the itemID saved
// with the set, so -1 is left for sets saved before that was recorded.
int __fastcall Script_GetItemIDs(void *L) {
    const uint32_t setID = ArgSetID(L, 1);
    const Set *s = Data::FindByID(setID);
    if (s == nullptr)
        return 0;
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const uint64_t g = s->items[i];
        int value = static_cast<int>(g); // GUID_EMPTY 0 / GUID_IGNORED 1
        if (g != GUID_EMPTY && g != GUID_IGNORED) {
            value = ResolveItemID(Locations::ResolveItemByGUID(g));
            if (value == 0)
                value = static_cast<int>(s->itemIDs[i]);
            if (value == 0)
                value = -1;
        }
        PushTableEntry(L, i + 1, static_cast<double>(value));
    }
    return 1;
}

// 4.3.4 `Script_GetEquipmentSetLocations`: { [slot] = locationCode } for
// all 19 slots — 0 empty, `LOC_IGNORED`, the packed location (see Set.h),
// or `LOC_MISSING`. Bank items count only while the bank is open, the
// same rule `UseEquipmentSet` swaps by.
int __fastcall Script_GetItemLocations(void *L) {
    const uint32_t setID = ArgSetID(L, 1);
    const Set *s = Data::FindByID(setID);
    if (s == nullptr)
        return 0;

    const bool bank = Locations::BankerOpen();
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const uint64_t g = s->items[i];
        int loc = 0;
        if (g == GUID_IGNORED) {
            loc = LOC_IGNORED;
        } else if (g != GUID_EMPTY) {
            loc = Locations::FindGUID(g, bank);
            if (loc == 0)
                loc = LOC_MISSING;
        }
        PushTableEntry(L, i + 1, static_cast<double>(loc));
    }
    return 1;
}

// The name argument at `idx`, scrubbed as 4.3.4 does. Raises its
// "Invalid string for setName" error and returns false when the scrub
// rejects it.
bool ArgName(void *L, int idx, std::string *out) {
    if (Data::ScrubName(ArgString(L, idx), out))
        return true;
    Game::Lua::Error(L, "Invalid string for setName");
    return false;
}

int __fastcall Script_CreateEquipmentSet(void *L) {
    const char *raw = ArgString(L, 1);
    if (raw == nullptr || raw[0] == '\0') {
        Game::Lua::Error(L, "Usage: C_EquipmentSet.CreateEquipmentSet(name [, icon])");
        return 0;
    }
    std::string name;
    if (!ArgName(L, 1, &name))
        return 0;
    const char *icon = ArgString(L, 2);
    const uint32_t setID = Data::Create(name.c_str(), icon);
    if (setID == 0)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(setID));
    return 1;
}

int __fastcall Script_SaveEquipmentSet(void *L) {
    const uint32_t setID = ArgSetID(L, 1);
    if (setID == 0) {
        Game::Lua::Error(L, "Usage: C_EquipmentSet.SaveEquipmentSet(setID [, icon])");
        return 0;
    }
    const char *icon = ArgString(L, 2);
    if (Data::SaveExisting(setID, icon))
        Action::Repaint(setID); // the icon may have changed
    return 0;
}

int __fastcall Script_ModifyEquipmentSet(void *L) {
    const uint32_t setID = ArgSetID(L, 1);
    const char *raw = ArgString(L, 2);
    if (setID == 0 || raw == nullptr || raw[0] == '\0') {
        Game::Lua::Error(L,
                         "Usage: C_EquipmentSet.ModifyEquipmentSet(setID, newName)");
        return 0;
    }
    std::string name;
    if (!ArgName(L, 2, &name))
        return 0;
    if (Data::Rename(setID, name.c_str()))
        Action::Repaint(setID); // buttons show the name
    return 0;
}

int __fastcall Script_DeleteEquipmentSet(void *L) {
    const uint32_t setID = ArgSetID(L, 1);
    if (setID == 0) {
        Game::Lua::Error(L, "Usage: C_EquipmentSet.DeleteEquipmentSet(setID)");
        return 0;
    }
    if (Data::Delete(setID))
        Action::ClearButtons(setID);
    return 0;
}

int __fastcall Script_IgnoreSlotForSave(void *L) {
    const int slot = ArgInt(L, 1);
    Data::IgnoreSlot(slot);
    return 0;
}

int __fastcall Script_UnignoreSlotForSave(void *L) {
    const int slot = ArgInt(L, 1);
    Data::UnignoreSlot(slot);
    return 0;
}

int __fastcall Script_IsSlotIgnoredForSave(void *L) {
    const int slot = ArgInt(L, 1);
    Game::Lua::PushBool(L, Data::IsSlotIgnored(slot));
    return 1;
}

int __fastcall Script_ClearIgnoredSlotsForSave(void *L) {
    (void)L;
    Data::ClearIgnoredSlots();
    return 0;
}

// Whether a swap would race an item already in transit — see
// `Locations::ContainsLockedItems`.
int __fastcall Script_EquipmentSetContainsLockedItems(void *L) {
    const uint32_t setID = ArgSetID(L, 1);
    const Set *s = Data::FindByID(setID);
    if (s == nullptr)
        return 0;
    Game::Lua::PushBool(L, Locations::ContainsLockedItems(*s));
    return 1;
}

// 4.3.4 `CGEquipmentManager::UseEquipmentSet`, over 1.12's per-item swap
// packets instead of its single CMSG_EQUIPMENT_SET_USE:
//   - refused (ERR_CLIENT_LOCKED_OUT, the same "You can't do that right
//     now." text as 4.3.4's SPELL_FAILED_CANT_DO_THAT_RIGHT_NOW) while a
//     swap is in flight or the cursor holds a bag item;
//   - in combat only the weapon slots swap, with one COMBAT_SWAP message
//     when another slot would have changed;
//   - an item that can't be found is skipped, with one MISSING_ITEM
//     message;
//   - bank items are used only while the bank is open (the server refuses
//     them otherwise);
//   - the result arrives later as EQUIPMENT_SWAP_FINISHED (see
//     `EquipmentSet::Pending`).
//
// Each move is the engine's own `FUN_INVENTORY_SWAP` (opcodes 0x10D /
// 0x10C), an atomic server-side swap that never touches the cursor, so a
// two-item cycle (rings A<->B) resolves in one packet. Sources are resolved
// against the state BEFORE anything is sent: the engine's inventory only
// changes when the server answers, so a lookup between packets would see
// stale locations. A virtual paperdoll tracks the moves instead.

bool CursorHoldsBagItem() {
    return Game::Read<uint32_t>(Offsets::VAR_CURSOR_TYPE) == 1 &&
           (Game::Read<uint32_t>(Offsets::VAR_CURSOR_ITEM_GUID_LO) != 0 ||
            Game::Read<uint32_t>(Offsets::VAR_CURSOR_ITEM_GUID_HI) != 0);
}

bool PlayerInCombat() {
    const uint8_t *desc = Unit::Identity::PlayerDescriptor();
    return desc != nullptr &&
           (Game::Read<uint32_t>(desc, Offsets::OFF_UNIT_FIELD_FLAGS) &
            Offsets::UNIT_FLAG_IN_COMBAT) != 0;
}

// The Lua bagID and 1-based slot of a bag or bank location code (see
// Set.h's pack helpers): 0..4 player bags, -1 main bank, 5..10 bank bags.
// A main-bank code carries the inventory slot id, so it comes back
// through `INVSLOT_BANK1` to the 1-based slot `Item::Swap::FromBag` takes.
void UnpackContainer(int loc, int *bagID, int *slot) {
    *slot = loc & LOC_SLOT_MASK;
    const int bag = (loc >> LOC_BAG_SHIFT) & 0xFF;
    if ((loc & LOC_BANK) == 0) {
        *bagID = bag;
    } else if ((loc & LOC_BAGS) == 0) {
        *bagID = -1;
        *slot = *slot - Offsets::INVSLOT_BANK1 + 1;
    } else {
        *bagID = bag + BANK_BAG_OFFSET;
    }
}

int __fastcall Script_UseEquipmentSet(void *L) {
    const uint32_t setID = ArgSetID(L, 1);
    const Set *s = Data::FindByID(setID);
    if (s == nullptr) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }
    if (!Pending::CanUse() || CursorHoldsBagItem()) {
        Text::UIError("ERR_CLIENT_LOCKED_OUT", "You can't do that right now.");
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }

    // Copy what the swap needs: the free-slot scan below stomps the Lua
    // stack, and the error messages run Lua handlers.
    const std::string name = s->name;
    uint64_t guids[SLOT_COUNT];
    std::memcpy(guids, s->items, sizeof(guids));

    Pending::Request req{};
    req.setID = setID;
    auto noteMoved = [&req](uint64_t g) {
        if (g != 0)
            req.moved[req.movedCount++] = g;
    };

    uint64_t virtualPaperdoll[SLOT_COUNT];
    Locations::SnapshotPaperdoll(virtualPaperdoll);

    // Decide every slot against the original state.
    const bool inCombat = PlayerInCombat();
    const bool bank = Locations::BankerOpen();
    bool combatWarned = false;
    bool missingWarned = false;
    int sourceLoc[SLOT_COUNT] = {};
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const uint64_t g = guids[i];
        if (g == GUID_IGNORED)
            continue;
        if (inCombat && !SwapsInCombat(i + 1)) {
            if (!combatWarned && g != virtualPaperdoll[i]) {
                Text::UIError("ERR_EQUIPMENT_MANAGER_COMBAT_SWAP_S",
                              "Some items from set %s cannot be swapped because "
                              "you are in combat.",
                              name.c_str());
                combatWarned = true;
            }
            continue;
        }
        if (g != GUID_EMPTY) {
            sourceLoc[i] = Locations::FindGUID(g, bank);
            if (sourceLoc[i] == 0) {
                if (!missingWarned) {
                    Text::UIError("ERR_EQUIPMENT_MANAGER_MISSING_ITEM_S",
                                  "One or more items from set %s were missing and "
                                  "could not be equipped.",
                                  name.c_str());
                    missingWarned = true;
                }
                continue;
            }
        }
        req.want[i] = g;
        req.cares[i] = true;
    }

    // Free bag slots for phase 3, snapshotted up front for the same
    // stale-state reason: a fresh scan per unequip would keep finding the
    // same "first empty" slot. At most every slot comes off.
    struct FreeSlot {
        int bag;
        int slot;
    };
    FreeSlot freeSlots[SLOT_COUNT];
    int freeSlotCount = 0;
    for (int bag = 0; bag <= 4 && freeSlotCount < SLOT_COUNT; ++bag) {
        const int slots = Item::Location::GetBagSlotCount(bag);
        for (int slot = 1; slot <= slots && freeSlotCount < SLOT_COUNT; ++slot) {
            if (Item::Location::ResolveBag(L, bag, slot) == nullptr)
                freeSlots[freeSlotCount++] = {bag, slot};
        }
    }
    int nextFreeSlot = 0;

    auto currentPaperdollSlot = [&](uint64_t g) -> int {
        for (int slot = 1; slot <= SLOT_COUNT; ++slot)
            if (virtualPaperdoll[slot - 1] == g)
                return slot;
        return 0;
    };

    // Phase 1: paperdoll -> paperdoll. Neither side is a bag, so phase 2's
    // bag sources stay valid.
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const int targetSlot = i + 1;
        const uint64_t g = guids[i];
        if (!req.cares[i] || g == GUID_EMPTY ||
            (sourceLoc[i] & (LOC_BAGS | LOC_BANK)) != 0)
            continue;
        const int srcSlot = currentPaperdollSlot(g);
        if (srcSlot == 0 || srcSlot == targetSlot)
            continue;
        const uint64_t displaced = virtualPaperdoll[targetSlot - 1];
        if (Item::Swap::FromPaperdoll(Locations::ResolveItemByGUID(g), srcSlot,
                                      targetSlot)) {
            virtualPaperdoll[targetSlot - 1] = g;
            virtualPaperdoll[srcSlot - 1] = displaced;
            noteMoved(g);
            noteMoved(displaced);
        } else {
            req.cares[i] = false;
        }
    }

    // Phase 2: bag / bank -> paperdoll. The paperdoll occupant lands where
    // the incoming item was. Anything the set still wanted worn was placed
    // by phase 1, so a displacement here can't strand a set item.
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const int targetSlot = i + 1;
        const uint64_t g = guids[i];
        if (!req.cares[i] || g == GUID_EMPTY ||
            (sourceLoc[i] & (LOC_BAGS | LOC_BANK)) == 0)
            continue;
        int bagID = 0;
        int slot = 0;
        UnpackContainer(sourceLoc[i], &bagID, &slot);
        const uint64_t displaced = virtualPaperdoll[targetSlot - 1];
        if (Item::Swap::FromBag(Locations::ResolveItemByGUID(g), bagID, slot,
                                targetSlot)) {
            virtualPaperdoll[targetSlot - 1] = g;
            noteMoved(g);
            noteMoved(displaced);
        } else {
            req.cares[i] = false;
        }
    }

    // Phase 3: take off what the set wants empty. Runs last so an item
    // phases 1/2 displaced into such a slot comes off too. No free bag slot
    // is 4.3.4's bags-full result.
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const int targetSlot = i + 1;
        const uint64_t occupant = virtualPaperdoll[targetSlot - 1];
        if (!req.cares[i] || guids[i] != GUID_EMPTY || occupant == 0)
            continue;
        if (nextFreeSlot >= freeSlotCount) {
            req.bagsFull = true;
            req.cares[i] = false;
            continue;
        }
        const FreeSlot &fs = freeSlots[nextFreeSlot++];
        if (Item::Swap::ToBag(Locations::ResolveItemByGUID(occupant), targetSlot,
                              fs.bag, fs.slot)) {
            virtualPaperdoll[targetSlot - 1] = 0;
            noteMoved(occupant);
        } else {
            req.cares[i] = false;
        }
    }

    Pending::Begin(req);
    Game::Lua::SetTop(L, 0);
    Game::Lua::PushBoolean(L, 1);
    return 1;
}

const Game::Doc::Field kSetIDArgs[] = {
    Game::Doc::Req("setID", "number"),
};
const Game::Doc::Field kSlotArgs[] = {
    Game::Doc::Req("slot", "luaIndex", "An equipment slot, from 1 to 19."),
};

const Game::Doc::Field kCanUseRets[] = {
    Game::Doc::Req("canUseEquipmentSets", "bool", "False while a set swap is in progress."),
};
const Game::Doc::Function kCanUseEquipmentSets{
    "Returns whether an equipment set can be used now.", {}, kCanUseRets};

const Game::Doc::Field kNumRets[] = {
    Game::Doc::Req("numEquipmentSets", "number"),
};
const Game::Doc::Function kGetNumEquipmentSets{
    "Returns the number of equipment sets the character has.", {}, kNumRets};

const Game::Doc::Field kIDsRets[] = {
    Game::Doc::Req("setIDs", "table", "The set IDs in the order the sets were created."),
};
const Game::Doc::Function kGetEquipmentSetIDs{
    "Returns the IDs of all the character's equipment sets.", {}, kIDsRets};

const Game::Doc::Field kIDArgs[] = {
    Game::Doc::Req("name", "cstring", "The exact name; case matters."),
};
const Game::Doc::Field kIDRets[] = {
    Game::Doc::Opt("setID", "number", nullptr, "Nil when no set has that name."),
};
const Game::Doc::Function kGetEquipmentSetID{
    "Returns the ID of the equipment set with a name.", kIDArgs, kIDRets};

const Game::Doc::Field kInfoRets[] = {
    Game::Doc::Opt("name", "string", nullptr,
                   "Nil, with the rest, when the set does not exist."),
    Game::Doc::Opt("icon", "string", nullptr, "The icon's texture name."),
    Game::Doc::Opt("setID", "number"),
    Game::Doc::Opt("isEquipped", "bool", nullptr,
                   "True when every slot that is not ignored holds what the set saved."),
    Game::Doc::Opt("numItems", "number", nullptr,
                   "numEquipped plus numInInventory plus numMissing."),
    Game::Doc::Opt("numEquipped", "number"),
    Game::Doc::Opt("numInInventory", "number", nullptr,
                   "In the bags, or in the bank while the bank is open."),
    Game::Doc::Opt("numMissing", "number"),
    Game::Doc::Opt("numIgnored", "number"),
};
const Game::Doc::Function kGetEquipmentSetInfo{
    "Returns an equipment set's name, icon, and where its items are.", kSetIDArgs,
    kInfoRets};

const Game::Doc::Field kIgnoredRets[] = {
    Game::Doc::Opt("ignoredSlots", "table", nullptr,
                   "The numbers of the ignored slots; nil when the set does not exist."),
};
const Game::Doc::Function kGetIgnoredSlots{
    "Returns the slots an equipment set leaves alone.", kSetIDArgs, kIgnoredRets};

const Game::Doc::Field kItemIDsRets[] = {
    Game::Doc::Opt("itemIDs", "table", nullptr,
                   "Keyed by slot: the item ID, 0 for an empty slot, 1 for an ignored slot, "
                   "or -1 when not known."),
};
const Game::Doc::Function kGetItemIDs{
    "Returns the item ID an equipment set saved for each slot.", kSetIDArgs, kItemIDsRets};

const Game::Doc::Field kLocationsRets[] = {
    Game::Doc::Opt("locations", "table", nullptr,
                   "Keyed by slot: a packed location, 0 for an empty slot, 1 for an ignored "
                   "slot, or -1 for a missing item."),
};
const Game::Doc::Function kGetItemLocations{
    "Returns where each item of an equipment set is now.", kSetIDArgs, kLocationsRets};

const Game::Doc::Field kCreateArgs[] = {
    Game::Doc::Req("name", "cstring", "Shorter than 32 characters, with no line break."),
    Game::Doc::Opt("icon", "string", "\"INV_Misc_QuestionMark\"", "A texture name."),
};
const Game::Doc::Field kCreateRets[] = {
    Game::Doc::Opt("setID", "number", nullptr, "Nil when the name is already in use."),
};
const Game::Doc::Function kCreateEquipmentSet{
    "Saves the equipped items as a new equipment set.", kCreateArgs, kCreateRets};

const Game::Doc::Field kSaveArgs[] = {
    Game::Doc::Req("setID", "number"),
    Game::Doc::Opt("icon", "string", nullptr, "A new texture name; nil keeps the icon."),
};
const Game::Doc::Function kSaveEquipmentSet{
    "Saves the equipped items into an existing equipment set.", kSaveArgs, {}};

const Game::Doc::Field kModifyArgs[] = {
    Game::Doc::Req("setID", "number"),
    Game::Doc::Req("newName", "cstring"),
};
const Game::Doc::Function kModifyEquipmentSet{
    "Renames an equipment set; does nothing when another set has the name.", kModifyArgs,
    {}};

const Game::Doc::Function kDeleteEquipmentSet{
    "Deletes an equipment set and removes it from the action bars.", kSetIDArgs, {}};

const Game::Doc::Function kIgnoreSlotForSave{
    "Makes the next save of an equipment set leave a slot alone.", kSlotArgs, {}};
const Game::Doc::Function kUnignoreSlotForSave{
    "Makes the next save of an equipment set include a slot again.", kSlotArgs, {}};

const Game::Doc::Field kIsIgnoredRets[] = {
    Game::Doc::Req("isSlotIgnored", "bool"),
};
const Game::Doc::Function kIsSlotIgnoredForSave{
    "Returns whether the next save of an equipment set leaves a slot alone.", kSlotArgs,
    kIsIgnoredRets};
const Game::Doc::Function kClearIgnoredSlotsForSave{
    "Makes the next save of an equipment set include every slot.", {}, {}};

const Game::Doc::Field kLockedRets[] = {
    Game::Doc::Opt("hasLockedItems", "bool", nullptr, "Nil when the set does not exist."),
};
const Game::Doc::Function kEquipmentSetContainsLockedItems{
    "Returns whether an item of an equipment set is locked by a move in progress.",
    kSetIDArgs, kLockedRets};

const Game::Doc::Field kUseRets[] = {
    Game::Doc::Req("setWasEquipped", "bool",
                   "True when the swap starts; EQUIPMENT_SWAP_FINISHED gives the result."),
};
const Game::Doc::Function kUseEquipmentSet{
    "Equips the items of an equipment set.", kSetIDArgs, kUseRets};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "CanUseEquipmentSets",
                                     &Script_CanUseEquipmentSets, &kCanUseEquipmentSets);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "GetNumEquipmentSets",
                                     &Script_GetNumEquipmentSets, &kGetNumEquipmentSets);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "GetEquipmentSetIDs",
                                     &Script_GetEquipmentSetIDs, &kGetEquipmentSetIDs);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "GetEquipmentSetID",
                                     &Script_GetEquipmentSetID, &kGetEquipmentSetID);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "GetEquipmentSetInfo",
                                     &Script_GetEquipmentSetInfo, &kGetEquipmentSetInfo);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "GetIgnoredSlots",
                                     &Script_GetIgnoredSlots, &kGetIgnoredSlots);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "GetItemIDs",
                                     &Script_GetItemIDs, &kGetItemIDs);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "GetItemLocations",
                                     &Script_GetItemLocations, &kGetItemLocations);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "CreateEquipmentSet",
                                     &Script_CreateEquipmentSet, &kCreateEquipmentSet);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "SaveEquipmentSet",
                                     &Script_SaveEquipmentSet, &kSaveEquipmentSet);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "ModifyEquipmentSet",
                                     &Script_ModifyEquipmentSet, &kModifyEquipmentSet);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "DeleteEquipmentSet",
                                     &Script_DeleteEquipmentSet, &kDeleteEquipmentSet);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "IgnoreSlotForSave",
                                     &Script_IgnoreSlotForSave, &kIgnoreSlotForSave);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "UnignoreSlotForSave",
                                     &Script_UnignoreSlotForSave, &kUnignoreSlotForSave);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "IsSlotIgnoredForSave",
                                     &Script_IsSlotIgnoredForSave, &kIsSlotIgnoredForSave);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "ClearIgnoredSlotsForSave",
                                     &Script_ClearIgnoredSlotsForSave,
                                     &kClearIgnoredSlotsForSave);
    Game::Lua::RegisterTableFunction("C_EquipmentSet",
                                     "EquipmentSetContainsLockedItems",
                                     &Script_EquipmentSetContainsLockedItems,
                                     &kEquipmentSetContainsLockedItems);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "UseEquipmentSet",
                                     &Script_UseEquipmentSet, &kUseEquipmentSet);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace EquipmentSet::Api
