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

// `GROUP_ROSTER_UPDATE` — the group's roster changed, once per frame, as the
// 3.80.2 client fires it: the party update and the group destroy queue it as a
// one-entry deferred event (0x140EC0890, "GroupRosterUpdateEvent"), and the
// deferred-event flush near the end of the world update fires it once, with
// no payload (0x140E9E460). A join or a leave fires GROUP_JOINED or
// GROUP_LEFT immediately and this one later in the frame.
//
// 1.12 signals a roster change with PARTY_MEMBERS_CHANGED and
// RAID_ROSTER_UPDATE, joins and leaves included. Each arms the event here,
// and the world tick (the same flush point as BAG_UPDATE_DELAYED's) fires it.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "event/SignalHook.h"
#include "tick/WorldTick.h"

namespace Group::RosterUpdate {

namespace {

const Game::Doc::Event kDoc{"PartyInfo",
                            "Fires once in each frame in which the roster of the player's group changed.", {}};
const Event::Custom::AutoReserve kEvent{"GROUP_ROSTER_UPDATE", &kDoc};

bool g_pending = false;

void Arm() { g_pending = true; }

// Cleared before the fire, so a roster change in a handler fires next frame.
void OnWorldTick() {
    if (!g_pending)
        return;
    g_pending = false;
    const int slot = kEvent.Slot();
    if (slot >= 0)
        Event::Custom::Fire(slot, "");
}

const Event::SignalHook::AutoObserve _party{Offsets::EVENT_PARTY_MEMBERS_CHANGED, &Arm};
const Event::SignalHook::AutoObserve _raid{Offsets::EVENT_RAID_ROSTER_UPDATE, &Arm};
const Tick::WorldTick::AutoSubscribe _tick{&OnWorldTick};

} // namespace

} // namespace Group::RosterUpdate
