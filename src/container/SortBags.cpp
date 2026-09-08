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
// PHASE ONE consolidates partial stacks, and hands that entirely to the
// server: one `CMSG_AUTOSTORE_BAG_ITEM` per redundant stack, which
// merges into existing stacks of the same item and splits across several
// of them when that is what fits. Nothing here computes what fits, so
// nothing here needs an item's max stack size — which matters, because
// that number is unavailable until the item's data has arrived from the
// server, and a merge that sizes stacks first silently skips the ones it
// could not size.
//
// PHASE TWO places items. It runs on the next `Bag::UpdateDelayed` — the
// engine's own "a bag changed this frame" signal — so the merges are
// settled before anything is read. It plans the whole layout up front
// and then tracks each item's live position as swaps fire, rather than
// re-reading a layout it is in the middle of mutating. Our swap is
// fire-and-forget and sets no client-side item lock, so the whole batch
// goes out in one frame.
//
// Neither phase may touch the Lua stack: phase two runs from a bag-update
// callback, outside any Lua call, where the engine may have its own
// values on the stack. Hence the `*From` senders and the direct invMgr
// walk rather than `ResolveBag`.
//
// THE ORDERING IS OURS. Retail computes it in code we cannot read, and
// its categories (per-bag assignment flags, expansion filters) have no
// counterpart here, so this is a defensible order rather than a
// reproduction of Blizzard's: hearthstone, gear by descending quality,
// consumables, reagents, trade goods, quest items, everything else by
// quality, junk last. Within a category, by item class, subclass, name,
// then fuller stacks first.
//
// An item whose data has NOT arrived is pinned: left where it is, with
// its slot withheld from the destination pool so nothing else is planned
// into it. It cannot be ranked without a class, quality and name, and
// guessing scatters it. A later call places it once the data lands. Phase
// one is unaffected, since it needs only the item ID.

#include "Game.h"
#include "Offsets.h"
#include "bag/UpdateDelayed.h"
#include "cvar/Factory.h"
#include "item/BagFamily.h"
#include "item/CGItem.h"
#include "item/ID.h"
#include "item/Location.h"
#include "item/Record.h"
#include "item/Swap.h"
#include "object/Resolve.h"
#include "unit/Identity.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Container::SortBags {

namespace {

// The two sets this can arrange. Both go through the same snapshot,
// comparator and placement; they differ only in how phase one merges
// (see ConsolidateViaServer / ConsolidateLocally).
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

// The source slots phase one asked the server to empty. Phase two counts
// how many are still occupied when it runs: any at all means it is
// planning against a half-merged snapshot, because the server's replies
// did not all arrive in the bag update that woke it.
struct SourceSlot {
    int bag, slot;
};
std::vector<SourceSlot> g_mergeSources;

// Item.dbc `m_class` values we rank on. Per-module codes, the way
// `Item::BagFamily` keeps its own.
constexpr uint32_t kClassConsumable = 0;
constexpr uint32_t kClassWeapon = 2;
constexpr uint32_t kClassArmor = 4;
constexpr uint32_t kClassReagent = 5;
constexpr uint32_t kClassTradeGoods = 7;
constexpr uint32_t kClassQuest = 12;

constexpr uint32_t kQualityPoor = 0;
constexpr uint32_t kQualityUncommon = 2;
constexpr uint32_t kQualityRare = 3;
constexpr uint32_t kQualityEpic = 4;

// Primary key. Lower sorts nearer the first slot.
enum Category : uint8_t {
    kCatHearthstone = 0,
    kCatGearEpic,
    kCatGearRare,
    kCatGearUncommon,
    kCatGearPlain,
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
    if (itemClass == kClassWeapon || itemClass == kClassArmor) {
        if (quality >= kQualityEpic)
            return kCatGearEpic;
        if (quality == kQualityRare)
            return kCatGearRare;
        if (quality == kQualityUncommon)
            return kCatGearUncommon;
        return kCatGearPlain;
    }
    if (itemClass == kClassConsumable)
        return kCatConsumable;
    if (itemClass == kClassReagent)
        return kCatReagent;
    if (itemClass == kClassTradeGoods)
        return kCatTradeGoods;
    if (itemClass == kClassQuest)
        return kCatQuest;
    if (quality >= kQualityEpic)
        return kCatOtherEpic;
    if (quality == kQualityRare)
        return kCatOtherRare;
    if (quality == kQualityUncommon)
        return kCatOtherUncommon;
    return kCatOtherPlain;
}

struct Entry {
    const void *item;    // CGItem — stable across the swap batch
    int curBag, curSlot; // live position, updated as swaps fire

