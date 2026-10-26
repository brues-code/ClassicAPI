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

// `C_Container.SortBags()` — arrange the player's bags.
//
// Two phases, because every item move is a server round trip and the
// second phase has to read slot contents the first phase changed.
//
// PHASE ONE consolidates partial stacks: per item, the stacks are poured
// fullest-first with an explicit swap or split per transfer (see
// Consolidate for why the server's autostore cannot do this). Sizing a
// transfer needs the item's max stack, so an item whose data has not
// arrived is left alone this pass.
//
// PHASE TWO places items. It plans the whole layout up front and then
// tracks each item's live position as swaps fire, rather than re-reading
// a layout it is in the middle of mutating, so the whole batch goes out
// in one frame.
//
// Both phases wait on the ENGINE'S ITEM LOCK, the bit its own move
// senders set: every item a phase relocates is locked once the batch is
// sent, and the engine clears that lock itself when the server answers —
// the item's slot GUID changing (an inventory or container slot watcher),
// a refusal naming it (SMSG_INVENTORY_CHANGE_FAILURE), or the item being
// destroyed by a merge. A phase is over when none of its items is still
// locked; a deadline that restarts on every answer releases the stragglers
// if the server goes quiet. The locks also grey the icons and stop the
// player picking an item up mid-sort, exactly as a hand-dragged move does.
//
// Neither phase may touch the Lua stack: everything after the initial call
// runs from the world tick, outside any Lua call, where the engine may
// have its own values on the stack. Hence the `*From` senders and the
// direct invMgr walk rather than `ResolveBag`.
//
// THE ORDERING IS OURS. Retail computes it in code we cannot read, and
// its categories (per-bag assignment flags, expansion filters) have no
// counterpart here, so this is a defensible order rather than a
// reproduction of Blizzard's: hearthstone, gear, consumables, reagents,
// trade goods, quest items, everything else by quality, junk last. Within
// a category, by item class, subclass, name, then fuller stacks first.
// Gear adds two keys so like pieces sit together: the equip slot right
// after the class (kSlotOrder), and quality, best first, before the name.
//
// An item whose data has NOT arrived, or that is locked (on the cursor,
// in the trade window, mid-move), is pinned: left where it is, with its
// slot withheld from the destination pool so nothing else is planned into
// it. An unrankable item cannot be placed and a locked one cannot be
// moved — the server refuses it, and one refused swap invalidates every
// later move in the batch. A later call places it.

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "equipmentset/Locations.h"
#include "item/BagFamily.h"
#include "item/CGItem.h"
#include "item/ID.h"
#include "item/Location.h"
#include "item/Record.h"
#include "item/Swap.h"
#include "object/Resolve.h"
#include "tick/WorldTick.h"
#include "time/Clock.h"
#include "unit/Identity.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace Container::SortBags {

namespace {

// The two sets this can arrange. Both go through the same merge,
// snapshot, comparator and placement.
constexpr int kInventoryBags[] = {0, 1, 2, 3, 4};
constexpr int kBankBags[] = {-1, 5, 6, 7, 8, 9, 10};

// Fill direction, persisted like every other engine setting: the cvar
// carries it to Config.wtf and `GetCVar("sortBagsRightToLeft")` reads it,
// so it survives a session the way the retail setting does.
CVar::Factory::Handle g_rightToLeftCVar = nullptr;

bool RightToLeft() {
    return CVar::Factory::GetInt(g_rightToLeftCVar, 0) != 0;
}

// The two containers a sort can be told to leave alone. An equipped bag
// is excluded per-bag through its own `DisableAutoSort` slot flag; the
// backpack and the main bank container have no such slot, so each gets
// its own setting. Persisted as cvars for the same reason the fill
// direction is.
CVar::Factory::Handle g_backpackAutosortCVar = nullptr;
CVar::Factory::Handle g_bankAutosortCVar = nullptr;

bool BackpackAutosortDisabled() {
    return CVar::Factory::GetInt(g_backpackAutosortCVar, 0) != 0;
}

bool BankAutosortDisabled() {
    return CVar::Factory::GetInt(g_bankAutosortCVar, 0) != 0;
}

// Merge packets sent by the most recent phase one, reset per sort.
int g_mergePackets = 0;

// Item.dbc `m_class` values we rank on. Per-module codes, the way
// `Item::BagFamily` keeps its own.
constexpr uint32_t kClassConsumable = 0;
constexpr uint32_t kClassArmor = 4;
constexpr uint32_t kClassReagent = 5;
constexpr uint32_t kClassTradeGoods = 7;

constexpr uint32_t kQualityPoor = 0;
constexpr uint32_t kQualityUncommon = 2;
constexpr uint32_t kQualityRare = 3;
constexpr uint32_t kQualityEpic = 4;

// Primary key. Lower sorts nearer the first slot.
enum Category : uint8_t {
    kCatHearthstone = 0,
    kCatGear,
    kCatConsumable,
    kCatReagent,
    kCatTradeGoods,
    kCatQuest,
    kCatOtherEpic,
    kCatOtherRare,
    kCatOtherUncommon,
    kCatOtherPlain,
    kCatJunk,
};

uint8_t CategoryFor(uint32_t itemID, uint32_t itemClass, uint32_t quality) {
    if (itemID == Offsets::HEARTHSTONE_ITEM_ID)
        return kCatHearthstone;
    if (quality == kQualityPoor)
        return kCatJunk;
    if (itemClass == Offsets::ITEM_CLASS_WEAPON || itemClass == kClassArmor)
        return kCatGear;
    if (itemClass == kClassConsumable)
        return kCatConsumable;
    if (itemClass == kClassReagent)
        return kCatReagent;
    if (itemClass == kClassTradeGoods)
        return kCatTradeGoods;
    if (itemClass == Offsets::ITEM_CLASS_QUEST)
        return kCatQuest;
    if (quality >= kQualityEpic)
        return kCatOtherEpic;
    if (quality == kQualityRare)
        return kCatOtherRare;
    if (quality == kQualityUncommon)
        return kCatOtherUncommon;
    return kCatOtherPlain;
}

// Gear's slot key: Item.dbc `m_inventoryType` (INVTYPE_*) in the order its
// pieces sort. Not the raw number, which does not follow the character
// pane: robe (20) belongs beside chest (5), and cloak (16) beside
// shoulder. Not the equipment slot either, which lumps shields, off-hand
// weapons and held items into one. The order is Baganator's (its
// Sorting/ItemFields.lua): weapons by hand, then ranged and ammo, then
// armor head to foot, then jewelry, shirt and tabard.
constexpr uint8_t kSlotOrder[] = {
    17, 13, 21, 14, 23, 26, 22, 15, 25, 24, 27, 28, // weapons, ranged, relic
    1, 3, 16, 5, 20, 9, 10, 6, 7, 8,                // head .. feet
    2, 11, 12, 4, 19, 18, 0,                        // jewelry, shirt, tabard, bag, none
};

uint8_t SlotRank(uint32_t inventoryType) {
    for (uint8_t i = 0; i < sizeof kSlotOrder; ++i)
        if (kSlotOrder[i] == inventoryType)
            return i;
    return static_cast<uint8_t>(sizeof kSlotOrder); // unknown: after all known
}

struct Entry {
    const void *item;    // CGItem — stable across the swap batch
    uint64_t guid;
    int startBag, startSlot;
    int curBag, curSlot; // live position, updated as swaps fire

