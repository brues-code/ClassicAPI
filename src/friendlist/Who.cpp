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

// `C_FriendList.SendWhoQueryByName` — sends a /who name-filter query
// invisibly (the server's standard "Players found" chat message
// suppressed; results buffer into the engine's WhoList so the
// existing `GetWhoInfo` / `WHO_LIST_UPDATE` work normally).
//
// Vanilla 1.12 exposes only `SendWho(query)` + `SetWhoToUI(flag)` as
// separate primitives; addons that want to query a specific player
// without triggering the friends-panel popup have to do the dance
// pfUI's `chat.lua` does (~50 lines: pending state, cooldown timer,
// FriendsFrame_OnEvent wrap, flag toggle). This collapses the
// invocation half of that dance to one call.
//
// Routing-flag save/restore: vanilla `VAR_WHO_TO_UI_FLAG` is
// process-global. Setting it to 1 for our query also affects any
// other /who response that lands afterward — a user typing `/who Bob`
// would inherit our flag and lose both the chat-count printer and
// the FriendsFrame popup. To keep our override local to OUR
// responses, we hook the SMSG_WHO handler at `FUN_005ADF60`,
// post-original, and restore the prior flag value once all in-flight
// queries we initiated have been answered.
//
// The friends-panel suppression itself still needs an addon-side
// `FriendsFrame_OnEvent` wrap — gated on `IsWhoQueryPending()` —
// because we don't hook the WHO_LIST_UPDATE Lua dispatch directly.
// With the wrap + gate, pfUI's ScanWhoName collapses to a one-liner.

#include "Game.h"
#include "Offsets.h"
#include "time/Clock.h"

#include <cstdint>
#include <cstring>