    // Destination, or unassigned. The unassigned sentinel is destSLOT,
    // never destBag: slots are 1-based so a negative one is impossible,
    // whereas bagID -1 is the main bank. Testing destBag < 0 here treats
    // every item bound for the main bank as unplaced and silently drops
    // its move.
    int destBag, destSlot;
    uint32_t itemID;
    uint32_t family; // the item's own bag family (0 = fits anywhere)
    uint32_t itemClass, subClass, count;
    const char *name;
    uint8_t category;
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
    if (a.subClass != b.subClass)
        return a.subClass < b.subClass;
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
bool ViewForBag(int bagID, BagView *out) {
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

    // Resolve the bag item that owns the container. Bank bags resolve
    // only once the bank window has been opened this session, which is
    // the right gate: the server refuses bank moves without it anyway,
    // so a bank sort attempted with the bank closed finds no cells and
    // does nothing rather than firing moves that would all be rejected.
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

const uint8_t *ItemAt(const BagView &v, int slotIndex0) {
    auto *guids = *reinterpret_cast<const uint64_t *const *>(
        v.invMgr + Offsets::OFF_INVMGR_GUID_ARRAY);
    if (guids == nullptr)
        return nullptr;
    const uint64_t guid = guids[v.firstLinear + slotIndex0];
    if (guid == 0)
        return nullptr;
    return static_cast<const uint8_t *>(
        Object::ByGuid(Offsets::TYPEMASK_ITEM, guid, "SortBags", 0));
}

uint32_t StackCount(const uint8_t *item) {
    auto *fields = Item::ObjectFields(item);
    if (fields == nullptr)
        return 1;
    const uint32_t n =
        Game::Read<uint32_t>(fields, Offsets::OFF_DESCRIPTOR_STACK_COUNT);
    return n == 0 ? 1 : n;
}

// --- phase one: ask the server to consolidate -------------------------------

// Returns true if any packet went out. Every stack after the first of
// its item ID is offered to the server, which merges what fits and
// rehomes the rest.
//
// When the item's record is cached we skip the ones that provably cannot
// stack, purely to save a packet. When it is NOT cached we send anyway:
// the alternative is silently skipping a merge we could not size, which
// is the failure this whole design avoids.
// One stack per group of the same item, as gathered for either merge.
struct Stack {
    const uint8_t *item;
    int bag, slot;
    uint32_t count;
};
struct Group {
    uint32_t itemID;
    uint32_t maxStack; // 0 when the item's data has not arrived
    std::vector<Stack> stacks;
};

// Collects every stackable item across `bags`, grouped by item ID.
// `maxStack` is 0 for an item whose data has not arrived; the callers
// differ in what they do about that.
std::vector<Group> GatherGroups(const int *bags, int bagCount) {
    std::vector<Group> groups;
    for (int b = 0; b < bagCount; ++b) {
        const int bag = bags[b];
        BagView view;
        if (!ViewForBag(bag, &view))
            continue;
        for (int i = 0; i < view.slotCount; ++i) {
            const uint8_t *item = ItemAt(view, i);
            if (item == nullptr)
                continue;
            const int itemID = Item::ID::FromCGItem(item);
            if (itemID <= 0)
                continue;
            const uint32_t id = static_cast<uint32_t>(itemID);

            uint32_t maxStack = 0;
            auto *rec = Item::PeekRecord(id);
            if (rec != nullptr) {
                maxStack =
                    Game::Read<uint32_t>(rec, Offsets::OFF_ITEMSTATS_STACKABLE);
                if (maxStack <= 1)
                    continue; // cannot stack at all, so never a merge candidate
            }

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
            group->stacks.push_back({item, bag, i + 1, StackCount(item)});
        }
    }
    return groups;
}

// Why this is computed here rather than delegated to the server's own
// autostore, which can merge a stack into several destinations for one
// packet and would otherwise be cheaper:
//
// `CMSG_AUTOSTORE_BAG_ITEM` names its destination with a container byte,
// and the only value the engine's builder will emit for "the player" is
// `INVENTORY_SLOT_BAG_0` (255). Server-side that is NOT the search-every-
// bag path — that one is `NULL_BAG` (0), which the builder cannot
// express. 255 takes the specific-container branch, which searches the
// keyring and the backpack and nothing else. So autostore can never
// merge a stack in one equipped bag with a stack in another, which is
// most of the real cases.
//
// Addressing both endpoints explicitly costs a packet per transfer
// instead of per emptied source — occasionally one more, when a stack
// has to be split across several destinations — and works everywhere.

// Bank consolidation has to be computed here. `CMSG_AUTOSTORE_BANK_ITEM`
// always CROSSES the inventory/bank boundary — a bank-sourced item
// travels out to the bags — so there is no "find this a fuller stack
// inside the bank" packet to delegate to. This is the one place the
// protocol forces a client-side merge, and it carries the cost that
// comes with that: it needs each item's max stack size, which is
// unavailable until that item's data has arrived, so a stack we cannot
// size is left alone.
//
// Two rolling pointers per item: stacks fullest-first, then poured from
// the back into the front until they meet.
bool Consolidate(const int *bags, int bagCount) {
    std::vector<Group> groups = GatherGroups(bags, bagCount);

    bool fired = false;
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
            const uint32_t space = group.maxStack - group.stacks[lo].count;
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
                fired = true;
                ++g_mergePackets;
                // Only a whole-stack transfer empties its source, and
                // `mergesPending` means "sources that should be empty by
                // now but are not".
                if (wholeStack)
                    g_mergeSources.push_back(
                        {group.stacks[hi].bag, group.stacks[hi].slot});
            }
            group.stacks[lo].count += moved;
            group.stacks[hi].count -= moved;
            if (group.stacks[hi].count == 0)
                --hi; // that stack is gone
            else
                ++lo; // this one is full
        }
    }
    return fired;
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
    int mergesPending;  // phase-one sources still occupied at plan time
};
Stats g_lastRun{};