    // Destination, or unassigned. The unassigned sentinel is destSLOT,
    // never destBag: slots are 1-based so a negative one is impossible,
    // whereas bagID -1 is the main bank. Testing destBag < 0 here treats
    // every item bound for the main bank as unplaced and silently drops
    // its move.
    int destBag, destSlot;
    uint32_t itemID;
    uint32_t family; // the item's own bag family (0 = fits anywhere)
    uint32_t itemClass, subClass, quality, count;
    const char *name;
    uint8_t category;
    uint8_t slotRank; // SlotRank(m_inventoryType)
};

struct Cell {
    int bag, slot;
};

char Lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// A missing name sorts after every present one. Substituting a
// placeholder string instead (libbagsort uses "zzz") wedges the unnamed
// item in among items actually named with those letters; this keeps it
// deterministic and at the end. In practice a nameless item is pinned
// before it reaches here, so this is the belt to that braces.
int CompareNames(const char *a, const char *b) {
    if (a == b)
        return 0;
    if (a == nullptr)
        return 1;
    if (b == nullptr)
        return -1;
    for (size_t i = 0;; ++i) {
        const char ca = Lower(a[i]), cb = Lower(b[i]);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        if (ca == '\0')
            return 0;
    }
}

bool Precedes(const Entry &a, const Entry &b) {
    if (a.category != b.category)
        return a.category < b.category;
    if (a.itemClass != b.itemClass)
        return a.itemClass < b.itemClass;
    const bool gear = a.category == kCatGear;
    if (gear && a.slotRank != b.slotRank)
        return a.slotRank < b.slotRank;
    if (a.subClass != b.subClass)
        return a.subClass < b.subClass;
    if (gear && a.quality != b.quality)
        return a.quality > b.quality; // best first
    const int byName = CompareNames(a.name, b.name);
    if (byName != 0)
        return byName < 0;
    if (a.count != b.count)
        return a.count > b.count; // fuller stacks first
    return a.itemID < b.itemID;   // total order, so the result is stable
}

// A bag's storage, addressed without Lua: the invMgr holding its GUID
// array, the linear index its first slot sits at, and the family it
// accepts. The backpack lives inside the player's own invMgr; bags 1..4
// each have one of their own.
struct BagView {
    const uint8_t *invMgr;
    int firstLinear;
    int slotCount;
    uint32_t family;
};

// The backpack and the main bank both live inside the player's own
// invMgr, at different linear bases. Equipped bags and bank bags each
// have an invMgr of their own, reached through the bag item.
//
// The bank resolves only while a banker is open: the server refuses every
// bank move without one, and the engine's own slot reader (`0x6228a0`)
// hides slots 39..68 on the same banker-GUID test. The main bank's GUIDs
// sit in the player's invMgr from login, so without this gate a closed-
// bank sort would plan and send moves the server rejects one by one.
bool ViewForBag(int bagID, BagView *out) {
    if ((bagID == -1 || bagID >= 5) && !EquipmentSet::Locations::BankerOpen())
        return false;
    if (bagID == 0) {
        auto *inv = Unit::Identity::PlayerInventoryManager();
        if (inv == nullptr)
            return false;
        *out = {inv, Offsets::INVMGR_BACKPACK_FIRST_SLOT,
                Offsets::BACKPACK_NUM_SLOTS, 0};
        return true;
    }
    if (bagID == -1) {
        auto *inv = Unit::Identity::PlayerInventoryManager();
        if (inv == nullptr)
            return false;
        *out = {inv, Offsets::INVMGR_BANK_MAIN_FIRST_SLOT,
                Offsets::INVMGR_BANK_MAIN_LAST_SLOT -
                    Offsets::INVMGR_BANK_MAIN_FIRST_SLOT + 1,
                0};
        return true;
    }

    // Resolve the bag item that owns the container.
    int equipSlot = 0;
    if (bagID >= 1 && bagID <= 4)
        equipSlot = Offsets::INVSLOT_BAG1 + bagID - 1;
    else if (bagID >= 5 && bagID <= 10)
        equipSlot = Offsets::INVMGR_BANK_BAG_FIRST_SLOT + 1 + (bagID - 5);
    else
        return false;

    auto *bagItem = Item::Location::ResolveEquipmentSlot(equipSlot);
    if (bagItem == nullptr)
        return false;
    auto *bagInv = static_cast<const uint8_t *>(
        Item::Location::ContainerInventory(bagItem));
    if (bagInv == nullptr)
        return false;
    const int slots = static_cast<int>(
        Game::Read<uint32_t>(bagInv, Offsets::OFF_INVMGR_SLOT_COUNT));
    if (slots <= 0)
        return false;

    // A specialty bag only accepts its own family, and Turtle leaves the
    // raw field empty on bags — `BitmaskForRecord` derives it from the
    // container subclass, so a quiver reports a family here rather than
    // reading as general-purpose. Shared with `C_Item.GetItemFamily`, so
    // a bag and the items it holds agree.
    uint32_t family = 0;
    const int bagItemID = Item::ID::FromCGItem(bagItem);
    if (bagItemID > 0) {
        auto *rec = Item::PeekRecord(static_cast<uint32_t>(bagItemID));
        if (rec != nullptr)
            family = Item::BagFamily::BitmaskForRecord(rec);
    }
    *out = {bagInv, 0, slots, family};
    return true;
}

const uint8_t *ItemByGuid(uint64_t guid) {
    return static_cast<const uint8_t *>(
        Object::ByGuid(Offsets::TYPEMASK_ITEM, guid, "SortBags", 0));
}

const uint8_t *ItemAt(const BagView &v, int slotIndex0, uint64_t *guidOut) {
    auto *guids = *reinterpret_cast<const uint64_t *const *>(
        v.invMgr + Offsets::OFF_INVMGR_GUID_ARRAY);
    if (guids == nullptr)
        return nullptr;
    const uint64_t guid = guids[v.firstLinear + slotIndex0];
    if (guid == 0)
        return nullptr;
    *guidOut = guid;
    return ItemByGuid(guid);
}

bool IsLocked(const uint8_t *item) {
    return (Game::Read<uint32_t>(item, Offsets::OFF_ITEM_CLIENT_LOCK) &
            Offsets::ITEM_CLIENT_LOCK_BIT) != 0;
}

// An item gone from the object manager is settled too: a whole-stack
// merge destroys its source.
bool StillLocked(uint64_t guid) {
    const uint8_t *item = ItemByGuid(guid);
    return item != nullptr && IsLocked(item);
}

// The engine's own lock setter / clearer (`0x4953e0` / `0x495420`): each
// flips bit 0 of `+0x314` and signals ITEM_LOCK_CHANGED.
using LockByGuid_t = void(__stdcall *)(uint32_t guidLo, uint32_t guidHi);

void CallLockFn(uintptr_t fn, uint64_t guid) {
    reinterpret_cast<LockByGuid_t>(fn)(static_cast<uint32_t>(guid),
                                       static_cast<uint32_t>(guid >> 32));
}

// Items the running phase relocated and locked; the phase is over once
// none of them is still locked.
std::vector<uint64_t> g_waiting;

void Lock(uint64_t guid) {
    CallLockFn(Offsets::FUN_ITEM_LOCK_BY_GUID, guid);
    g_waiting.push_back(guid);
}

uint32_t StackCount(const uint8_t *item) {
    auto *fields = Item::ObjectFields(item);
    if (fields == nullptr)
        return 1;
    const uint32_t n =
        Game::Read<uint32_t>(fields, Offsets::OFF_DESCRIPTOR_STACK_COUNT);
    return n == 0 ? 1 : n;
}

// --- phase one: consolidate stacks ------------------------------------------

// One stack per group of the same item.
struct Stack {
    const uint8_t *item;
    uint64_t guid;
    int bag, slot;
    uint32_t count;
};
struct Group {
    uint32_t itemID;
    uint32_t maxStack;
    std::vector<Stack> stacks;
};

// Collects every stackable item across `bags`, grouped by item ID. An
// item whose data has not arrived is skipped — its max stack is unknown,
// so no transfer into or out of it can be sized — and so is a locked one,
// which the server would refuse to move.
std::vector<Group> GatherGroups(const int *bags, int bagCount) {
    std::vector<Group> groups;
    for (int b = 0; b < bagCount; ++b) {
        const int bag = bags[b];
        BagView view;
        if (!ViewForBag(bag, &view))
            continue;
        for (int i = 0; i < view.slotCount; ++i) {
            uint64_t guid = 0;
            const uint8_t *item = ItemAt(view, i, &guid);
            if (item == nullptr || IsLocked(item))
                continue;
            const int itemID = Item::ID::FromCGItem(item);
            if (itemID <= 0)
                continue;
            const uint32_t id = static_cast<uint32_t>(itemID);

            auto *rec = Item::PeekRecord(id);
            if (rec == nullptr)
                continue;
            const uint32_t maxStack =
                Game::Read<uint32_t>(rec, Offsets::OFF_ITEMSTATS_STACKABLE);
            if (maxStack <= 1)
                continue; // cannot stack at all, so never a merge candidate

            Group *group = nullptr;
            for (auto &candidate : groups) {
                if (candidate.itemID == id) {
                    group = &candidate;
                    break;
                }
            }
            if (group == nullptr) {
                groups.push_back({id, maxStack, {}});
                group = &groups.back();
            }
            group->stacks.push_back({item, guid, bag, i + 1, StackCount(item)});
        }
    }
    return groups;
}

// Why every transfer is addressed explicitly rather than delegated to the
// server's autostore, which can merge a stack into several destinations
// for one packet:
//
// `CMSG_AUTOSTORE_BAG_ITEM` names its destination with a container byte,
// and the only value the engine's builder will emit for "the player" is
// `INVENTORY_SLOT_BAG_0` (255). Server-side that is NOT the search-every-
// bag path — that one is `NULL_BAG` (0), which the builder cannot
// express. 255 takes the specific-container branch, which searches the
// keyring and the backpack and nothing else. So autostore can never
// merge a stack in one equipped bag with a stack in another, which is
// most of the real cases. And `CMSG_AUTOSTORE_BANK_ITEM` always CROSSES
// the inventory/bank boundary, so the bank has no such packet at all.
//
// Addressing both endpoints explicitly costs a packet per transfer
// instead of per emptied source — occasionally one more, when a stack
// has to be split across several destinations — and works everywhere.
//
// Two rolling pointers per item: stacks fullest-first, then poured from
// the back into the front until they meet. Each source a transfer empties
// is locked once the batch is out, so phase two can wait for it.
void Consolidate(const int *bags, int bagCount) {
    std::vector<Group> groups = GatherGroups(bags, bagCount);
    std::vector<uint64_t> emptied;

    for (auto &group : groups) {
        if (group.stacks.size() < 2)
            continue;

        // Pack fully, not just enough to free a slot: 9 and 2 with a cap
        // of 10 become 10 and 1, even though that still occupies two
        // slots. Topping stacks up is the point of consolidating.
        //
        // This settles after one pass and does not re-fire on the next
        // call, because a maximally packed group has every stack full
        // except the last — so `space == 0` walks `lo` straight up to
        // `hi` and nothing is sent.
        std::sort(group.stacks.begin(), group.stacks.end(),
                  [](const Stack &a, const Stack &b) { return a.count > b.count; });
        size_t lo = 0, hi = group.stacks.size() - 1;
        while (lo < hi) {
            const uint32_t space = group.maxStack > group.stacks[lo].count
                                       ? group.maxStack - group.stacks[lo].count
                                       : 0;
            if (space == 0) {
                ++lo;
                continue;
            }
            const bool wholeStack = space >= group.stacks[hi].count;
            const uint32_t moved =
                wholeStack ? group.stacks[hi].count : space;

            // Choose the packet from OUR model, never from the item's
            // live stack count. The whole batch goes out in one frame,
            // so no reply has arrived and every descriptor still holds
            // its original count while the model has moved on. Asking
            // the descriptor would send a split for what is really the
            // last of a stack, and the server rejects a split that would
            // empty its source ("Couldn't split those items").
            const bool sent =
                wholeStack
                    ? Item::Swap::ContainersFrom(
                          group.stacks[hi].item, group.stacks[hi].bag,
                          group.stacks[hi].slot, group.stacks[lo].bag,
                          group.stacks[lo].slot)
                    : Item::Swap::MoveCountFrom(
                          group.stacks[hi].item, group.stacks[hi].bag,
                          group.stacks[hi].slot, group.stacks[lo].bag,
                          group.stacks[lo].slot, static_cast<int>(moved));
            if (sent) {
                ++g_mergePackets;
                // Only a whole-stack transfer changes which slots are
                // occupied, which is all phase two's plan depends on. A
                // partial split changes no slot GUID, so nothing would
                // ever clear a lock on it.
                if (wholeStack)
                    emptied.push_back(group.stacks[hi].guid);
            }
            group.stacks[lo].count += moved;
            group.stacks[hi].count -= moved;
            if (group.stacks[hi].count == 0)
                --hi; // that stack is gone
            else
                ++lo; // this one is full
        }
    }

    // Locked only after the batch is out, so no sender sees a lock it set.
    for (const uint64_t guid : emptied)
        Lock(guid);
}

// --- phase two: plan a layout and fire the swaps ----------------------------

// Counters from the most recent Place(), read back by
// `_classicapi_SortBagsStats()`. They exist to tell apart the three ways
// a pass can fail to converge: swaps the engine refused to send, items
// the planner found no cell for, and a plan that was complete and fully
// sent yet still left the bags unsorted (which would mean a swap did
// something other than swap).
struct Stats {
    int entries;
    int cells;
    int pinned;
    int planned;
    int unplanned;
    int swapsSent;
    int swapsFailed;
    int sameItemAvoided;
    int generalCells;   // destination cells in general-purpose containers
    int specialtyCells; // cells confined to one bag family
    int familyItems;    // items that want a specialty bag
    int mergesPending;  // phase-one sources unanswered when placement ran
    int locksReleased;  // phase-two items unanswered at the deadline
};
Stats g_lastRun{};

// Returns true when it relocated anything, i.e. there are locks to wait on.
bool Place(const int *bags, int bagCount, int mergesPending) {
    Stats stats{};
    stats.mergesPending = mergesPending;
    std::vector<Entry> items;
    std::vector<Cell> general;

    // Vanilla items carry at most one family bit, so a flat list keyed by
    // family is enough — no bitmask intersection needed.
    struct FamilyPool {
        uint32_t family;
        std::vector<Cell> cells;
        size_t next;
    };
    std::vector<FamilyPool> specialty;

    for (int b = 0; b < bagCount; ++b) {
        const int bag = bags[b];
        BagView view;
        if (!ViewForBag(bag, &view))
            continue;
        for (int i = 0; i < view.slotCount; ++i) {
            const int slot = i + 1;
            uint64_t guid = 0;
            const uint8_t *item = ItemAt(view, i, &guid);

            const uint8_t *rec = nullptr;
            int itemID = 0;
            if (item != nullptr) {
                itemID = Item::ID::FromCGItem(item);
                if (itemID > 0 && !IsLocked(item))
                    rec = Item::PeekRecord(static_cast<uint32_t>(itemID));
                if (rec == nullptr) {
                    ++stats.pinned;
                    continue; // pinned: unrankable or immovable, slot withheld
                }
            }

            if (view.family == 0) {
                general.push_back({bag, slot});
            } else {
                FamilyPool *pool = nullptr;
                for (auto &p : specialty) {
                    if (p.family == view.family) {
                        pool = &p;
                        break;
                    }
                }
                if (pool == nullptr) {
                    specialty.push_back({view.family, {}, 0});
                    pool = &specialty.back();
                }
                pool->cells.push_back({bag, slot});
            }

            if (item == nullptr)
                continue; // an empty slot contributes a cell and nothing else

            Entry e{};
            e.item = item;
            e.guid = guid;
            e.startBag = e.curBag = bag;
            e.startSlot = e.curSlot = slot;
            e.destBag = -1;
            e.destSlot = -1;
            e.itemID = static_cast<uint32_t>(itemID);
            e.family = Item::BagFamily::BitmaskForRecord(rec);
            e.itemClass = Game::Read<uint32_t>(rec, Offsets::OFF_ITEMSTATS_CLASS);
            e.subClass = Game::Read<uint32_t>(rec, Offsets::OFF_ITEMSTATS_SUBCLASS);
            e.quality = Game::Read<uint32_t>(rec, Offsets::OFF_ITEMSTATS_QUALITY);
            e.slotRank = SlotRank(
                Game::Read<uint32_t>(rec, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE));
            e.count = StackCount(item);
            e.name = *reinterpret_cast<const char *const *>(
                rec + Offsets::OFF_ITEMSTATS_NAME);
            e.category = CategoryFor(e.itemID, e.itemClass, e.quality);
            items.push_back(e);
        }
    }

    stats.entries = static_cast<int>(items.size());
    stats.generalCells = static_cast<int>(general.size());
    for (const auto &p : specialty)
        stats.specialtyCells += static_cast<int>(p.cells.size());
    stats.cells = stats.generalCells + stats.specialtyCells;
    for (const auto &e : items)
        if (e.family != 0)
            ++stats.familyItems;

    if (items.empty()) {
        g_lastRun = stats;
        return false;
    }

    std::sort(items.begin(), items.end(), Precedes);

    // Right-to-left is expressed entirely as reversed cell lists: the
    // passes below still fill from the "front" and the "far end", those
    // ends have just swapped which physical slot they mean. So the
    // sorted run starts at the last slot of the last bag and junk
    // collects at the first slot of the first, both flipping together.
    if (RightToLeft()) {
        std::reverse(general.begin(), general.end());
        for (auto &p : specialty)
            std::reverse(p.cells.begin(), p.cells.end());
    }

    // Forward pass for everything but junk: a matching specialty bag
    // first, overflowing into the general cells. Junk then fills the
    // general cells from the far end, so it collects away from the rest
    // and the two passes cannot claim the same cell.
    size_t genFront = 0;
    size_t genBack = general.size();
    for (auto &e : items) {
        if (e.category == kCatJunk)
            continue;
        Cell *cell = nullptr;
        if (e.family != 0) {
            for (auto &p : specialty) {
                if (p.family == e.family && p.next < p.cells.size()) {
                    cell = &p.cells[p.next++];
                    break;
                }
            }
        }
        if (cell == nullptr && genFront < genBack)
            cell = &general[genFront++];
        if (cell == nullptr)
            continue; // nowhere left to put it; leave it where it is
        e.destBag = cell->bag;
        e.destSlot = cell->slot;
    }
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        if (it->category != kCatJunk)
            continue;
        if (genBack <= genFront)
            break;
        const Cell &cell = general[--genBack];
        it->destBag = cell.bag;
        it->destSlot = cell.slot;
    }

