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

// `C_NewItems` — the modern "new item glow" bookkeeping system. Retail
// flags items freshly added to your bags as "new" (the sparkle/flash on
// the bag button) and clears the flag once you've acknowledged them.
// Vanilla 1.12 never had the feature — no DBC, no server data, no engine
// bit; it's pure client-side bookkeeping over bag contents. So we add it,
// entirely in C++ the same way retail's client owns it — no addon Lua.
//
// Public surface (matches retail exactly):
//   C_NewItems.IsNewItem(bagID, slotIndex)   -> bool
//   C_NewItems.RemoveNewItem(bagID, slotIndex)
//   C_NewItems.ClearAll()
//
// Newness is keyed on each item's **instance GUID** (read off the CGItem),
// not on (bagID, slotIndex). That's what makes a flag survive the player
// rearranging a bag — exactly like retail, and impossible for a pure-Lua
// addon (vanilla exposes no per-instance GUID to Lua).
//
// The feed is event-driven, not polled: it hangs off `Bag::UpdateDelayed`,
// which co-hooks the engine's three `BAG_UPDATE` fire sites and signals once
// per frame in which a bag actually changed. On that signal we scan bags 0..4,
// flag any resident GUID that isn't in the owned "seen-set", reconcile the
// seen-set against everything owned now, and prune flags for items that have
// genuinely left. `BAG_NEW_ITEMS_UPDATED` (no payload — matching retail) fires
// whenever the new-item set changes. Nothing runs on quiet frames.
//
// "New" is therefore "an item object you did not own before": loot, crafts,
// purchases, rewards, mail and trades all qualify, and loot that lands on an
// existing stack does not (the stack keeps its GUID). The one object that is
// unowned but not acquired is the half of a split stack; see the split
// section below.
//
// The seen-set is deliberately NOT rebuilt from each scan: a bag update in
// progress transiently drops items from the read — sometimes the entire
// backpack at once, for a frame or two — and a naive "seen = this scan"
// approach would then re-flag the momentarily-missing item as new when it
// reappears (the exact bug this system had). Instead each entry carries a
// consecutive-absent-scan counter (`SeenReconcile`): present items reset it to
// 0, absent ones age by one, and only items absent `kMaxMissStreak` scans
// running are evicted. Counting scans (not wall-clock) means a pause with no
// bag activity never ages an owned item; any reappearance resets the streak.
// This keeps the set BOUNDED to ~currently-owned items — genuinely-removed
// items age out — while absorbing the transient reads.
//
// The one exception is establishing the login baseline: a lightweight
// WorldTick handler waits ~1.5s after the player is first resolvable (long
// enough for the initial bag population to finish), takes a single scan as the
// baseline, and then goes idle — items already owned at login are therefore
// never flagged. It re-arms on player-GUID change (relog / character switch).
//
// The "already owned" baseline spans **everything the player owns** — bags,
// worn equipment (paperdoll + equipped bags), AND the bank (main + bank bags)
// — even though only *bag* items are ever flagged new. That's what stops a
// move INTO a bag from any of those (unequip, or a pull from the bank) from
// looking like a fresh loot: the item's GUID was already in the owned set, so
// the seen-set treats it as already-owned, not new. The bank is reachable without the bank
// window open because its GUIDs are read straight from the invMgr array (the
// same gate-bypass `C_Item.GetItemCount` uses; the array is server-populated
// at login).
//
// Bag enumeration is pure C++: it replicates what `PackBagSlot` does
// internally (see Offsets.h). The backpack is indexed straight off the
// player inventory manager; each equipped bag 1..4 is resolved to its
// container object and inventory via the same GUID getter + vtable call the
// engine uses, then walked with `GetItemBySlot`. None of the Lua-stack-bound
// helpers are touched, so the scan is safe from the frame-loop tick.

#include "Game.h"
#include "Offsets.h"
#include "bag/UpdateDelayed.h"
#include "event/Custom.h"
#include "item/CGItem.h"
#include "item/Location.h"
#include "net/PacketDispatch.h"
#include "net/SendObserver.h"
#include "object/Resolve.h"
#include "tick/WorldTick.h"
#include "time/Clock.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Item::NewItems {