void Place(const int *bags, int bagCount) {
    Stats stats{};
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
            const uint8_t *item = ItemAt(view, i);

            const uint8_t *rec = nullptr;
            int itemID = 0;
            if (item != nullptr) {
                itemID = Item::ID::FromCGItem(item);
                if (itemID > 0)
                    rec = Item::PeekRecord(static_cast<uint32_t>(itemID));
                if (rec == nullptr) {
                    ++stats.pinned;
                    continue; // pinned: unrankable, and its slot is withheld
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
            e.curBag = bag;
            e.curSlot = slot;
            e.destBag = -1;
            e.destSlot = -1;
            e.itemID = static_cast<uint32_t>(itemID);
            e.family = Item::BagFamily::BitmaskForRecord(rec);
            e.itemClass = Game::Read<uint32_t>(rec, Offsets::OFF_ITEMSTATS_CLASS);
            e.subClass = Game::Read<uint32_t>(rec, Offsets::OFF_ITEMSTATS_SUBCLASS);
            e.count = StackCount(item);
            e.name = *reinterpret_cast<const char *const *>(
                rec + Offsets::OFF_ITEMSTATS_NAME);
            e.category = CategoryFor(
                e.itemID, e.itemClass,
                Game::Read<uint32_t>(rec, Offsets::OFF_ITEMSTATS_QUALITY));
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

    // Any source phase one asked the server to empty that is still
    // occupied means this snapshot predates some of the merges.
    for (const auto &src : g_mergeSources) {
        for (const auto &e : items) {
            if (e.curBag == src.bag && e.curSlot == src.slot) {
                ++stats.mergesPending;
                break;
            }
        }
    }

    if (items.empty()) {
        g_lastRun = stats;
        return;
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
    g_lastRun = stats;
}

// --- sequencing -------------------------------------------------------------

enum Phase { kIdle, kAwaitingMerge };
Phase g_phase = kIdle;

// Which set the in-flight sort is arranging, so the deferred placement
// pass knows what to walk. Points at one of the two static lists, so
// there is nothing to own or free.
const int *g_activeBags = nullptr;
int g_activeBagCount = 0;

void OnBagUpdate() {
    if (g_phase != kAwaitingMerge)
        return;
    // Clear the phase BEFORE placing: the swaps we are about to fire will
    // signal this callback again next frame, and we must be idle by then
    // or the sort would restart forever.
    g_phase = kIdle;
    Place(g_activeBags, g_activeBagCount);
}

const Bag::UpdateDelayed::AutoSubscribe _bagSub{&OnBagUpdate};

// Shared entry point. Bags and bank differ only in the list.
void StartSort(const int *bags, int bagCount) {
    if (g_phase != kIdle)
        return; // a sort is already in flight
    g_activeBags = bags;
    g_activeBagCount = bagCount;
    g_mergePackets = 0;
    g_mergeSources.clear();
    if (Consolidate(bags, bagCount))
        g_phase = kAwaitingMerge; // let the merges land, then place
    else
        Place(bags, bagCount); // nothing to merge; the layout reads clean
}

// `C_Container.SortBags()` / `C_Container.SortBankBags()` — no
// arguments, no return, matching the modern signatures. Fire-and-forget:
// the work spans at least two frames. A call while a sort is already in
// flight is ignored rather than queued, so a held-down keybind cannot
// stack them up — and neither can sorting bags and bank at once.
// Runs a sort over `bags`, dropping `excluded` when its autosort setting
// says to leave that container alone. Dropping a bag from the set is all
// it takes: the snapshot, the comparator and the placement only ever see
// the bags they are handed, so an excluded bag is neither read from nor
// written to.
void StartSortExcluding(const int *bags, int count, int excluded,
                        bool excludeIt) {
    if (!excludeIt) {
        StartSort(bags, count);
        return;
    }
    std::vector<int> kept;
    kept.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        if (bags[i] != excluded)
            kept.push_back(bags[i]);
    }
    if (!kept.empty())
        StartSort(kept.data(), static_cast<int>(kept.size()));
}

int __fastcall Script_C_Container_SortBags(void * /*L*/) {
    StartSortExcluding(kInventoryBags,
                       sizeof(kInventoryBags) / sizeof(kInventoryBags[0]),
                       /*excluded*/ 0, BackpackAutosortDisabled());
    return 0;
}

int __fastcall Script_C_Container_SortBankBags(void * /*L*/) {
    StartSortExcluding(kBankBags, sizeof(kBankBags) / sizeof(kBankBags[0]),
                       /*excluded*/ -1, BankAutosortDisabled());
    return 0;
}

// `_classicapi_SortBagsStats()` -> entries, cells, pinned, planned,
// unplanned, swapsSent, swapsFailed, sameItemAvoided — from the last
// placement pass. Diagnostic only; not part of the API surface.
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
    return 13;
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

void RegisterLuaFunctions() {
    // Dedups by name, so re-registering each reload is harmless.
    g_rightToLeftCVar = CVar::Factory::Register("sortBagsRightToLeft", "0");
    g_backpackAutosortCVar =
        CVar::Factory::Register("backpackAutosortDisabled", "0");
    g_bankAutosortCVar = CVar::Factory::Register("bankAutosortDisabled", "0");

    Game::Lua::RegisterTableFunction("C_Container", "SortBags",
                                     &Script_C_Container_SortBags);
    Game::Lua::RegisterTableFunction("C_Container", "SetSortBagsRightToLeft",
                                     &Script_SetSortBagsRightToLeft);
    Game::Lua::RegisterTableFunction("C_Container", "GetSortBagsRightToLeft",
                                     &Script_GetSortBagsRightToLeft);
    Game::Lua::RegisterTableFunction("C_Container", "SortBankBags",
                                     &Script_C_Container_SortBankBags);
    Game::Lua::RegisterTableFunction("C_Container", "SetBackpackAutosortDisabled",
                                     &Script_SetBackpackAutosortDisabled);
    Game::Lua::RegisterTableFunction("C_Container", "GetBackpackAutosortDisabled",
                                     &Script_GetBackpackAutosortDisabled);
    Game::Lua::RegisterTableFunction("C_Container", "SetBankAutosortDisabled",
                                     &Script_SetBankAutosortDisabled);
    Game::Lua::RegisterTableFunction("C_Container", "GetBankAutosortDisabled",
                                     &Script_GetBankAutosortDisabled);
    Game::Lua::RegisterGlobalFunction("_classicapi_SortBagsStats",
                                      &Script_SortBagsStats);
    Game::Lua::RegisterGlobalFunction("_classicapi_SortBagsBags",
                                      &Script_SortBagsBags);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::SortBags