    // NEVER plan a swap between two stacks of the same item. The server's
    // swap handler tries a MERGE first and only falls through to a real
    // swap if the merge is impossible: if the two counts fit one stack it
    // collapses them into a single slot, and if they do not it
    // redistributes the counts and leaves both items where they are.
    // Neither is a swap, so the bookkeeping below would be wrong from
    // that point on and every later move in the batch would compound it.
    //
    // Two stacks of one item are interchangeable, though, so this costs
    // nothing: within each same-item run, hand each destination cell to
    // whichever member already occupies it. The run is contiguous because
    // identical items agree on every key ahead of count.
    for (size_t i = 0; i < items.size();) {
        size_t j = i;
        while (j < items.size() && items[j].itemID == items[i].itemID)
            ++j;
        for (size_t m = i; m < j; ++m) {
            for (size_t n = i; n < j; ++n) {
                if (n == m)
                    continue; // its own destination matching is just "home"
                if (items[n].destSlot < 0)
                    continue; // unassigned; nothing to trade
                if (items[n].destBag == items[m].curBag &&
                    items[n].destSlot == items[m].curSlot) {
                    std::swap(items[m].destBag, items[n].destBag);
                    std::swap(items[m].destSlot, items[n].destSlot);
                    ++stats.sameItemAvoided;
                    break; // m now targets the slot it is already in
                }
            }
        }
        i = j;
    }

