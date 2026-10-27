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
// Targeted single-item looting. Given a corpse GUID and a desired
// itemID, opens that corpse's loot window via the engine's standard
// `FUN_005DF2A0` path, finds the slot containing that itemID once the
// window populates, and dispatches `CMSG_LOOT_ITEM` directly through
// `FUN_005E1AD0`. The Lua-side `LootSlot` wrapper isn't used — see
// the `FUN_CMSG_LOOT_ITEM` comment in Offsets.h for why.
//
// Fast path when the window is already open for the requested target:
// skip the open round-trip and dispatch immediately. This makes
// "take items A, B, C from corpse X" a series of cheap calls — the
// first opens the window, the rest land in fast-path.

#include "../Game.h"
#include "../Offsets.h"
#include "../guid/Guid.h"
#include "../object/Resolve.h"
#include "../tick/WorldTick.h"
#include "Request.h"

#include <cstdint>

namespace Loot::UnitItem {

namespace {

constexpr int kStepTimeoutTicks = 180;

enum class State {
    Idle,
    WaitingOpen,
};

// `FUN_005E1AD0` is `__stdcall(uint8_t)`, not `__fastcall` — the
// callee reads its arg at `[ebp+8]` (stack) and ends with `ret 4`.
// Mis-declaring as `__fastcall` makes MSVC skip the push but the
// callee still `ret 4`'s, drifting the caller's ESP by 4 bytes per
// call and corrupting the surrounding frame. The first symptom is a
// crash several frames later on an unrelated vtable lookup —
// exactly the EIP=0x00000A70-style faults we saw before this fix.
using SendLootItem_t = void(__stdcall *)(uint8_t slotByte);

State s_state = State::Idle;
uint64_t s_targetGuid = 0;
uint32_t s_desiredItemID = 0;
int s_ticksSinceTransition = 0;

uint64_t ReadLootTargetGuid() {
    const uint32_t lo = Game::Read<uint32_t>(Offsets::VAR_LOOT_GUID_LO);
    const uint32_t hi = Game::Read<uint32_t>(Offsets::VAR_LOOT_GUID_HI);
    return static_cast<uint64_t>(hi) << 32 | lo;
}

// Walks the currently-open loot window's slots looking for `itemID`.
// Returns the wire-protocol slot byte the server expects in
// `CMSG_LOOT_ITEM`, or `-1` if no slot matches. First match wins; if
// the same itemID appears twice, only the first is reachable through
// this function (caller can issue a second call after the first
// completes).
int FindWireSlotForItemID(uint32_t itemID) {
    for (int slot = 0; slot < Offsets::LOOT_MAX_SLOTS; ++slot) {
        auto *entry = reinterpret_cast<const uint8_t *>(
            Offsets::VAR_LOOT_SLOTS + slot * Offsets::LOOT_SLOT_STRIDE);
        const uint32_t id = *reinterpret_cast<const uint32_t *>(entry);
        if (id == 0)
            continue;
        if (id == itemID)
            return static_cast<int>(*(entry + Offsets::OFF_LOOT_SLOT_WIRE_INDEX));
    }
    return -1;
}

bool TakeItem(uint32_t itemID) {
    const int slotByte = FindWireSlotForItemID(itemID);
    if (slotByte < 0)
        return false;
    auto Send = reinterpret_cast<SendLootItem_t>(Offsets::FUN_CMSG_LOOT_ITEM);
    Send(static_cast<uint8_t>(slotByte));
    return true;
}

void Reset() {
    s_state = State::Idle;
    s_targetGuid = 0;
    s_desiredItemID = 0;
}

void OnTick() {
    if (s_state == State::Idle)
        return;
    ++s_ticksSinceTransition;
    if (s_state == State::WaitingOpen) {
        if (ReadLootTargetGuid() == s_targetGuid) {
            TakeItem(s_desiredItemID);
            Reset();
            return;
        }
        if (s_ticksSinceTransition >= kStepTimeoutTicks) {
            // Never opened — release a request the server never
            // answered, or it blocks all looting (see Request.h).
            if (void *player = Game::ResolveUnitToken("player"))
                Request::ReleaseIfUnanswered(player, s_targetGuid);
            Reset();
        }
    }
}

const Tick::WorldTick::AutoSubscribe _tick{&OnTick};

// `C_Loot.LootUnitItem(guid, itemID)` — takes a specific item from a
// specific corpse. If the corpse's loot window is already open, the
// item is dispatched synchronously and the function returns `true` if
// the slot was found, `false` if not. Otherwise the corpse's window is
// opened asynchronously, and once it populates the item is sent
// automatically. The async path returns `true` once the request is
// queued; there's no "did it succeed" event — observe the player's
// inventory or `LOOT_OPENED`/`ITEM_PUSH` events to confirm.
//
// Returns `false` immediately if:
//   - args are missing or invalid types,
//   - another `LootUnitItem` is mid-flight,
//   - the world isn't loaded,
//   - the target GUID doesn't resolve to a visible unit,
//   - the player is mounted, or can't loot right now (another loot
//     window open, falling, not in control),
//   - (sync path only) no slot in the currently-open window holds
//     the requested itemID.
//
// The CMSG_LOOT_ITEM send bypasses the BoP-confirmation dialog the
// vanilla UI normally shows. Server still enforces all real
// permissions (loot rights, distance, master-loot rules); the
// client-side BoP-prompt is purely a courtesy that callers of this
// programmatic API have already opted out of by virtue of asking.
int __fastcall Script_LootUnitItem(void *L) {
    if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: C_Loot.LootUnitItem(guid, itemID)");
        return 0;
    }
    uint64_t guid = 0;
    if (!Guid::Parse(Game::Lua::ToString(L, 1), &guid) || guid == 0) {
        Game::Lua::Error(L, "Usage: C_Loot.LootUnitItem(guid, itemID) — invalid guid");
        return 0;
    }
    const uint32_t itemID = static_cast<uint32_t>(Game::Lua::ToNumber(L, 2));

    if (s_state != State::Idle) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    if (*reinterpret_cast<void *volatile *>(Offsets::VAR_LOCAL_PLAYER_PTR) == nullptr) {
        Game::Lua::PushBool(L, false);
        return 1;
    }

    // Fast path: window already open for the requested target — dispatch
    // CMSG_LOOT_ITEM synchronously. No waiting on `WorldTick`, no state
    // transition needed.
    if (ReadLootTargetGuid() == guid) {
        Game::Lua::PushBool(L, TakeItem(itemID));
        return 1;
    }

    void *player = Game::ResolveUnitToken("player");
    if (player == nullptr) {
        Game::Lua::PushBool(L, false);
        return 1;
    }

    void *target = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, nullptr, 0);
    if (target == nullptr) {
        Game::Lua::PushBool(L, false);
        return 1;
    }

    if (!Request::Send(player, target, guid)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }

    s_targetGuid = guid;
    s_desiredItemID = itemID;
    s_state = State::WaitingOpen;
    s_ticksSinceTransition = 0;
    Game::Lua::PushBool(L, true);
    return 1;
}

void Register() {
    Game::Lua::RegisterTableFunction("C_Loot", "LootUnitItem",
                                     &Script_LootUnitItem);
}

} // namespace

static const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace Loot::UnitItem
