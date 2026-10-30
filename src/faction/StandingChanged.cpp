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

// FACTION_STANDING_CHANGED — fires once per reputation change with
// `(factionID, newStanding, repGained)`. Polyfills modern WoW's event
// of the same name (modern fires `(factionID, newStanding)`); we add
// `repGained` as a third arg because the engine has the delta on hand
// in EDX and addons commonly need it for "+X reputation" displays
// without re-deriving it from the chat string. Vanilla 1.12 only
// exposes the localized `CHAT_MSG_COMBAT_FACTION_CHANGE` text, which
// addons currently scrape and reverse-resolve back to IDs.
//
// We hook the engine's per-rep-change notify dispatcher at
// `0x0062C5F0`. That function is the single chokepoint that fires the
// chat event after a successful standing update: it's only called when
// the value actually changed AND the engine's `notify` flag was set
// (i.e. from the per-update SMSG handler, not the bulk init at login).
// This matches modern WoW's semantics — FACTION_STANDING_CHANGED does
// not fire for initial faction sync.
//
// The hook target's calling convention is `__fastcall(ecx = factionID,
// edx = signedDelta)`. `delta` is forwarded as the `repGained` event
// arg (positive on gain, negative on loss). To produce the
// `newStanding` payload, we read back the live total via
// `FUN_REPUTATION_GET_STANDING(factionID)` — the setter has already
// written the new delta into the per-slot storage by the time our
// hook fires.

#include "StandingChanged.h"

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"

namespace Faction::StandingChanged {

namespace {

using GetStanding_t = int(__fastcall *)(int factionID);

constexpr const char *kEventName = "FACTION_STANDING_CHANGED";

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("factionID", "number"),
    Game::Doc::Req("newStanding", "number", "The faction's standing after the change."),
    Game::Doc::Req("repGained", "number", "Negative for a loss."),
};
const Game::Doc::Event kEventDoc{"Reputation", "Fires when the player's reputation changes.",
                                 kPayload};

const Event::Custom::AutoReserve _reserve{kEventName, &kEventDoc};

// In-flight snapshot of the current rep change. Set on hook entry,
// cleared on hook exit. WoW's main thread is the only one that runs
// the rep-update SMSG handler and Lua callbacks, so a plain file
// static is sufficient — no TLS needed.
LastChange g_last{};

int __fastcall Script_C_Reputation_GetLastStandingChange(void *L) {
    if (!g_last.valid)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(g_last.factionID));
    Game::Lua::PushNumber(L, static_cast<double>(g_last.newStanding));
    Game::Lua::PushNumber(L, static_cast<double>(g_last.delta));
    return 3;
}

const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("factionID", "number", nullptr,
                   "Nil, with the rest, outside a FACTION_STANDING_CHANGED or "
                   "CHAT_MSG_COMBAT_FACTION_CHANGE handler."),
    Game::Doc::Opt("newStanding", "number", nullptr, "The faction's standing after the change."),
    Game::Doc::Opt("repGained", "number", nullptr, "Negative for a loss."),
};
const Game::Doc::Function kGetLastStandingChange{
    "Returns the reputation change that FACTION_STANDING_CHANGED and "
    "CHAT_MSG_COMBAT_FACTION_CHANGE report, while their handlers run.",
    {}, kRets, nullptr, true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Reputation", "GetLastStandingChange",
                                     &Script_C_Reputation_GetLastStandingChange,
                                     &kGetLastStandingChange);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

FireNotify_t FireNotify_o = nullptr;

const LastChange &Last() { return g_last; }

void __fastcall FireNotify_h(int factionID, int delta) {
    // The engine's setter at 0x004D6330 has already written the new
    // standing into the per-slot storage by the time we get here, so
    // `FUN_REPUTATION_GET_STANDING` returns the post-change total. We
    // capture it before forwarding so anything reading
    // `g_last` from inside the engine's chat dispatch (and from our
    // own `FACTION_STANDING_CHANGED` observers below) sees a
    // consistent snapshot.
    auto getStanding = reinterpret_cast<GetStanding_t>(
        Offsets::FUN_REPUTATION_GET_STANDING);
    const int newStanding = getStanding(factionID);
    g_last = {true, factionID, newStanding, delta};

    // Forward to the engine first so its CHAT_MSG_COMBAT_FACTION_CHANGE
    // fires before our FACTION_STANDING_CHANGED — preserves ordering
    // any addon relying on the chat event firing first would expect.
    FireNotify_o(factionID, delta);

    const int slot = _reserve.Slot();
    if (slot >= 0)
        Event::Custom::Fire(slot, "%d%d%d", factionID, newStanding, delta);

    g_last = {};
}

static const Game::HookAutoRegister _hookreg{
    Offsets::FUN_REPUTATION_FIRE_NOTIFY,
    reinterpret_cast<void *>(&FireNotify_h),
    reinterpret_cast<void **>(&FireNotify_o)};

} // namespace Faction::StandingChanged