    // Fire the batch. Each entry carries its live position, so the plan
    // stays valid as slots shuffle and nothing re-reads the bags. The
    // occupant of a destination takes the mover's old slot, which is what
    // the engine's swap does anyway.
    for (auto &e : items) {
        if (e.destSlot < 0) { // see the sentinel note on Entry
            ++stats.unplanned;
            continue;
        }
        ++stats.planned;
        if (e.destBag == e.curBag && e.destSlot == e.curSlot)
            continue;
        Entry *occupant = nullptr;
        for (auto &o : items) {
            if (&o != &e && o.curBag == e.destBag && o.curSlot == e.destSlot) {
                occupant = &o;
                break;
            }
        }
        const int fromBag = e.curBag, fromSlot = e.curSlot;
        if (!Item::Swap::ContainersFrom(e.item, fromBag, fromSlot, e.destBag,
                                        e.destSlot)) {
            ++stats.swapsFailed;
            continue;
        }
        ++stats.swapsSent;
        e.curBag = e.destBag;
        e.curSlot = e.destSlot;
        if (occupant != nullptr) {
            occupant->curBag = fromBag;
            occupant->curSlot = fromSlot;
        }
    }

    // Lock what ended up somewhere else. Its final slot held a different
    // GUID before, so the slot watcher's answer is guaranteed to change it
    // and clear the lock. An item that moved and came back is left
    // unlocked: its slot never changes, and nothing would clear it.
    bool moved = false;
    for (const auto &e : items) {
        if (e.curBag != e.startBag || e.curSlot != e.startSlot) {
            Lock(e.guid);
            moved = true;
        }
    }
    g_lastRun = stats;
    return moved;
}

