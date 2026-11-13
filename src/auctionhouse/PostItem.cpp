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

// `C_AuctionHouse.PostItem(itemLocation, duration, quantity, numStacks, bid,
// buyout)` — post `numStacks` auctions of `quantity` each from a single
// source stack, in one call. A ClassicAPI extension modelled on the retail
// multi-sell flow; vanilla's `StartAuction` posts exactly one item object.
//
// Why this is a tick-driven state machine (not a synchronous loop):
// vanilla's `CMSG_AUCTION_SELL_ITEM` (opcode 0x256) is
// `{ auctioneerGuid, itemGuid, bid, buyout, runTime }` — NO count field
// (WotLK added the server-side count; 1.12 has none, verified from
// `Script_StartAuction`). So the server posts the *entire* item object a
// GUID points at. To post a partial `quantity` out of a larger source, the
// client must first materialize that quantity as its own server-committed
// item object — i.e. split it into a bag slot (`CMSG_SPLIT_ITEM`) — and both
// the split and the resulting post are server-authoritative and async (the
// new stack's GUID doesn't exist client-side until `SMSG_UPDATE_OBJECT`
// lands). We drive the split→post→split→post sequence off `WorldTick`,
// keying each transition on observed bag state.
//
// Free-slot cost: a partial post needs at most ONE reused general-bag slot
// (split `quantity` off → post that stack → the post empties the slot →
// reuse it for the next split). The final stack (once the source is whittled
// to exactly `quantity`) posts the source object directly, no split. Posting
// a whole existing stack (`quantity` == the source count) needs zero free
// slots.
//
// Posting reuses the engine's `Script_StartAuction` verbatim (write the
// target stack's GUID into the sell-slot globals, push bid/buyout/runTime,
// call it) so all of its validation runs per stack — damaged-item reject,
// buyout normalization, runTime table check, auctioneer/"AH open" check.
//
// Events (retail multi-sell names, fired via Event::Custom):
//   AUCTION_MULTISELL_START(total)        — job accepted, posting begins
//   AUCTION_MULTISELL_UPDATE(done, total) — after each stack lands
//   AUCTION_MULTISELL_FAILURE             — aborted (source changed, no slot,
//                                           AH closed, or a step timed out)
//
// Limitations (v1): source is the single stack at `itemLocation` (not
// aggregated across bags); temp splits use general-purpose bags only, so
// family-restricted items (arrows/shards in specialty bags) aren't
// supported; one job at a time.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "item/CGItem.h"
#include "item/ID.h"
#include "item/Location.h"
#include "item/Record.h"
#include "item/Swap.h"
#include "tick/WorldTick.h"

#include <cstdint>