namespace FriendList::Who {

namespace {

// Server-enforced /who cooldown is ~5 seconds — a faster client gets
// silent-dropped. We mirror that on the client to avoid wasting
// queries that won't get a response, and to give addons a clean
// "did my query get sent?" signal via the bool return.
constexpr uint32_t kCooldownMs = 5000;

// Lost-response safety net. If a query's SMSG_WHO somehow never
// arrives (network blip, server drop), `g_inFlight` would otherwise
// stay positive forever and we'd never restore the routing flag.
// On every new send we check this window first and force-reset if
// the last send is older than this and the counter's still up.
constexpr uint32_t kPendingWindowMs = 10000;

uint32_t g_lastSendTick = 0;
bool g_haveSent = false;

// Count of /who queries we've initiated whose SMSG_WHO response we
// haven't seen yet. Drives `IsWhoQueryPending` directly — addons
// gating on this get an exact answer rather than the old 5s-timer
// heuristic. Also gates the flag-restore in the response hook.
int g_inFlight = 0;

// Snapshot of `VAR_WHO_TO_UI_FLAG` taken on the first send while
// `g_inFlight == 0`. Restored once `g_inFlight` returns to zero so
// our override doesn't leak into unrelated /who queries.
int g_savedFlag = 0;
bool g_flagOverridden = false;

bool OnCooldown() {
    if (!g_haveSent)
        return false;
    return Time::Clock::Elapsed(g_lastSendTick) < kCooldownMs;
}

void RestoreFlagIfOwned() {
    if (!g_flagOverridden)
        return;
    *reinterpret_cast<int *>(
        static_cast<uintptr_t>(Offsets::VAR_WHO_TO_UI_FLAG)) = g_savedFlag;
    g_flagOverridden = false;
}

// Force-reset if our in-flight counter has been stuck for longer
// than the response should ever take. Covers the (rare) lost-packet
// case where SMSG_WHO never arrives for a query we sent — without
// this, `IsWhoQueryPending` would stay true and the flag would never
// restore. Runs at the START of every new send so the safety check
// always precedes a fresh override.
void ResetIfStuck() {
    if (g_inFlight <= 0)
        return;
    if (!g_haveSent)
        return;
    if (Time::Clock::Elapsed(g_lastSendTick) < kPendingWindowMs)
        return;
    g_inFlight = 0;
    RestoreFlagIfOwned();
}

bool IsPending() {
    return g_inFlight > 0;
}

// `__thiscall(this = WhoSystem*, queryStr)` — the inner sender that
// `Script_SendWho` tail-calls. Parses the /who filter syntax and
// ships CMSG_WHO. Returns void; failure is silent (e.g. NULL system
// pointer pre-login).
using SendQuery_t = void(__thiscall *)(void *self, const char *queryStr);

bool SendQueryInternal(const char *fullQuery) {
    auto *system = *reinterpret_cast<void *const *>(
        static_cast<uintptr_t>(Offsets::VAR_SOCIAL_SYSTEM));
    if (system == nullptr)
        return false;
    auto fn = reinterpret_cast<SendQuery_t>(Offsets::FUN_WHO_SYSTEM_SEND_QUERY);
    fn(system, fullQuery);
    return true;
}

int *FlagPtr() {
    return reinterpret_cast<int *>(
        static_cast<uintptr_t>(Offsets::VAR_WHO_TO_UI_FLAG));
}

// Sets the routing flag to 1 (WhoList path) and remembers the prior
// value once per "in-flight burst". Only the FIRST send in a burst
// snapshots; subsequent overlapping sends ride on the same snapshot.
// `RestoreFlagIfOwned` puts it back once the matching responses have
// all been processed by `WhoResponse_h`.
void OverrideFlagForWhoList() {
    if (!g_flagOverridden) {
        g_savedFlag = *FlagPtr();
        g_flagOverridden = true;
    }
    *FlagPtr() = 1;
}

// Hook on the SMSG_WHO response handler (`FUN_005ADF60`, opcode 0x63).
// All SMSG opcode handlers in 1.12 are `__stdcall(uint32 opcode,
// void *packetReader)` — verified by the `ret 0x8` epilogue on
// `FUN_005ADF60` and its siblings registered via
// `FUN_005AB650(opcode, handler, 0)`. (An earlier version of this
// hook used `__cdecl` based on Ghidra's default decompile signature,
// which corrupted the stack on every call — the trampoline cleaned
// 8 bytes the caller had already cleaned, ESP slid down, and
// execution eventually landed in unmapped memory.)
//
// We invoke the trampoline first so the engine's response handling
// (including its `FrameScript_SignalEvent(WHO_LIST_UPDATE)` call
// into Lua) sees `IsWhoQueryPending() == true` and the flag still
// set to 1. After the original returns, we decrement and — when
// in-flight hits 0 — restore the pre-override flag value.
using WhoResponse_t = int(__stdcall *)(uint32_t opcode, void *packet);
WhoResponse_t WhoResponse_o = nullptr;

int __stdcall WhoResponse_h(uint32_t opcode, void *packet) {
    const int result = WhoResponse_o(opcode, packet);
    if (g_inFlight > 0) {
        g_inFlight--;
        if (g_inFlight == 0)
            RestoreFlagIfOwned();
    }
    return result;
}

static const Game::HookAutoRegister _hookreg{
    Offsets::FUN_SMSG_WHO_RESPONSE,
    reinterpret_cast<void *>(&WhoResponse_h),
    reinterpret_cast<void **>(&WhoResponse_o)};

// `C_FriendList.SendWhoQueryByName(name)` — fires a /who query for
// exactly one character name, buffered into the WhoList so a normal
// `WHO_LIST_UPDATE` + `GetWhoInfo(i)` flow can read the result.
//
// Returns:
//   `true`  — query was sent (sets pending state; cooldown begins).
//   `false` — on cooldown, name was empty, or the WhoSystem isn't
//             yet initialized (pre-login).
//
// Does NOT suppress the friends-panel popup on response. Addons that
// don't want the popup wrap `FriendsFrame_OnEvent` themselves and
// gate on `C_FriendList.IsWhoQueryPending()`:
//
//   local original = FriendsFrame_OnEvent
//   _G.FriendsFrame_OnEvent = function()
//       if event == "WHO_LIST_UPDATE" and C_FriendList.IsWhoQueryPending() then
//           return
//       end
//       return original()
//   end
//
// (Hooking the engine's signal dispatch to suppress automatically is
// a follow-up — would need a hook on `FrameScript_SignalEvent` or
// the SMSG_WHO handler, both higher-risk than the surface we have
// today.)
int __fastcall Script_C_FriendList_SendWhoQueryByName(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L,
            "Usage: C_FriendList.SendWhoQueryByName(name)");
        return 0;
    }
    const char *name = Game::Lua::ToString(L, 1);
    if (name == nullptr || *name == '\0') {
        Game::Lua::PushBool(L, 0);
        return 1;
    }
    if (OnCooldown()) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }

    // Build the "n-<name>" filter the engine's parser expects. Bound
    // by the realistic max character name length (12) plus the
    // 2-char prefix plus a NUL — 16 bytes is comfortably enough; cap
    // at 48 for paranoia in case a server allows longer names.
    char query[64];
    const size_t prefixLen = 2;
    const size_t maxName = sizeof(query) - prefixLen - 1;
    size_t nameLen = std::strlen(name);
    if (nameLen > maxName)
        nameLen = maxName;
    std::memcpy(query, "n-", prefixLen);
    std::memcpy(query + prefixLen, name, nameLen);
    query[prefixLen + nameLen] = '\0';

    // Reap stalled state from a prior send whose response never
    // arrived before overriding the flag for this one — otherwise
    // `g_savedFlag` would shift from the engine's real default
    // (typically 0) to whatever we last forced.
    ResetIfStuck();

    // Snapshot the pre-override flag (so we can restore in the
    // response hook) then force route-to-WhoList for our query.
    OverrideFlagForWhoList();
    if (!SendQueryInternal(query)) {
        // Send failed — usually pre-login (WhoSystem singleton
        // null). Nothing in flight, so we can immediately undo the
        // override and report failure.
        if (g_inFlight == 0)
            RestoreFlagIfOwned();
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }
    g_lastSendTick = Time::Clock::NowMs();
    g_haveSent = true;
    g_inFlight++;
    Game::Lua::PushBoolean(L, 1);
    return 1;
}