// --- sequencing -------------------------------------------------------------

// How long the locks may sit with no answer before the phase gives up on
// the rest. Each answer restarts the wait, so this only has to cover one
// server round trip, not the whole batch.
constexpr uint32_t kSettleMs = 3000;

enum Phase { kIdle, kMerging, kPlacing };
Phase g_phase = kIdle;
size_t g_lastPending = 0;
uint32_t g_deadline = 0;

// Which set the in-flight sort is arranging, so the deferred placement
// pass knows what to walk. Owned, since the autosort exclusions hand in
// a filtered list that does not outlive the call.
std::vector<int> g_activeBags;

void BeginWait(Phase phase) {
    g_phase = phase;
    g_lastPending = g_waiting.size();
    g_deadline = Time::Clock::NowMs() + kSettleMs;
}

void RunPlacement(int mergesPending) {
    if (Place(g_activeBags.data(), static_cast<int>(g_activeBags.size()),
              mergesPending))
        BeginWait(kPlacing);
    else
        g_phase = kIdle;
}

// Ends the running phase once none of its items is still locked, or when
// the server has gone quiet past the deadline — then the stragglers are
// released through the engine's own clearer, so nothing stays greyed out
// and unusable on a lost reply.
void OnTick() {
    if (g_phase == kIdle)
        return;

    size_t pending = 0;
    for (const uint64_t guid : g_waiting)
        if (StillLocked(guid))
            ++pending;

    if (pending != 0) {
        const uint32_t now = Time::Clock::NowMs();
        if (pending != g_lastPending) {
            g_lastPending = pending;
            g_deadline = now + kSettleMs;
            return;
        }
        if (!Time::Clock::Reached(now, g_deadline))
            return;
        for (const uint64_t guid : g_waiting)
            if (StillLocked(guid))
                CallLockFn(Offsets::FUN_ITEM_UNLOCK_BY_GUID, guid);
    }
    g_waiting.clear();

    if (g_phase == kMerging)
        RunPlacement(static_cast<int>(pending));
    else {
        g_lastRun.locksReleased = static_cast<int>(pending);
        g_phase = kIdle;
    }
}

