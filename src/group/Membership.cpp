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

// GROUP_JOINED(category, partyGUID) / GROUP_LEFT(category, partyGUID):
// retail's events for the player entering and leaving a group.
//
// 1.12 signals every roster change with PARTY_MEMBERS_CHANGED and
// RAID_ROSTER_UPDATE (no-arg, EVENT_PARTY_MEMBERS_CHANGED /
// EVENT_RAID_ROSTER_UPDATE), after the roster is rebuilt. The events ride on
// those two signals: when the player's membership (IsInGroup's test, a party
// or a raid roster with members) differs from the last one seen, one of them
// fires, before the engine's own roster event.
//
// - category: retail's LE_PARTY_CATEGORY_HOME (1). 1.12 has only that group;
//   a battleground puts the player in an ordinary raid.
// - partyGUID: nil. Retail's party GUID names the group on the server; the
//   1.12 group list (SMSG_GROUP_LIST) carries no group id.
// - The membership seen at each world entry is taken as it is, with no
//   event: a player who logs in or reloads inside a group gets no
//   GROUP_JOINED (our choice; retail's rule for a login is unknown).

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "event/EnteringWorld.h"
#include "event/SignalHook.h"
#include "group/Roster.h"

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
const Event::Custom::AutoReserve kJoined{"GROUP_JOINED", &kJoinedDoc};
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
    Fire(inGroup ? kJoined.Slot() : kLeft.Slot());
}

const Event::SignalHook::AutoObserve _party{Offsets::EVENT_PARTY_MEMBERS_CHANGED, &OnRoster};
const Event::SignalHook::AutoObserve _raid{Offsets::EVENT_RAID_ROSTER_UPDATE, &OnRoster};

void OnEnterWorld() { g_inGroup = InGroup(); }

const Event::EnteringWorld::AutoObserve _enterWorld{&OnEnterWorld};

} // namespace

} // namespace Group::Membership