// `C_FriendList.IsWhoQueryPending()` — true while at least one of
// our `SendWhoQueryByName` calls is still awaiting its SMSG_WHO
// response. Backed by an exact in-flight counter that the
// `WhoResponse_h` hook decrements when the engine processes a
// response — addons gating their `FriendsFrame_OnEvent` wraps on
// this get a precise yes/no rather than a 5s-window heuristic.
//
// Counter has a 10-second safety net (`ResetIfStuck` at next send)
// for the rare case where a response packet is lost; in normal play
// the engine responds within hundreds of milliseconds.
int __fastcall Script_C_FriendList_IsWhoQueryPending(void *L) {
    Game::Lua::PushBoolean(L, IsPending());
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_FriendList", "SendWhoQueryByName",
                                     &Script_C_FriendList_SendWhoQueryByName);
    Game::Lua::RegisterTableFunction("C_FriendList", "IsWhoQueryPending",
                                     &Script_C_FriendList_IsWhoQueryPending);
}

// Clear transient /who state on every in-game Lua (re)build — login OR
// /reload. The DLL survives a logout→character-select→login, so a query
// whose SMSG_WHO response never crossed that boundary would otherwise leave
// g_inFlight stuck (IsWhoQueryPending() wrongly true for the next character)
// and our VAR_WHO_TO_UI_FLAG override forced on (routing the next
// character's unrelated /who to the WhoList). ResetIfStuck only heals this
// lazily on a *subsequent* send; a fresh-init reset makes every character
// start clean. RestoreFlagIfOwned puts the engine flag back to the value we
// snapshotted before overriding. ModuleAutoRegister fires per login and per
// /reload, before any addon runs, so this always precedes a new burst.
void ResetState() {
    g_inFlight = 0;
    RestoreFlagIfOwned();
    g_haveSent = false;
    g_lastSendTick = 0;
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::ModuleAutoRegister _resetOnInit{&ResetState};

} // namespace

} // namespace FriendList::Who