const Tick::WorldTick::AutoSubscribe _tick{&OnTick};

// A login or /reload ends whatever was in flight: after a logout the
// waited-on items are gone, which would read as settled and run the
// placement over the next character's bags.
void OnInGameInit() {
    g_phase = kIdle;
    g_waiting.clear();
}

const Game::ModuleAutoRegister _init{&OnInGameInit};

// Shared entry point. Bags and bank differ only in the list. A call while
// a sort is in flight — merging, placing, or waiting on the server for
// either — is ignored rather than queued, so a held-down keybind cannot
// stack them up, and neither can sorting bags and bank at once.
void StartSort(std::vector<int> bags) {
    if (g_phase != kIdle)
        return;
    g_activeBags = std::move(bags);
    g_mergePackets = 0;
    g_waiting.clear();
    Consolidate(g_activeBags.data(), static_cast<int>(g_activeBags.size()));
    if (!g_waiting.empty())
        BeginWait(kMerging); // let the merges land, then place
    else
        RunPlacement(0); // nothing changes occupancy; the layout reads clean
}

// Runs a sort over `bags`, dropping `excluded` when its autosort setting
// says to leave that container alone. Dropping a bag from the set is all
// it takes: the snapshot, the comparator and the placement only ever see
// the bags they are handed, so an excluded bag is neither read from nor
// written to.
template <size_t N>
void StartSortExcluding(const int (&bags)[N], int excluded, bool excludeIt) {
    std::vector<int> kept;
    kept.reserve(N);
    for (const int bag : bags) {
        if (!excludeIt || bag != excluded)
            kept.push_back(bag);
    }
    if (!kept.empty())
        StartSort(std::move(kept));
}

