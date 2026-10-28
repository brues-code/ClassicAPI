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

// `C_MerchantFrame.*` — six functions modeled on retail's namespace,
// all engine-direct (no Lua-roundtrip through `GetMerchantItemInfo`
// etc.). Storage layouts and the `MerchantSellItem` dispatcher are
// documented in Offsets.h.
//
// `GetBuybackItemID` and the global `GetBuybackItemLink` walk:
//   buyback slot → invMgr slot index → GUID → engine
//   `ResolveItemByGUID` → CGItem. The ID is the instance block's
//   +0x0C; the link is the engine's builder (Item::Link::FromCGItem).
//
// `GetNumJunkItems` / `SellAllJunkItems` walk player bags 0..4 via
// the existing `Item::Location::ResolveBag` helper and look up
// quality via the ItemStats cache. The sell loop calls
// `FUN_MERCHANT_SELL_ITEM` directly with the merchant NPC GUID +
// the item's GUID + count=0 (sell whole stack), exactly what the
// merchant branch of `Script_UseContainerItem` does internally —
// no Lua-side `UseContainerItem` round-trip, no risk of triggering
// the use-not-sell branch, immune to addon hooks on
// `UseContainerItem`.

#include "Game.h"
#include "Offsets.h"
#include "item/CGItem.h"
#include "item/Link.h"
#include "item/Location.h"
#include "item/Record.h"
#include "unit/Identity.h"
#include "tick/WorldTick.h"

#include <cstdint>
#include <vector>

namespace Merchant::Frame {

namespace {

// Read the 64-bit merchant NPC GUID. Both halves zero means no
// merchant window is open and the rest of the merchant globals are
// stale.
bool MerchantOpen(uint32_t &outGuidLo, uint32_t &outGuidHi) {
    outGuidLo = *reinterpret_cast<const uint32_t *>(
        Offsets::VAR_MERCHANT_NPC_GUID_LO);
    outGuidHi = *reinterpret_cast<const uint32_t *>(
        Offsets::VAR_MERCHANT_NPC_GUID_HI);
    return (outGuidLo | outGuidHi) != 0;
}

// CGItem +0x08 → instance block → +0x0C = itemID. Same chain
// EquipmentSet uses for set-membership lookups.
int ItemIDFromCGItem(const uint8_t *cgItem) {
    if (cgItem == nullptr)
        return 0;
    auto *instance = Item::InstanceBlock(cgItem);
    if (instance == nullptr)
        return 0;
    return static_cast<int>(*reinterpret_cast<const uint32_t *>(
        instance + Offsets::OFF_INSTANCE_BLOCK_ITEM_ID));
}

// Item GUID — same instance block, `+0x00` = uint64 GUID.
uint64_t ItemGUIDFromCGItem(const uint8_t *cgItem) {
    if (cgItem == nullptr)
        return 0;
    auto *instance = Item::InstanceBlock(cgItem);
    if (instance == nullptr)
        return 0;
    return *reinterpret_cast<const uint64_t *>(
        instance + Offsets::OFF_INSTANCE_BLOCK_GUID);
}

// Looks up `itemID` in the ItemStats cache and reads quality from
// `+0x1C`. Quality 0 = Poor (grey junk). Returns -1 on cache miss
// so callers can distinguish "not loaded" from "loaded poor-quality".
int ItemQuality(uint32_t itemID) {
    auto *record = Item::PeekRecord(itemID);
    if (record == nullptr)
        return -1;
    return static_cast<int>(*reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_QUALITY));
}

// Player invMgr lives inline at player + 0x1D38. First uint32 is the
// max slot count; +0x04 is the GUID array pointer.
// Walk a single bag, invoking `cb(cgItem)` for each non-empty slot.
// `cb` returns true to continue, false to stop iteration. Stomps
// the Lua stack — caller must own it (`ResolveBag` does the stomping).
template <typename Cb>
void ForEachItemInBag(void *L, int bagID, Cb &&cb) {
    const int slotCount = Item::Location::GetBagSlotCount(bagID);
    for (int slot = 1; slot <= slotCount; ++slot) {
        const uint8_t *item = Item::Location::ResolveBag(L, bagID, slot);
        if (item == nullptr)
            continue;
        if (!cb(bagID, slot, item))
            return;
    }
}

// ---------------------------------------------------------------------
// Buyback slots
// ---------------------------------------------------------------------

// The item in the 0-based buyback `slot`, or nullptr. Mirrors 1.12's
// Script_GetBuybackItemInfo (0x004FB310) and 4.3.4's
// Script_GetBuybackItemLink: no vendor-GUID check, so the cached list
// still answers after the window closes (the close handler clears the
// GUID but not VAR_BUYBACK_COUNT).
const uint8_t *BuybackItem(int slot) {
    const int count = static_cast<int>(*reinterpret_cast<const uint32_t *>(
        Offsets::VAR_BUYBACK_COUNT));
    if (slot < 0 || slot >= count)
        return nullptr;

    // Buyback slot → linear invMgr slot index → GUID.
    const uint32_t invSlot = *reinterpret_cast<const uint32_t *>(
        Offsets::VAR_BUYBACK_SLOTS + slot * 4);
    if (invSlot == 0)
        return nullptr;

    auto *invMgr = Unit::Identity::PlayerInventoryManager();
    if (invMgr == nullptr)
        return nullptr;
    // First u32 of the invMgr struct is the max slot count.
    const uint32_t maxSlots = *reinterpret_cast<const uint32_t *>(invMgr);
    if (invSlot >= maxSlots)
        return nullptr;
    auto *guidArray = *reinterpret_cast<const uint64_t *const *>(
        invMgr + Offsets::OFF_INVMGR_GUID_ARRAY);
    if (guidArray == nullptr)
        return nullptr;
    const uint64_t guid = guidArray[invSlot];
    if (guid == 0)
        return nullptr;

    return Item::Location::ResolveByGUID(guid);
}

int __fastcall Script_GetBuybackItemID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L,
            "Usage: C_MerchantFrame.GetBuybackItemID(slot)");
        return 0;
    }
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    const int itemID = ItemIDFromCGItem(BuybackItem(slot));
    if (itemID == 0)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    return 1;
}