namespace AuctionHouse::PostItem {

namespace {

const Game::Doc::Field kStartPayload[] = {
    Game::Doc::Req("numRepetitions", "number", "The auctions the job will post."),
};
const Game::Doc::Event kStartDoc{
    "AuctionHouse", "Fires when C_AuctionHouse.PostItem accepts a job and starts to post.",
    kStartPayload};
const Game::Doc::Field kUpdatePayload[] = {
    Game::Doc::Req("createdCount", "number", "The auctions posted so far."),
    Game::Doc::Req("totalToCreate", "number"),
};
const Game::Doc::Event kUpdateDoc{
    "AuctionHouse", "Fires after each auction that C_AuctionHouse.PostItem posts.",
    kUpdatePayload};
const Game::Doc::Event kFailureDoc{
    "AuctionHouse", "Fires when a C_AuctionHouse.PostItem job stops before it posts all auctions.",
    {}};

const Event::Custom::AutoReserve _evtStart{"AUCTION_MULTISELL_START", &kStartDoc};
const Event::Custom::AutoReserve _evtUpdate{"AUCTION_MULTISELL_UPDATE", &kUpdateDoc};
const Event::Custom::AutoReserve _evtFailure{"AUCTION_MULTISELL_FAILURE", &kFailureDoc};

// Generous per-step abort bound. A split or a post is one server round-trip
// (~tens of frames); 600 frames (~10s at 60fps) means "something's wrong"
// (disconnect, server refusal we can't see) rather than normal latency.
constexpr int kMaxWaitTicks = 600;

enum class Phase { Idle, SplitWait, PostWait };

struct Job {
    bool active;
    int srcBag, srcSlot; // source stack (shrinks in place as we split off it)
    int itemID;          // guards against the source slot changing under us
    int quantity;
    int totalStacks;
    int postedStacks;
    int bid, buyout, runTime;
    Phase phase;
    int workBag, workSlot; // slot currently being split-into / posted-from
    uint64_t postGuid;     // GUID we posted — detect it leaving bags
    int waitTicks;
};
Job g_job{};

// --- small CGItem / cache readers ---------------------------------------

int CGItemCount(const uint8_t *item) {
    auto *desc = Item::ObjectFields(item);
    if (desc == nullptr)
        return 0;
    return Game::Read<int>(desc, Offsets::OFF_DESCRIPTOR_STACK_COUNT);
}

uint64_t CGItemGuid(const uint8_t *item) {
    auto *inst = Item::InstanceBlock(item);
    if (inst == nullptr)
        return 0;
    return Game::Read<uint64_t>(inst, Offsets::OFF_INSTANCE_BLOCK_GUID);
}

struct SlotInfo {
    const uint8_t *item; // null = empty slot
    int itemID;
    int count;
    uint64_t guid;
};

SlotInfo ReadSlot(void *L, int bag, int slot) {
    SlotInfo r{nullptr, 0, 0, 0};
    const uint8_t *item = Item::Location::ResolveBag(L, bag, slot);
    if (item == nullptr)
        return r;
    r.item = item;
    r.itemID = Item::ID::FromCGItem(item);
    r.count = CGItemCount(item);
    r.guid = CGItemGuid(item);
    return r;
}

// --- auction-house globals ----------------------------------------------

uint64_t AuctioneerGuid() {
    const uint32_t lo = Game::Read<uint32_t>(
        Offsets::VAR_AUCTION_AUCTIONEER_GUID_LO);
    const uint32_t hi = Game::Read<uint32_t>(
        Offsets::VAR_AUCTION_AUCTIONEER_GUID_HI);
    return (static_cast<uint64_t>(hi) << 32) | lo;
}
bool AHOpen() { return AuctioneerGuid() != 0; }

void SetSellGuid(uint64_t guid) {
    Game::Ref<uint32_t>(Offsets::VAR_AUCTION_SELL_GUID_LO) =
        static_cast<uint32_t>(guid);
    Game::Ref<uint32_t>(Offsets::VAR_AUCTION_SELL_GUID_HI) =
        static_cast<uint32_t>(guid >> 32);
}

// Point the sell slot at `guid` and fire the engine's own StartAuction for
// this bid/buyout/runTime. StartAuction reads its args from L[1..3]; we own
// the stack at frame-tail, so set top to 0, push the three, call, reset.
void PostStack(void *L, uint64_t guid) {
    SetSellGuid(guid);
    Game::Lua::SetTop(L, 0);
    Game::Lua::PushNumber(L, static_cast<double>(g_job.bid));
    Game::Lua::PushNumber(L, static_cast<double>(g_job.buyout));
    Game::Lua::PushNumber(L, static_cast<double>(g_job.runTime));
    auto fn = reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_START_AUCTION);
    fn(L);
    Game::Lua::SetTop(L, 0);
}

// --- free-slot discovery (general-purpose bags only) --------------------

bool BagIsGeneral(int bag) {
    if (bag == 0)
        return true; // backpack is always general
    if (bag < 1 || bag > 4)
        return false;
    const uint8_t *bagItem =
        Item::Location::ResolveEquipmentSlot(Offsets::INVSLOT_BAG1 + bag - 1);
    if (bagItem == nullptr)
        return false; // no bag equipped there
    const int id = Item::ID::FromCGItem(bagItem);
    if (id == 0)
        return false;
    const uint8_t *rec = Item::PeekRecord(static_cast<uint32_t>(id));
    if (rec == nullptr)
        return false;
    return Game::Read<uint32_t>(
               rec, Offsets::OFF_ITEMSTATS_BAG_FAMILY) == 0;
}

bool FindFreeGeneralSlot(void *L, int *outBag, int *outSlot) {
    for (int bag = 0; bag <= 4; ++bag) {
        if (!BagIsGeneral(bag))
            continue;
        const int n = Item::Location::GetBagSlotCount(bag);
        for (int slot = 1; slot <= n; ++slot) {
            if (Item::Location::ResolveBag(L, bag, slot) == nullptr) {
                *outBag = bag;
                *outSlot = slot;
                return true;
            }
        }
    }
    return false;
}

// --- state machine ------------------------------------------------------

void Finish(bool success) {
    g_job.active = false;
    if (!success)
        Event::Custom::Fire(_evtFailure.Slot(), "");
    // success needs no event: the last PostWait already fired
    // AUCTION_MULTISELL_UPDATE(total, total).
}

void Tick() {
    if (!g_job.active)
        return;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    const int savedTop = Game::Lua::GetTop(L);

    if (!AHOpen()) { // window closed / walked away mid-job
        Finish(false);
        Game::Lua::SetTop(L, savedTop);
        return;
    }

    switch (g_job.phase) {
    case Phase::Idle: {
        if (g_job.postedStacks >= g_job.totalStacks) {
            Finish(true);
            break;
        }
        const SlotInfo s = ReadSlot(L, g_job.srcBag, g_job.srcSlot);
        if (s.item == nullptr || s.itemID != g_job.itemID || s.count < g_job.quantity) {
            Finish(false); // source moved / consumed / insufficient
            break;
        }
        if (s.count == g_job.quantity) {
            // Final exact stack — post the source object directly, no split.
            g_job.workBag = g_job.srcBag;
            g_job.workSlot = g_job.srcSlot;
            g_job.postGuid = s.guid;
            PostStack(L, s.guid);
            g_job.phase = Phase::PostWait;
            g_job.waitTicks = 0;
        } else {
            int fb = 0, fs = 0;
            if (!FindFreeGeneralSlot(L, &fb, &fs)) {
                Finish(false); // no room to materialize the stack
                break;
            }
            if (!Item::Swap::MoveCount(L, g_job.srcBag, g_job.srcSlot, fb, fs,
                                       g_job.quantity)) {
                Finish(false);
                break;
            }
            g_job.workBag = fb;
            g_job.workSlot = fs;
            g_job.phase = Phase::SplitWait;
            g_job.waitTicks = 0;
        }
        break;
    }
    case Phase::SplitWait: {
        const SlotInfo w = ReadSlot(L, g_job.workBag, g_job.workSlot);
        if (w.item != nullptr && w.itemID == g_job.itemID && w.count == g_job.quantity) {
            g_job.postGuid = w.guid;
            PostStack(L, w.guid);
            g_job.phase = Phase::PostWait;
            g_job.waitTicks = 0;
        } else if (++g_job.waitTicks > kMaxWaitTicks) {
            Finish(false);
        }
        break;
    }
    case Phase::PostWait: {
        const SlotInfo w = ReadSlot(L, g_job.workBag, g_job.workSlot);
        // Post confirmed once the server pulls the item out of the slot.
        if (w.item == nullptr || w.guid != g_job.postGuid) {
            ++g_job.postedStacks;
            Event::Custom::Fire(_evtUpdate.Slot(), "%d%d",
                                g_job.postedStacks, g_job.totalStacks);
            g_job.phase = Phase::Idle;
            g_job.waitTicks = 0;
        } else if (++g_job.waitTicks > kMaxWaitTicks) {
            Finish(false);
        }
        break;
    }
    }

    Game::Lua::SetTop(L, savedTop);
}

const Tick::WorldTick::AutoSubscribe _tick{&Tick};

// --- Lua entry ----------------------------------------------------------

// Reads `loc.field` as an int. False if missing/non-numeric. Leaves the
// stack as found. (Same shape as Item::Location's file-static helper.)
bool ReadIntField(void *L, int locIdx, const char *field, int *out) {
    Game::Lua::PushString(L, field);
    Game::Lua::GetTable(L, locIdx);
    const bool ok = Game::Lua::IsNumber(L, -1);
    if (ok)
        *out = static_cast<int>(Game::Lua::ToNumber(L, -1));
    Game::Lua::SetTop(L, -2);
    return ok;
}

// Map the modern `duration` arg to a valid engine runTime (minutes).
// Accepts the 1/2/3 index form and the raw {120,480,1440} value form.
// Returns 0 for anything else.
int MapDuration(int d) {
    auto *table = reinterpret_cast<const uint32_t *>(Offsets::VAR_AUCTION_DURATION_TABLE);
    if (d >= 1 && d <= Offsets::AUCTION_DURATION_TABLE_COUNT)
        return static_cast<int>(table[d - 1]);
    for (int i = 0; i < Offsets::AUCTION_DURATION_TABLE_COUNT; ++i)
        if (static_cast<int>(table[i]) == d)
            return d;
    return 0;
}

// Push (false, reason) and return 2 — the immediate-rejection shape.
int Reject(void *L, const char *reason) {
    Game::Lua::PushBool(L, false);
    Game::Lua::PushString(L, reason);
    return 2;
}

constexpr int kLuaTable = 5; // LUA_TTABLE (no named constant in Game::Lua)

int __fastcall Script_C_AuctionHouse_PostItem(void *L) {
    if (Game::Lua::Type(L, 1) != kLuaTable) {
        Game::Lua::Error(L,
            "Usage: C_AuctionHouse.PostItem(itemLocation, duration, quantity, numStacks, bid, buyout)");
        return 0;
    }
    if (!Game::Lua::IsNumber(L, 2) || !Game::Lua::IsNumber(L, 3) ||
        !Game::Lua::IsNumber(L, 4) || !Game::Lua::IsNumber(L, 5)) {
        Game::Lua::Error(L,
            "Usage: C_AuctionHouse.PostItem(itemLocation, duration, quantity, numStacks, bid, buyout)");
        return 0;
    }

    int bagID = 0, slotIndex = 0;
    if (!ReadIntField(L, 1, "bagID", &bagID) ||
        !ReadIntField(L, 1, "slotIndex", &slotIndex))
        return Reject(L, "itemLocation must be a { bagID, slotIndex } table");

    const int duration = static_cast<int>(Game::Lua::ToNumber(L, 2));
    const int quantity = static_cast<int>(Game::Lua::ToNumber(L, 3));
    const int numStacks = static_cast<int>(Game::Lua::ToNumber(L, 4));
    const int bid = static_cast<int>(Game::Lua::ToNumber(L, 5));
    const int buyout = Game::Lua::IsNumber(L, 6) ? static_cast<int>(Game::Lua::ToNumber(L, 6)) : 0;

    const int runTime = MapDuration(duration);
    if (runTime == 0)
        return Reject(L, "invalid duration (use 1/2/3 or 120/480/1440)");
    if (quantity < 1 || quantity > 255)
        return Reject(L, "quantity must be 1..255");
    if (numStacks < 1)
        return Reject(L, "numStacks must be >= 1");
    if (bid < 1)
        return Reject(L, "bid must be >= 1");

    if (!AHOpen())
        return Reject(L, "auction house is not open");
    if (g_job.active)
        return Reject(L, "a multi-post is already in progress");

    const SlotInfo s = ReadSlot(L, bagID, slotIndex);
    if (s.item == nullptr)
        return Reject(L, "source slot is empty");

    const long need = static_cast<long>(quantity) * numStacks;
    if (static_cast<long>(s.count) < need)
        return Reject(L, "not enough items for numStacks x quantity");

    // A partial post (source larger than one stack) needs one reusable
    // general-bag slot to materialize each split. A whole-stack post
    // (count == quantity, numStacks == 1) needs none.
    if (s.count > quantity) {
        int fb = 0, fs = 0;
        if (!FindFreeGeneralSlot(L, &fb, &fs))
            return Reject(L, "need at least one free general bag slot to split");
    }

    g_job = Job{};
    g_job.active = true;
    g_job.srcBag = bagID;
    g_job.srcSlot = slotIndex;
    g_job.itemID = s.itemID;
    g_job.quantity = quantity;
    g_job.totalStacks = numStacks;
    g_job.postedStacks = 0;
    g_job.bid = bid;
    g_job.buyout = buyout;
    g_job.runTime = runTime;
    g_job.phase = Phase::Idle;

    Event::Custom::Fire(_evtStart.Slot(), "%d", numStacks);
    Game::Lua::PushBool(L, true);
    return 1;
}

const Game::Doc::Field kPostItemArgs[] = {
    Game::Doc::Req("itemLocation", "ItemLocation", "A bag slot: {bagID = B, slotIndex = S}."),
    Game::Doc::Req("duration", "number",
                   "1, 2 or 3 for 2, 8 or 24 hours; 120, 480 or 1440 also work."),
    Game::Doc::Req("quantity", "number", "Items in each auction, 1 to 255."),
    Game::Doc::Req("numStacks", "number", "The number of auctions."),
    Game::Doc::Req("bid", "number", "In copper, for each auction."),
    Game::Doc::Opt("buyout", "number", "0", "In copper, for each auction; 0 for no buyout."),
};
const Game::Doc::Field kPostItemRets[] = {
    Game::Doc::Req("accepted", "bool"),
    Game::Doc::Opt("reason", "string", nullptr, "Why the job was refused; nil when accepted."),
};
const Game::Doc::Function kPostItem{
    "Starts to post several auctions of one size from one bag slot.", kPostItemArgs,
    kPostItemRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_AuctionHouse", "PostItem",
                                     &Script_C_AuctionHouse_PostItem, &kPostItem);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace AuctionHouse::PostItem
