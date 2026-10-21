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

#pragma once

// The scan every soft target shares (interact, enemy, friend): objects drawn
// on screen, inside the kind's arc, nearest first. Each kind adds its own
// test of what qualifies and how the range applies.

#include "cvar/Factory.h"

#include <cstdint>

namespace Target::SoftTarget {

// A SoftTarget<Kind> cvar's 0 off / 1 gamepad / 2 KBM / 3 always. 1.12 has
// no gamepad, so only 2 and 3 turn the kind on.
bool Enabled(CVar::Factory::Handle enable);

// Where the player stands and what the kind's arc and range cvars allow.
struct Area {
    void *player;
    float pos[3];
    float yaw; // the camera's
    int arc;
    float coneHalf;
    float rangeSq;
};

// Fills `area` from the player and the kind's cvars. The range is capped by
// the tab-target range targetNearestDistance. False outside the world.
bool Begin(Area &area, CVar::Factory::Handle arc, CVar::Factory::Handle range,
           float defaultRange);

// True when `object` is drawn and inside the arc; `distSq` is its squared
// distance from the player. The range is the caller's test.
bool InView(const Area &area, void *object, float &distSq);

// A kind's test for an object in view.
using Accept = bool (*)(const Area &area, void *object, float distSq, void *ctx);

// The GUID of the nearest object of `typeMask` in view that `accept` takes,
// or 0. The player is never a candidate.
uint64_t Nearest(const Area &area, int typeMask, Accept accept, void *ctx);

} // namespace Target::SoftTarget
