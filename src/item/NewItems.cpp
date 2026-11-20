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

// `C_NewItems` — which bag items are new, as the 3.80.2 Classic client keeps
// it. The store is one set of item GUIDs (3.80.2's
// `unordered_map<WOWGUID, bool>` at 0x1444DEF60) that lives as long as the
// process: nothing clears it on login, logout, a character switch or a
// loading screen, and nothing takes an item out when it leaves the bags.
// Only `RemoveNewItem` and `ClearAll` remove entries.
//
// What adds an item is the item-push packet. 3.80.2's handler
// (FUN_1416961A0) adds the pushed item's GUID for a push to the active
// player, with no test of looted / created / bag, while the set holds fewer
// than 50 entries, and fires `BAG_NEW_ITEMS_UPDATED` (no payload) on every
// add, even for an item that is already new. Its other feed, a server flag on
// the item object (0x200000), and the message it sends the server on a
// removal have no 1.12 counterpart.
//
// 1.12's push is `SMSG_ITEM_PUSH_RESULT`: the player GUID, three u32 (the
// server's received / created / show-in-chat), a u8 bag, a u32 slot, the item
// ID, its suffix factor, its random property and the count. It names no item
// GUID, and the server (tortoise-wow `Player::SendNewItem`) sends it at once
// while the item's object update leaves with the world tick's update flush,
// so the item is not in place yet when the push arrives. Each push therefore
// waits as a pending push and is resolved on a later bag change:
//   - a push to a slot names the slot the item went into; the item there with
//     the pushed item ID is the new one;
//   - a push onto existing stacks has slot 0xFFFFFFFF; the stack of that item
//     ID in that bag whose count went up is the one 3.80.2 flags (its server
//     names the stack the items landed on).
// Mail and trade send no push, so their items are not new.

#include "Game.h"
#include "Offsets.h"
#include "bag/UpdateDelayed.h"
#include "event/Custom.h"
#include "item/CGItem.h"
#include "item/ID.h"
#include "item/Location.h"
#include "net/PacketDispatch.h"
#include "object/Resolve.h"
#include "unit/Identity.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace Item::NewItems {