int __fastcall Script_GetBuybackItemLink(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetBuybackItemLink(slot)");
        return 0;
    }
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    const char *link = Item::Link::FromCGItem(BuybackItem(slot));
    if (link == nullptr)
        return 0;
    Game::Lua::PushString(L, link);
    return 1;
}

// ---------------------------------------------------------------------
// GetItemInfo — returns a table mirroring retail's MerchantItemInfo
// shape, populated from what vanilla actually has on hand.
// ---------------------------------------------------------------------

int __fastcall Script_GetItemInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_MerchantFrame.GetItemInfo(slot)");
        return 0;
    }
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    if (slot < 0)
        return 0;

    uint32_t merchantLo, merchantHi;
    if (!MerchantOpen(merchantLo, merchantHi))
        return 0;

    const int count = static_cast<int>(*reinterpret_cast<const uint32_t *>(
        Offsets::VAR_MERCHANT_COUNT));
    if (slot >= count)
        return 0;

    auto *entry = reinterpret_cast<const uint8_t *>(
        Offsets::VAR_MERCHANT_ITEMS + slot * Offsets::MERCHANT_STRIDE);
    const uint32_t itemID = *reinterpret_cast<const uint32_t *>(
        entry + Offsets::OFF_MERCHANT_ITEM_ID);
    if (itemID == 0)
        return 0;
    const uint32_t price = *reinterpret_cast<const uint32_t *>(
        entry + Offsets::OFF_MERCHANT_PRICE);
    const uint32_t stackCount = *reinterpret_cast<const uint32_t *>(
        entry + Offsets::OFF_MERCHANT_STACK_COUNT);
    const int32_t numAvailable = *reinterpret_cast<const int32_t *>(
        entry + Offsets::OFF_MERCHANT_NUM_AVAILABLE);

    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "itemID", static_cast<double>(itemID));
    Game::Lua::SetFieldNumber(L, "price", static_cast<double>(price));
    Game::Lua::SetFieldNumber(L, "stackCount", static_cast<double>(stackCount));
    Game::Lua::SetFieldNumber(L, "numAvailable", static_cast<double>(numAvailable));
    Game::Lua::SetFieldBool(L, "isPurchasable", true);
    Game::Lua::SetFieldBool(L, "isThrottled", false);
    Game::Lua::SetFieldBool(L, "hasExtendedCost", false);
    return 1;
}

