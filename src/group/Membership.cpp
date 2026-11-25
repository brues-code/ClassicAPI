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

// GROUP_JOINED / GROUP_FORMED / GROUP_LEFT(category, partyGUID): the events
// for the player entering and leaving a group, as the 3.80.2 client fires
// them from its party update (0x1417FFE10), immediately:
//   - GROUP_JOINED when it creates a group object (one with no members
//     before, 0x1417FD5F0), GROUP_LEFT when it destroys one (0x1417FF750).
//   - GROUP_FORMED once per group object, when the group holds two players
//     (0x1417FD6B6).
//   - Logout frees the group objects and fires nothing (0x141693A90), so the
//     first party update after a login fires GROUP_JOINED; /reload and a
//     loading screen keep the object and fire nothing.
//
// 1.12 signals every roster change with PARTY_MEMBERS_CHANGED and
// RAID_ROSTER_UPDATE (no-arg, EVENT_PARTY_MEMBERS_CHANGED /
// EVENT_RAID_ROSTER_UPDATE), after the roster is rebuilt. The events ride on
// those two signals: when the player's membership (IsInGroup's test, a party
// or a raid roster with members) differs from the last one seen, JOINED or
// LEFT fires, before the engine's own roster event. A 1.12 group holds you
// and at least one other player, so GROUP_FORMED follows every GROUP_JOINED.
// The state survives /reload in the DLL and is reset at logout (the glue
// pass), with no event.
//
// - category: retail's LE_PARTY_CATEGORY_HOME (1). 1.12 has only that group;
//   a battleground puts the player in an ordinary raid.
// - partyGUID: nil. 3.80.2's party GUID names the group on the server; the
//   1.12 group list (SMSG_GROUP_LIST) carries no group id. For the same
//   reason a move from one group straight into another (3.80.2: LEFT, then
//   JOINED) is not seen.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "event/SignalHook.h"
#include "group/Roster.h"
#include "lua/Arg.h"
#include "unit/Identity.h"

namespace Group::Membership {

namespace {

constexpr int kPartyCategoryHome = 1; // LE_PARTY_CATEGORY_HOME

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("category", "number", "Always 1, LE_PARTY_CATEGORY_HOME."),
    Game::Doc::Opt("partyGUID", "WOWGUID", nullptr, "Always nil on this client."),
};
const Game::Doc::Event kJoinedDoc{"PartyInfo", "Fires when the player joins a party or a raid.",
                                  kPayload};
const Game::Doc::Event kLeftDoc{"PartyInfo", "Fires when the player leaves a party or a raid.",
                                kPayload};
const Game::Doc::Event kFormedDoc{
    "PartyInfo", "Fires once for each group the player is in, when the group holds two players.", kPayload};
const Event::Custom::AutoReserve kJoined{"GROUP_JOINED", &kJoinedDoc};
const Event::Custom::AutoReserve kFormed{"GROUP_FORMED", &kFormedDoc};
const Event::Custom::AutoReserve kLeft{"GROUP_LEFT", &kLeftDoc};

bool g_inGroup = false;

bool InGroup() { return Group::Roster::RaidCount() > 0 || Group::Roster::PartyCount() > 0; }

void Fire(int event) {
    void *L = Game::Lua::State();
    if (!Event::Custom::HasListeners(event) || L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushNumber(L, kPartyCategoryHome);
    Game::Lua::PushNil(L);
    Event::Custom::FireValues(event, L, top + 1, 2);
    Game::Lua::SetTop(L, top);
}

void OnRoster() {
    const bool inGroup = InGroup();
    if (inGroup == g_inGroup)
        return;
    g_inGroup = inGroup;
    if (!inGroup) {
        Fire(kLeft.Slot());
        return;
    }
    Fire(kJoined.Slot());
    Fire(kFormed.Slot());
}

const Event::SignalHook::AutoObserve _party{Offsets::EVENT_PARTY_MEMBERS_CHANGED, &OnRoster};
const Event::SignalHook::AutoObserve _raid{Offsets::EVENT_RAID_ROSTER_UPDATE, &OnRoster};

// Logout: the first roster update after the next login joins again.
void ResetOnLogout() { g_inGroup = false; }

const Game::GlueModuleAutoRegister _logout{&ResetOnLogout};

// UnitInAnyGroup([unit [, partyIndex]]) — 3.80.2's 0x140E390D0: the unit is
// in the player's home group (partyIndex 1) or instance group (2), either
// when none is given. 1.12 has only the home group (a battleground raid is
// it), so 2 is always false. 3.80.2 also counts a unit a member controls and
// a stranger flagged as in some party (player +0x12ED8 / +0x12ED9); 1.12 has
// neither. The player itself is in one while grouped.
int __fastcall Script_UnitInAnyGroup(void *L) {
    constexpr const char *kUsage = "Usage: local result = UnitInAnyGroup([unit, partyIndex])";
    uint64_t guid;
    if (!Game::Lua::Arg::UnitToken(L, 1, kUsage, guid, true))
        return 0;
    bool home = true;
    if (Game::Lua::Type(L, 2) > Game::Lua::TYPE_NIL) {
        uint32_t index;
        if (!Game::Lua::Arg::Index(L, 2, kUsage, index))
            return 0;
        home = index == static_cast<uint32_t>(kPartyCategoryHome - 1);
    }
    bool in = false;
    if (home && guid != 0)
        in = guid == Unit::Identity::PlayerGuid()
                 ? InGroup()
                 : Group::Roster::IsPartyMember(guid) || Group::Roster::IsRaidMember(guid);
    Game::Lua::PushBool(L, in);
    return 1;
}

const Game::Doc::Field kAnyGroupArgs[] = {
    Game::Doc::Opt("unit", "UnitToken"),
    Game::Doc::Opt("partyIndex", "luaIndex", nullptr, "1 for the home group, 2 for an instance group; both when nil."),
};
const Game::Doc::Field kAnyGroupRets[] = {Game::Doc::Req("result", "bool")};
const Game::Doc::Function kUnitInAnyGroup{"Returns true if the unit is in one of the player's groups.",
                                          kAnyGroupArgs, kAnyGroupRets, "Unit"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitInAnyGroup", &Script_UnitInAnyGroup, &kUnitInAnyGroup);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Group::Membership