// `C_Container.SortBags()` / `C_Container.SortBankBags()` — no
// arguments, no return, matching the modern signatures. Fire-and-forget:
// the work spans several frames.
int __fastcall Script_C_Container_SortBags(void * /*L*/) {
    StartSortExcluding(kInventoryBags, /*excluded*/ 0, BackpackAutosortDisabled());
    return 0;
}

int __fastcall Script_C_Container_SortBankBags(void * /*L*/) {
    StartSortExcluding(kBankBags, /*excluded*/ -1, BankAutosortDisabled());
    return 0;
}

// `_classicapi_SortBagsStats()` -> entries, cells, pinned, planned,
// unplanned, swapsSent, swapsFailed, sameItemAvoided, generalCells,
// specialtyCells, familyItems, mergePackets, mergesPending, locksReleased —
// from the last sort. Diagnostic only; not part of the API surface.
int __fastcall Script_SortBagsStats(void *L) {
    Game::Lua::PushNumber(L, g_lastRun.entries);
    Game::Lua::PushNumber(L, g_lastRun.cells);
    Game::Lua::PushNumber(L, g_lastRun.pinned);
    Game::Lua::PushNumber(L, g_lastRun.planned);
    Game::Lua::PushNumber(L, g_lastRun.unplanned);
    Game::Lua::PushNumber(L, g_lastRun.swapsSent);
    Game::Lua::PushNumber(L, g_lastRun.swapsFailed);
    Game::Lua::PushNumber(L, g_lastRun.sameItemAvoided);
    Game::Lua::PushNumber(L, g_lastRun.generalCells);
    Game::Lua::PushNumber(L, g_lastRun.specialtyCells);
    Game::Lua::PushNumber(L, g_lastRun.familyItems);
    Game::Lua::PushNumber(L, g_mergePackets);
    Game::Lua::PushNumber(L, g_lastRun.mergesPending);
    Game::Lua::PushNumber(L, g_lastRun.locksReleased);
    return 14;
}

// `_classicapi_SortBagsBags(which)` -> bagID, slotCount, family for each
// bag that resolves in that set (0 = the inventory set, 1 = the bank
// set). A bag that does not resolve is skipped entirely, so a missing ID
// in the output is itself the answer. Diagnostic only.
int __fastcall Script_SortBagsBags(void *L) {
    const bool bank = Game::Lua::IsNumber(L, 1) &&
                      static_cast<int>(Game::Lua::ToNumber(L, 1)) == 1;
    const int *bags = bank ? kBankBags : kInventoryBags;
    const int count = bank
                          ? sizeof(kBankBags) / sizeof(kBankBags[0])
                          : sizeof(kInventoryBags) / sizeof(kInventoryBags[0]);
    int pushed = 0;
    for (int b = 0; b < count; ++b) {
        BagView view;
        if (!ViewForBag(bags[b], &view))
            continue;
        Game::Lua::PushNumber(L, bags[b]);
        Game::Lua::PushNumber(L, view.slotCount);
        Game::Lua::PushNumber(L, static_cast<double>(view.family));
        pushed += 3;
    }
    return pushed;
}

// `C_Container.SetSortBagsRightToLeft(enable)` — no return, matching the
// modern signature. Takes effect on the next sort; it does not re-sort.
int __fastcall Script_SetSortBagsRightToLeft(void *L) {
    CVar::Factory::SetInt(g_rightToLeftCVar,
                          Game::Lua::ToBoolean(L, 1) != 0 ? 1 : 0);
    return 0;
}

// `C_Container.GetSortBagsRightToLeft()` -> isEnabled
int __fastcall Script_GetSortBagsRightToLeft(void *L) {
    Game::Lua::PushBool(L, RightToLeft());
    return 1;
}