// ---------------------------------------------------------------------
// GetNumJunkItems
// ---------------------------------------------------------------------

int __fastcall Script_GetNumJunkItems(void *L) {
    // Retail returns 0 unless a merchant frame is currently open —
    // the count is a "what would SellAllJunkItems do right now"
    // signal, not a passive inventory query.
    uint32_t merchantLo, merchantHi;
    if (!MerchantOpen(merchantLo, merchantHi)) {
        Game::Lua::PushNumber(L, 0);
        return 1;
    }

    int junkCount = 0;
    for (int bag = 0; bag <= 4; ++bag) {
        ForEachItemInBag(L, bag, [&](int, int, const uint8_t *item) {
            const int itemID = ItemIDFromCGItem(item);
            if (itemID > 0 && ItemQuality(static_cast<uint32_t>(itemID)) == 0)
                ++junkCount;
            return true;
        });
    }
    Game::Lua::PushNumber(L, static_cast<double>(junkCount));
    return 1;
}

// ---------------------------------------------------------------------
// IsMerchantItemRefundable — vanilla has no refund mechanic.
// ---------------------------------------------------------------------

int __fastcall Script_IsMerchantItemRefundable(void *L) {
    Game::Lua::PushBoolean(L, 0);
    return 1;
}

// ---------------------------------------------------------------------
// IsSellAllJunkEnabled — no client-side gate in vanilla; always on.
// ---------------------------------------------------------------------

int __fastcall Script_IsSellAllJunkEnabled(void *L) {
    Game::Lua::PushBoolean(L, 1);
    return 1;
}

// ---------------------------------------------------------------------
// SellAllJunkItems — direct call into the engine's CMSG_SELL_ITEM
// dispatcher per junk item. No `UseContainerItem` round-trip.
// ---------------------------------------------------------------------

// Irregular calling convention: `count` in ECX, then **four stack
// dwords** (merchantLo, merchantHi, itemLo, itemHi). The callee
// reads them from `[ebp+8]..[ebp+0x14]` and ignores EDX entirely —
// see the call site at `0x004FA371` inside `Script_UseContainerItem`,
// which does `push edi; push esi; push ecx; push eax; xor ecx,
// ecx; call`. MSVC `__fastcall` puts arg1/arg2 in ECX/EDX, so we
// declare a dummy EDX arg to force the remaining four onto the
// stack in the right slots.
using MerchantSellItem_t = void(__fastcall *)(unsigned int count,
                                              unsigned int /*edx, unused*/,
                                              uint32_t merchantLo,
                                              uint32_t merchantHi,
                                              uint32_t itemLo,
                                              uint32_t itemHi);

// Pending-sell queue. Selling 10+ items in one frame loses one or
// more to a flood throttle (verified empirically — burst-sending 10
// CMSG_SELL_ITEM packets via FUN_MERCHANT_SELL_ITEM consistently
// drops the 2nd-to-last; running SellAllJunkItems again sells the
// straggler cleanly). Spreading them one-per-frame via the shared
// WorldTick subscriber sidesteps the throttle and matches the
// natural cadence of click-by-click selling.
//
// We track the merchant GUID at enqueue time so a mid-drain change
// (player closes the vendor, opens a different one, etc.) cancels
// the rest of the queue rather than mis-routing sells to the new
// merchant.
struct PendingSells {
    std::vector<uint64_t> queue;
    uint32_t merchantLo = 0;
    uint32_t merchantHi = 0;
};