namespace {

constexpr const char *kEvtNewItems = "BAG_NEW_ITEMS_UPDATED";
const Game::Doc::Event kNewItemsDoc{"NewItems", "Fires each time a bag item becomes new.", {}};
const Event::Custom::AutoReserve _r{kEvtNewItems, &kNewItemsDoc};

// 3.80.2's push handler adds only while the set holds fewer than this many
// entries; items that are gone still count.
constexpr size_t kMaxNew = 50;

// The push's slot when the items went onto existing stacks.
constexpr uint32_t kStackedSlot = 0xFFFFFFFF;

std::unordered_set<uint64_t> g_new;

void Add(uint64_t guid) {
    if (guid == 0 || g_new.size() >= kMaxNew)
        return;
    g_new.insert(guid);
    Event::Custom::Fire(_r.Slot(), "");
}

// --- packet-format inventory addresses ---------------------------------------

const uint8_t *ResolveByGuid(int type, uint64_t guid) {
    if (guid == 0)
        return nullptr;
    return static_cast<const uint8_t *>(Object::ByGuid(type, guid, "ItemMgr", 0x172));
}

uint32_t SlotCount(const uint8_t *invMgr) {
    return Game::Read<uint32_t>(invMgr, Offsets::OFF_INVMGR_SLOT_COUNT);
}

// The GUID in `slot` of an inventory manager's GUID array, bounded by its
// own slot count.
uint64_t GuidAt(const uint8_t *invMgr, uint32_t slot) {
    if (invMgr == nullptr || slot >= SlotCount(invMgr))
        return 0;
    auto *arr = Game::Read<const uint64_t *>(invMgr, Offsets::OFF_INVMGR_GUID_ARRAY);
    return arr != nullptr ? arr[slot] : 0;
}

// The inventory a packet's bag byte names: 0xFF is the player's own list, any
// other byte is the player slot that holds the container.
const uint8_t *PacketInventory(uint8_t bag) {
    const uint8_t *invMgr = Unit::Identity::PlayerInventoryManager();
    if (invMgr == nullptr || bag == Offsets::INVENTORY_SLOT_BAG_0)
        return invMgr;
    const uint8_t *container = ResolveByGuid(Offsets::TYPEMASK_CONTAINER, GuidAt(invMgr, bag));
    return static_cast<const uint8_t *>(Item::Location::ContainerInventory(container));
}

const uint8_t *SlotItem(const uint8_t *invMgr, uint32_t slot) {
    return ResolveByGuid(Offsets::TYPEMASK_ITEM, GuidAt(invMgr, slot));
}

uint32_t ItemIDOf(const uint8_t *item) {
    return static_cast<uint32_t>(Item::ID::FromCGItem(item));
}

// --- pending pushes ----------------------------------------------------------

struct Stack {
    uint64_t guid;
    uint32_t count;
};

struct Pending {
    uint64_t player; // the push was for this character
    uint8_t bag;
    uint32_t slot;   // kStackedSlot for a push onto existing stacks
    uint32_t itemID;
    std::vector<Stack> stacks; // a stacked push: the bag's stacks of the item when it arrived
};

std::vector<Pending> g_pending;

// True when the push is resolved: its item is flagged, or (a push to a slot)
// another item took the slot. A push whose item has not arrived waits.
bool Resolve(const Pending &p) {
    const uint8_t *invMgr = PacketInventory(p.bag);
    if (invMgr == nullptr)
        return false;
    if (p.slot != kStackedSlot) {
        const uint8_t *item = SlotItem(invMgr, p.slot);
        if (item == nullptr)
            return false;
        if (ItemIDOf(item) == p.itemID)
            Add(::Object::Guid(item));
        return true;
    }
    for (const Stack &s : p.stacks) {
        const uint8_t *item = ResolveByGuid(Offsets::TYPEMASK_ITEM, s.guid);
        if (item != nullptr && Item::StackCount(item) > s.count) {
            Add(s.guid);
            return true;
        }
    }
    return false;
}

void ResolvePending() {
    const uint64_t player = Unit::Identity::PlayerGuid();
    for (size_t i = 0; i < g_pending.size();) {
        if (g_pending[i].player != player || Resolve(g_pending[i]))
            g_pending.erase(g_pending.begin() + static_cast<std::ptrdiff_t>(i));
        else
            ++i;
    }
}

void OnPacket(uint32_t opcode, Net::CDataStore *packet) {
    if (opcode != Offsets::SMSG_ITEM_PUSH_RESULT)
        return;
    Pending p;
    p.player = Net::Read<uint64_t>(packet);
    Net::Read<uint32_t>(packet); // received
    Net::Read<uint32_t>(packet); // created
    Net::Read<uint32_t>(packet); // show in chat
    p.bag = Net::Read<uint8_t>(packet);
    p.slot = Net::Read<uint32_t>(packet);
    p.itemID = Net::Read<uint32_t>(packet);
    if (Net::Overrun(packet) || p.player == 0 || p.player != Unit::Identity::PlayerGuid())
        return; // a group member's loot, sent to the whole group
    if (p.slot == kStackedSlot) {
        const uint8_t *invMgr = PacketInventory(p.bag);
        if (invMgr == nullptr)
            return;
        const uint32_t slots = SlotCount(invMgr);
        for (uint32_t slot = 0; slot < slots; ++slot) {
            const uint8_t *item = SlotItem(invMgr, slot);
            if (item != nullptr && ItemIDOf(item) == p.itemID)
                p.stacks.push_back({::Object::Guid(item), Item::StackCount(item)});
        }
        if (p.stacks.empty())
            return;
    }
    // A later push to the same place replaces one still waiting.
    for (Pending &q : g_pending) {
        if (q.player == p.player && q.bag == p.bag && q.slot == p.slot && q.itemID == p.itemID) {
            q = std::move(p);
            return;
        }
    }
    g_pending.push_back(std::move(p));
}

const Net::PacketDispatch::AutoSubscribe _packetSub{&OnPacket};
const Bag::UpdateDelayed::AutoSubscribe _bagChange{&ResolvePending};

// --- C_NewItems Lua surface ----------------------------------------------

// The item at `(containerIndex, slotIndex)`, read as 3.80.2's argument
// readers read them (FUN_140BEC680 / FUN_140A6BF10): both numbers, the bag in
// int range and the slot from 0 to 2^32 - 1, or the usage error. A slot past
// the bag's end, slot 0 included, is no item.
bool SlotItemArg(void *L, const char *usage, const uint8_t **item) {
    *item = nullptr;
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, usage);
        return false;
    }
    const double bag = Game::Lua::ToNumber(L, 1);
    const double slot = Game::Lua::ToNumber(L, 2);
    if (!(bag >= -2147483648.0 && bag <= 2147483647.0) || !(slot >= 0.0 && slot <= 4294967295.0)) {
        Game::Lua::Error(L, usage);
        return false;
    }
    // 3.80.2 truncates `slot - 1` to its 0-based slot.
    const double slot0 = slot - 1.0;
    if (slot0 < 2147483647.0)
        *item = Item::Location::ResolveBag(static_cast<int>(bag), static_cast<int>(slot0) + 1);
    return true;
}

int __fastcall Script_IsNewItem(void *L) {
    const uint8_t *item;
    if (!SlotItemArg(L, "Usage: local isNew = C_NewItems.IsNewItem(containerIndex, slotIndex)", &item))
        return 0;
    Game::Lua::PushBool(L, item != nullptr && g_new.count(::Object::Guid(item)) != 0);
    return 1;
}

int __fastcall Script_RemoveNewItem(void *L) {
    const uint8_t *item;
    if (SlotItemArg(L, "Usage: C_NewItems.RemoveNewItem(containerIndex, slotIndex)", &item) &&
        item != nullptr)
        g_new.erase(::Object::Guid(item));
    return 0;
}

int __fastcall Script_ClearAll(void *L) {
    (void)L;
    g_new.clear();
    return 0;
}

const Game::Doc::Field kSlotArgs[] = {
    Game::Doc::Req("containerIndex", "number",
                   "The bag: 0 is the backpack, 1 to 4 are the bags, -1 is the bank, 5 to 10 are the "
                   "bank bags and -2 is the keyring."),
    Game::Doc::Req("slotIndex", "number", "The slot in that bag."),
};
const Game::Doc::Field kIsNewItemRets[] = {
    Game::Doc::Req("isNew", "bool", "True when the item in the slot is new."),
};
const Game::Doc::Function kIsNewItem{
    "Returns whether the item in a bag slot is new: the server gave it to you (loot, a craft, a "
    "purchase or a reward), and its new flag is not cleared.",
    kSlotArgs, kIsNewItemRets};
const Game::Doc::Function kRemoveNewItem{"Clears the new flag of the item in a bag slot.", kSlotArgs,
                                         {}};
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