// `C_Container.SetBackpackAutosortDisabled(disable)` /
// `C_Container.SetBankAutosortDisabled(disable)` — keep that container
// out of `SortBags` / `SortBankBags`. No return, matching the modern
// signatures; the paired getters report the stored setting.
int __fastcall Script_SetBackpackAutosortDisabled(void *L) {
    CVar::Factory::SetInt(g_backpackAutosortCVar,
                          Game::Lua::ToBoolean(L, 1) != 0 ? 1 : 0);
    return 0;
}

int __fastcall Script_GetBackpackAutosortDisabled(void *L) {
    Game::Lua::PushBool(L, BackpackAutosortDisabled());
    return 1;
}

int __fastcall Script_SetBankAutosortDisabled(void *L) {
    CVar::Factory::SetInt(g_bankAutosortCVar,
                          Game::Lua::ToBoolean(L, 1) != 0 ? 1 : 0);
    return 0;
}

int __fastcall Script_GetBankAutosortDisabled(void *L) {
    Game::Lua::PushBool(L, BankAutosortDisabled());
    return 1;
}

const Game::Doc::Function kSortBags{
    "Sorts the items in the backpack and the equipped bags.", {}, {}};
const Game::Doc::Function kSortBankBags{
    "Sorts the items in the bank and the bank bags; does nothing while the bank is closed.",
    {}, {}};

const Game::Doc::Field kRightToLeftArgs[] = {
    Game::Doc::Req("enable", "bool", "True to fill from the last slot of the last bag."),
};
const Game::Doc::Field kRightToLeftRets[] = {
    Game::Doc::Req("isEnabled", "bool"),
};
const Game::Doc::Function kSetSortBagsRightToLeft{
    "Sets the direction in which a sort fills the bags.", kRightToLeftArgs, {}};
const Game::Doc::Function kGetSortBagsRightToLeft{
    "Whether a sort fills the bags from the last slot of the last bag.", {},
    kRightToLeftRets};

const Game::Doc::Field kDisableArgs[] = {
    Game::Doc::Req("disable", "bool", "True to leave the container out of sorts."),
};
const Game::Doc::Field kDisableRets[] = {
    Game::Doc::Req("isDisabled", "bool"),
};
const Game::Doc::Function kSetBackpackAutosortDisabled{
    "Sets whether SortBags leaves the backpack alone.", kDisableArgs, {}};
const Game::Doc::Function kGetBackpackAutosortDisabled{
    "Whether SortBags leaves the backpack alone.", {}, kDisableRets};
const Game::Doc::Function kSetBankAutosortDisabled{
    "Sets whether SortBankBags leaves the main bank container alone.", kDisableArgs, {}};
const Game::Doc::Function kGetBankAutosortDisabled{
    "Whether SortBankBags leaves the main bank container alone.", {}, kDisableRets};

void RegisterLuaFunctions() {
    // Dedups by name, so re-registering each reload is harmless.
    // Retail has no cvars by these names, so there is no help line to copy.
    constexpr int kCategory = Offsets::CVAR_CATEGORY_GAME;
    g_rightToLeftCVar = CVar::Factory::Register("sortBagsRightToLeft", nullptr, kCategory, "0");
    g_backpackAutosortCVar =
        CVar::Factory::Register("backpackAutosortDisabled", nullptr, kCategory, "0");
    g_bankAutosortCVar = CVar::Factory::Register("bankAutosortDisabled", nullptr, kCategory, "0");

    Game::Lua::RegisterTableFunction("C_Container", "SortBags",
                                     &Script_C_Container_SortBags, &kSortBags);
    Game::Lua::RegisterTableFunction("C_Container", "SetSortBagsRightToLeft",
                                     &Script_SetSortBagsRightToLeft,
                                     &kSetSortBagsRightToLeft);
    Game::Lua::RegisterTableFunction("C_Container", "GetSortBagsRightToLeft",
                                     &Script_GetSortBagsRightToLeft,
                                     &kGetSortBagsRightToLeft);
    Game::Lua::RegisterTableFunction("C_Container", "SortBankBags",
                                     &Script_C_Container_SortBankBags, &kSortBankBags);
    Game::Lua::RegisterTableFunction("C_Container", "SetBackpackAutosortDisabled",
                                     &Script_SetBackpackAutosortDisabled,
                                     &kSetBackpackAutosortDisabled);
    Game::Lua::RegisterTableFunction("C_Container", "GetBackpackAutosortDisabled",
                                     &Script_GetBackpackAutosortDisabled,
                                     &kGetBackpackAutosortDisabled);
    Game::Lua::RegisterTableFunction("C_Container", "SetBankAutosortDisabled",
                                     &Script_SetBankAutosortDisabled,
                                     &kSetBankAutosortDisabled);
    Game::Lua::RegisterTableFunction("C_Container", "GetBankAutosortDisabled",
                                     &Script_GetBankAutosortDisabled,
                                     &kGetBankAutosortDisabled);
    Game::Lua::RegisterGlobalFunction("_classicapi_SortBagsStats",
                                      &Script_SortBagsStats);
    Game::Lua::RegisterGlobalFunction("_classicapi_SortBagsBags",
                                      &Script_SortBagsBags);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::SortBags