PendingSells g_pending;

void TickDrainPending() {
    if (g_pending.queue.empty())
        return;

    uint32_t curMerchantLo, curMerchantHi;
    if (!MerchantOpen(curMerchantLo, curMerchantHi) ||
        curMerchantLo != g_pending.merchantLo ||
        curMerchantHi != g_pending.merchantHi) {
        // Merchant changed/closed — abandon the rest.
        g_pending.queue.clear();
        return;
    }

    const uint64_t guid = g_pending.queue.back();
    g_pending.queue.pop_back();

    auto sell = reinterpret_cast<MerchantSellItem_t>(
        Offsets::FUN_MERCHANT_SELL_ITEM);
    sell(/*count=*/0, /*edx, unused=*/0,
         g_pending.merchantLo, g_pending.merchantHi,
         static_cast<uint32_t>(guid),
         static_cast<uint32_t>(guid >> 32));
}

static const Tick::WorldTick::AutoSubscribe _drain{&TickDrainPending};

int __fastcall Script_SellAllJunkItems(void *L) {
    uint32_t merchantLo, merchantHi;
    if (!MerchantOpen(merchantLo, merchantHi))
        return 0;

    // If a previous call's queue is still draining (extremely rare —
    // the tick fires every frame so the queue empties in N frames
    // for N items), abandon it. The new call is the authoritative
    // intent.
    g_pending.queue.clear();
    g_pending.merchantLo = merchantLo;
    g_pending.merchantHi = merchantHi;

    for (int bag = 0; bag <= 4; ++bag) {
        ForEachItemInBag(L, bag, [&](int, int, const uint8_t *item) {
            const int itemID = ItemIDFromCGItem(item);
            if (itemID <= 0 || ItemQuality(static_cast<uint32_t>(itemID)) != 0)
                return true;
            const uint64_t guid = ItemGUIDFromCGItem(item);
            if (guid != 0)
                g_pending.queue.push_back(guid);
            return true;
        });
    }
    // The WorldTick subscriber drains one per frame.
    return 0;
}

const Game::Doc::Field kSlotArgs[] = {
    Game::Doc::Req("slot", "number", "The buyback slot, 1 to 12."),
};
const Game::Doc::Field kLinkRets[] = {
    Game::Doc::Opt("itemLink", "string", nullptr,
                   "The item link, or nil if the slot is empty."),
};
const Game::Doc::Function kGetBuybackItemLink{
    "Returns the item link of an item in the buyback list of the vendor.",
    kSlotArgs, kLinkRets, "MerchantGlobals"};

void Register() {
    Game::Lua::RegisterGlobalFunction("GetBuybackItemLink", &Script_GetBuybackItemLink,
                                      &kGetBuybackItemLink);
    Game::Lua::RegisterTableFunction("C_MerchantFrame", "GetBuybackItemID",
                                     &Script_GetBuybackItemID);
    Game::Lua::RegisterTableFunction("C_MerchantFrame", "GetItemInfo",
                                     &Script_GetItemInfo);
    Game::Lua::RegisterTableFunction("C_MerchantFrame", "GetNumJunkItems",
                                     &Script_GetNumJunkItems);
    Game::Lua::RegisterTableFunction("C_MerchantFrame",
                                     "IsMerchantItemRefundable",
                                     &Script_IsMerchantItemRefundable);
    Game::Lua::RegisterTableFunction("C_MerchantFrame", "IsSellAllJunkEnabled",
                                     &Script_IsSellAllJunkEnabled);
    Game::Lua::RegisterTableFunction("C_MerchantFrame", "SellAllJunkItems",
                                     &Script_SellAllJunkItems);
}

} // namespace

static const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace Merchant::Frame