namespace {

constexpr const char *kEvtNewItems = "BAG_NEW_ITEMS_UPDATED";
const Game::Doc::Event kNewItemsDoc{
    "NewItems", "Fires when an item gains or loses its new flag.", {}};
const Event::Custom::AutoReserve _r{kEvtNewItems, &kNewItemsDoc};

// Sized for the whole owned universe the baseline tracks — backpack + four
// bags + 19 worn slots + 4 equipped bags + main bank (24) + six bank bags.
// A full inventory-and-bank can push ~300, so 512 leaves generous headroom.
// Overflow silently drops (AddUnique bails at the cap) — a dropped GUID just
// can't be flagged/recognized, never a crash.
constexpr int kMaxItems = 512;

// Delay after the player is first resolvable before the baseline is taken,
// giving the initial bag population time to finish. Wall-clock (via the
// engine ms counter) so it's independent of frame rate.
constexpr uint32_t kSettleMs = 1500;

// Consecutive bag-change scans an item may be absent from the read before it's
// dropped from the seen-set. A bag update in progress transiently drops items
// from the scan — sometimes the whole backpack at once — for a frame or two;
// counting SCANS (not wall-clock) means a pause with no bag activity never ages
// an owned item, and any reappearance resets the streak to 0, so a transient
// miss never reaches the threshold. Genuinely-removed items hit it within a few
// subsequent bag changes and age out — that's what keeps the set BOUNDED to
// ~currently-owned items rather than everything ever owned.
constexpr int kMaxMissStreak = 4;

constexpr int kMaxContainerSlots = 64;

// One seen-set entry: an owned item's GUID and how many consecutive scans it
// has been absent from the read (0 = present this scan).
struct SeenEntry {
    uint64_t guid;
    int miss;
};
SeenEntry g_seen[kMaxItems];
int g_seenCount = 0;
uint64_t g_new[kMaxItems]; // subset currently flagged "new"
int g_newCount = 0;
bool g_seeded = false;     // true once the login baseline has been taken
uint64_t g_playerGuid = 0; // re-arm the baseline when this changes (relog)
uint32_t g_loginMs = 0;    // engine ms at which the current player resolved

using Time::Clock::NowMs;

bool Contains(const uint64_t *arr, int count, uint64_t guid) {
    for (int i = 0; i < count; ++i)
        if (arr[i] == guid)
            return true;
    return false;
}

bool AddUnique(uint64_t *arr, int *count, uint64_t guid) {
    if (Contains(arr, *count, guid) || *count >= kMaxItems)
        return false;
    arr[(*count)++] = guid;
    return true;
}

// --- seen-set (bounded, absent-scan-count evicted) -----------------------

bool SeenContains(uint64_t guid) {
    for (int i = 0; i < g_seenCount; ++i)
        if (g_seen[i].guid == guid)
            return true;
    return false;
}

// One scan's reconcile: age every entry by one missed scan, then reset the
// streak (and insert) for everything owned right now, then evict entries that
// have been absent `kMaxMissStreak` scans running. `owned` is the union of the
// current bag scan and non-bag owned items (equipment + bank).
void SeenReconcile(const uint64_t *owned, int ownedCount) {
    for (int i = 0; i < g_seenCount; ++i)
        ++g_seen[i].miss;
    for (int j = 0; j < ownedCount; ++j) {
        const uint64_t guid = owned[j];
        if (guid == 0)
            continue;
        bool found = false;
        for (int i = 0; i < g_seenCount; ++i)
            if (g_seen[i].guid == guid) {
                g_seen[i].miss = 0;
                found = true;
                break;
            }
        if (!found && g_seenCount < kMaxItems)
            g_seen[g_seenCount++] = {guid, 0};
    }
    for (int i = 0; i < g_seenCount;) {
        if (g_seen[i].miss >= kMaxMissStreak)
            g_seen[i] = g_seen[--g_seenCount];
        else
            ++i;
    }
}

bool RemoveFrom(uint64_t *arr, int *count, uint64_t guid) {
    for (int i = 0; i < *count; ++i) {
        if (arr[i] == guid) {
            arr[i] = arr[--(*count)];
            return true;
        }
    }
    return false;
}

uint64_t ItemGUID(const uint8_t *item) {
    if (item == nullptr)
        return 0;
    auto *instance = Item::InstanceBlock(item);
    if (instance == nullptr)
        return 0;
    return *reinterpret_cast<const uint64_t *>(
        instance + Offsets::OFF_INSTANCE_BLOCK_GUID);
}

// --- pure-C++ bag enumeration (no Lua stack) -----------------------------

using GetItemBySlot_t = void *(__thiscall *)(void *inventory, int slot0Based);

// Collects the instance GUIDs of every item in bags 0..4 into `out`.
// Returns the count. Pure C++ — safe to call off the frame-loop tick.
int ScanBags(const uint8_t *player, uint64_t *out) {
    auto getItemBySlot =
        reinterpret_cast<GetItemBySlot_t>(Offsets::FUN_ITEMMGR_GET_ITEM_BY_SLOT);
    int count = 0;

    // Backpack (bagID 0): indexed directly off the player inventory manager
    // at linear slot (S-1) + 23. GetItemBySlot only reads the manager, so the
    // const_cast to satisfy its `this` parameter is safe.
    auto *invMgr = const_cast<uint8_t *>(player) + Offsets::OFF_PLAYER_INVENTORY_MANAGER;
    const int backpackSlots = Item::Location::GetBagSlotCount(0);
    for (int slot = 1; slot <= backpackSlots; ++slot) {
        auto *item = static_cast<const uint8_t *>(
            getItemBySlot(invMgr, Offsets::BACKPACK_LINEAR_BASE + slot - 1));
        const uint64_t guid = ItemGUID(item);
        if (guid != 0)
            AddUnique(out, &count, guid);
    }

    // Equipped bags 1..4: resolve each bag's inventory object, then walk it.
    for (int bagID = 1; bagID <= 4; ++bagID) {
        const int slots = Item::Location::GetBagSlotCount(bagID);
        if (slots <= 0)
            continue;
        void *bagInv = Item::Location::EquippedBagInventory(bagID);
        if (bagInv == nullptr)
            continue;
        for (int slot = 1; slot <= slots; ++slot) {
            auto *item = static_cast<const uint8_t *>(getItemBySlot(bagInv, slot - 1));
            const uint64_t guid = ItemGUID(item);
            if (guid != 0)
                AddUnique(out, &count, guid);
        }
    }
    return count;
}

// Appends GUIDs from an invMgr's GUID array (the flat `+OFF_INVMGR_GUID_ARRAY`
// list `GetItemBySlot` indexes) over linear slots [first, last]. Reading the
// array directly is how we reach BANK slots without the bank window open —
// `GetItemBySlot` gates slots 39..68 on a live banker GUID, but the array
// itself is server-populated at login (same bypass `C_Item.GetItemCount`
// uses). We only need the GUID here, not the resolved object.
void AppendGuidArray(const uint8_t *invMgr, int first, int last, uint64_t *out,
                     int *count) {
    if (invMgr == nullptr)
        return;
    auto *arr = *reinterpret_cast<const uint64_t *const *>(
        invMgr + Offsets::OFF_INVMGR_GUID_ARRAY);
    if (arr == nullptr)
        return;
    for (int slot = first; slot <= last; ++slot)
        if (arr[slot] != 0)
            AddUnique(out, count, arr[slot]);
}

const uint8_t *ResolveByGuid(int type, uint64_t guid) {
    if (guid == 0)
        return nullptr;
    return static_cast<const uint8_t *>(
        Object::ByGuid(type, guid, "ItemMgr", 0x172));
}

// Appends every item the player owns that ISN'T in bags 0..4 — worn
// equipment (paperdoll 1..19 + the four equipped-bag containers 20..23),
// the main bank (24 slots), and each bank bag's contents. Used only to widen
// the "already owned" baseline so a move INTO a bag from any of these
// (unequip, or a pull from the bank) reads as a move, not a fresh loot. We
// never *flag* these as new — this only feeds the seen-set. Pure C++ (no Lua
// stack) — safe off the frame tick.
void AppendNonBagOwned(uint64_t *out, int *count) {
    // Worn equipment + equipped bags: GetItemBySlot works for these (no
    // banker gate), so go through the normal per-slot resolver.
    for (int slot = Offsets::EQUIPMENT_SLOT_FIRST; slot <= Offsets::INVSLOT_BAG1 + 3;
         ++slot) {
        const uint64_t guid = ItemGUID(Item::Location::ResolveEquipmentSlot(slot));
        if (guid != 0)
            AddUnique(out, count, guid);
    }

    const uint8_t *playerInvMgr = Unit::Identity::PlayerInventoryManager();
    if (playerInvMgr == nullptr)
        return;
    // Main bank slots 39..62 live in the player invMgr's own GUID array.
    AppendGuidArray(playerInvMgr, Offsets::INVMGR_BANK_MAIN_FIRST_SLOT,
                    Offsets::INVMGR_BANK_MAIN_LAST_SLOT, out, count);
    // Bank bags (containers at player slots 63..68) each carry their own
    // invMgr; resolve the container, then walk its GUID array.
    auto *playerArr = *reinterpret_cast<const uint64_t *const *>(
        playerInvMgr + Offsets::OFF_INVMGR_GUID_ARRAY);
    if (playerArr == nullptr)
        return;
    for (int slot = Offsets::INVMGR_BANK_BAG_FIRST_SLOT;
         slot <= Offsets::INVMGR_BANK_BAG_LAST_SLOT; ++slot) {
        const uint8_t *bag = ResolveByGuid(Offsets::TYPEMASK_CONTAINER, playerArr[slot]);
        if (bag == nullptr)
            continue;
        auto *bagInvMgr =
            static_cast<const uint8_t *>(Item::Location::ContainerInventory(bag));
        if (bagInvMgr == nullptr)
            continue;
        int bagSlots = static_cast<int>(*reinterpret_cast<const uint32_t *>(bagInvMgr));
        if (bagSlots > kMaxContainerSlots)
            bagSlots = kMaxContainerSlots; // guard against a garbage count at login
        if (bagSlots > 0)
            AppendGuidArray(bagInvMgr, 0, bagSlots - 1, out, count);
    }
}

// --- split stacks -----------------------------------------------------------
//
// Splitting a stack gives the split-off half a NEW item GUID (the server
// clones the item), so the seen-set alone would flag it as a fresh
// acquisition. Splits are the client's own requests, so the source stack is
// recorded as CMSG_SPLIT_ITEM leaves; in the scan that shows the half, an
// unseen bag GUID with the source's item ID is that half, not a new item.
//
// A pending split retires when its source stack shrinks or disappears: the
// server sends the source's new count in the same update that places the
// half, so after that scan the half has either been claimed or the split
// went onto an existing stack (no new GUID). A refused split
// (SMSG_INVENTORY_CHANGE_FAILURE) changes nothing, so failures clear the list.

constexpr int kMaxPendingSplits = 8;

struct PendingSplit {
    uint64_t source; // GUID of the stack being split
    uint32_t itemID;
    uint32_t count; // the source's stack count when the request left
};
PendingSplit g_splits[kMaxPendingSplits];
int g_splitCount = 0;

uint32_t ItemID(const uint8_t *item) {
    auto *instance = Item::InstanceBlock(item);
    if (instance == nullptr)
        return 0;
    return *reinterpret_cast<const uint32_t *>(instance + Offsets::OFF_INSTANCE_BLOCK_ITEM_ID);
}

uint32_t StackCount(const uint8_t *item) {
    auto *fields = Item::ObjectFields(item);
    if (fields == nullptr)
        return 0;
    return *reinterpret_cast<const uint32_t *>(fields + Offsets::OFF_DESCRIPTOR_STACK_COUNT);
}

// The GUID in `slot` of an inventory manager's GUID array, bounded by its
// own slot count.
uint64_t GuidAt(const uint8_t *invMgr, int slot) {
    if (invMgr == nullptr)
        return 0;
    const uint32_t count =
        *reinterpret_cast<const uint32_t *>(invMgr + Offsets::OFF_INVMGR_SLOT_COUNT);
    auto *arr = *reinterpret_cast<const uint64_t *const *>(invMgr + Offsets::OFF_INVMGR_GUID_ARRAY);
    if (arr == nullptr || slot < 0 || static_cast<uint32_t>(slot) >= count)
        return 0;
    return arr[slot];
}

// The item at a packet-format (bag, slot) address: bag 0xFF is the player's
// own inventory, any other bag byte is the player slot holding the container.
const uint8_t *PacketSlotItem(uint8_t bag, uint8_t slot) {
    const uint8_t *invMgr = Unit::Identity::PlayerInventoryManager();
    if (invMgr != nullptr && bag != Offsets::INVENTORY_SLOT_BAG_0) {
        const uint8_t *container = ResolveByGuid(Offsets::TYPEMASK_CONTAINER, GuidAt(invMgr, bag));
        invMgr = container != nullptr
                     ? static_cast<const uint8_t *>(Item::Location::ContainerInventory(container))
                     : nullptr;
    }
    return ResolveByGuid(Offsets::TYPEMASK_ITEM, GuidAt(invMgr, slot));
}

void OnSend(uint32_t opcode, Net::CDataStore *packet) {
    if (opcode != Offsets::OP_CMSG_SPLIT_ITEM || g_splitCount >= kMaxPendingSplits)
        return;
    const uint8_t srcBag = Net::Read<uint8_t>(packet);
    const uint8_t srcSlot = Net::Read<uint8_t>(packet);
    const uint8_t *source = PacketSlotItem(srcBag, srcSlot);
    if (source == nullptr)
        return;
    g_splits[g_splitCount++] = {ItemGUID(source), ItemID(source), StackCount(source)};
}

void OnPacket(uint32_t opcode, Net::CDataStore *packet) {
    if (opcode == Offsets::SMSG_INVENTORY_CHANGE_FAILURE && Net::Read<uint8_t>(packet) != 0)
        g_splitCount = 0;
}

const Net::SendObserver::AutoSubscribe _sendSub{&OnSend};
const Net::PacketDispatch::AutoSubscribe _packetSub{&OnPacket};

// True when an unseen bag GUID is the half of a pending split, which it
// claims.
bool ClaimSplitHalf(uint64_t guid) {
    if (g_splitCount == 0)
        return false;
    const uint32_t itemID = ItemID(ResolveByGuid(Offsets::TYPEMASK_ITEM, guid));
    for (int i = 0; i < g_splitCount; ++i) {
        if (g_splits[i].itemID == itemID) {
            g_splits[i] = g_splits[--g_splitCount];
            return true;
        }
    }
    return false;
}

// Drops pending splits whose source has already shrunk or gone.
void RetireSplits() {
    for (int i = 0; i < g_splitCount;) {
        const uint8_t *source = ResolveByGuid(Offsets::TYPEMASK_ITEM, g_splits[i].source);
        if (source == nullptr || StackCount(source) < g_splits[i].count)
            g_splits[i] = g_splits[--g_splitCount];
        else
            ++i;
    }
}

void FireChanged() {
    Event::Custom::Fire(_r.Slot(), "");
}

// Login-baseline handler. Runs every frame but does no work once the
// baseline is taken — it only watches for (re)login and, after a short
// settle, takes the single authoritative baseline scan. Steady state is just
// a GUID read + `if (g_seeded) return`.
void OnTick() {
    const uint64_t guid = Unit::Identity::PlayerGuid();
    if (guid == 0)
        return; // not in world yet

    // Fresh login / character switch: drop all state and re-arm the settle.
    if (guid != g_playerGuid) {
        g_playerGuid = guid;
        g_newCount = 0;
        g_seenCount = 0;
        g_splitCount = 0;
        g_seeded = false;
        g_loginMs = NowMs();
    }

    if (g_seeded)
        return; // baseline established; the bag-change signal drives the rest
    if (NowMs() - g_loginMs < kSettleMs)
        return; // let the initial bag population finish

    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return; // not resolvable yet — retry next tick

    // Everything currently owned — bags AND worn equipment + bank — becomes the
    // baseline; nothing owned at login is "new". Including equipment means a
    // later unequip (paperdoll → bag) is recognized as a move, not a loot.
    uint64_t owned[kMaxItems];
    int ownedCount = ScanBags(player, owned);
    AppendNonBagOwned(owned, &ownedCount);
    g_seenCount = 0;
    SeenReconcile(owned, ownedCount);
    g_newCount = 0;
    g_seeded = true;
}

// Authoritative bag-change handler — fires only on frames where the engine
// actually changed a bag (via Bag::UpdateDelayed). Diffs the current bag set
// against the baseline, flags newly-acquired items, prunes departed ones.
void OnBagChanged() {
    if (!g_seeded)
        return; // pre-baseline changes are folded into the login baseline
    const uint64_t guid = Unit::Identity::PlayerGuid();
    if (guid == 0 || guid != g_playerGuid)
        return; // mid-relog; OnTick re-arms the baseline
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return;

    uint64_t currentBags[kMaxItems];
    const int bagCount = ScanBags(player, currentBags);

    bool changed = false;
    // Flag bag items we've never owned. An item merely dropped from a recent
    // scan (bag update mid-swap) is still in the seen-set (its miss streak is
    // below the threshold), so it is NOT re-flagged when it reappears — that
    // was the false-"new" bug. Checked before the reconcile so a genuinely new
    // item isn't inserted first. The half of a split stack is unseen too, but
    // it is not an acquisition.
    for (int i = 0; i < bagCount; ++i) {
        if (!SeenContains(currentBags[i]) && !ClaimSplitHalf(currentBags[i]))
            changed |= AddUnique(g_new, &g_newCount, currentBags[i]);
    }
    RetireSplits();

    // Reconcile the seen-set against everything owned now (bags + equipment +
    // bank): resets present items, ages absent ones, evicts long-gone ones.
    uint64_t owned[kMaxItems];
    int ownedCount = 0;
    for (int i = 0; i < bagCount; ++i)
        AddUnique(owned, &ownedCount, currentBags[i]);
    AppendNonBagOwned(owned, &ownedCount);
    SeenReconcile(owned, ownedCount);

    // Drop flags only for items that have genuinely left (fallen out of the
    // seen-set entirely), not ones merely missing from this one scan.
    for (int i = 0; i < g_newCount;) {
        if (!SeenContains(g_new[i])) {
            g_new[i] = g_new[--g_newCount];
            changed = true;
        } else {
            ++i;
        }
    }

    if (changed)
        FireChanged();
}

const Tick::WorldTick::AutoSubscribe _tick{&OnTick};
const Bag::UpdateDelayed::AutoSubscribe _bagChange{&OnBagChanged};

// --- C_NewItems Lua surface ----------------------------------------------

// Resolves a `(bagID, slotIndex)` Lua-arg pair to the item's instance GUID,
// or 0 if the args are missing/non-numeric or the slot is empty. Uses the
// Lua-context bag resolver — fine here, these run as Lua C functions.
uint64_t SlotGUID(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2))
        return 0;
    const int bagID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const int slotIndex = static_cast<int>(Game::Lua::ToNumber(L, 2));
    return ItemGUID(Item::Location::ResolveBag(L, bagID, slotIndex));
}

int __fastcall Script_IsNewItem(void *L) {
    const uint64_t guid = SlotGUID(L);
    Game::Lua::PushBool(L, guid != 0 && Contains(g_new, g_newCount, guid));
    return 1;
}

int __fastcall Script_RemoveNewItem(void *L) {
    const uint64_t guid = SlotGUID(L);
    if (guid != 0 && RemoveFrom(g_new, &g_newCount, guid))
        FireChanged();
    return 0;
}

int __fastcall Script_ClearAll(void *L) {
    (void)L;
    if (g_newCount > 0) {
        g_newCount = 0;
        FireChanged();
    }
    return 0;
}

const Game::Doc::Field kSlotArgs[] = {
    Game::Doc::Req("containerIndex", "number", "The bag, 0 to 4."),
    Game::Doc::Req("slotIndex", "number", "The slot in that bag."),
};
const Game::Doc::Field kIsNewItemRets[] = {
    Game::Doc::Req("isNew", "bool", "True when the item in the slot is flagged new."),
};
const Game::Doc::Function kIsNewItem{
    "Whether the item in a bag slot is new: looted, created, bought, rewarded, taken "
    "from mail or traded since login, and not yet cleared.",
    kSlotArgs, kIsNewItemRets};
const Game::Doc::Function kRemoveNewItem{
    "Clears the new flag of the item in a bag slot.", kSlotArgs, {}};
const Game::Doc::Function kClearAll{"Clears every new flag.", {}, {}};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_NewItems", "IsNewItem", &Script_IsNewItem, &kIsNewItem);
    Game::Lua::RegisterTableFunction("C_NewItems", "RemoveNewItem", &Script_RemoveNewItem,
                                     &kRemoveNewItem);
    Game::Lua::RegisterTableFunction("C_NewItems", "ClearAll", &Script_ClearAll, &kClearAll);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::NewItems
