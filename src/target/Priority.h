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

#include <cstddef>
#include <cstdint>

namespace Target::Priority {

// A candidate's flag word — the 3.80.2 client's bits
// (docs/notes/soft-targeting-3802.md, "Flag word bits").
enum Flag : uint32_t {
    Unit = 0x1,
    Player = 0x2,
    GameObject = 0x4,
    Party = 0x8, // a player in the party (set with Raid)
    Raid = 0x10, // a player in the party or the raid
    Dead = 0x20,
    Enemy = 0x40,
    Friend = 0x80,
    Interact = 0x100,
    OwnObject = 0x200, // the player's channel object (the fishing bobber)
    Boss = 0x800,
    PvpPriority = 0x4000,
    CombatLock = 0x8000,
    PointBlank = 0x10000,
    InMelee = 0x20000,
    InFrustum = 0x40000,
    Visible = 0x80000, // in the frustum, and in line of sight where that is needed
    InRange = 0x100000,
    Excluded = 0x200000,
    InCone = 0x400000,
    CameraRay = 0x800000,
    FacingRay = 0x1000000,
    Critter = 0x2000000,
};

// Rebuilds the candidate list when TargetPriorityUpdateDelay has passed since
// the last rebuild. Run once a frame by the soft-target driver; true when it
// rebuilt.
bool Update();

// Rebuilds the list unless it was built at this millisecond, and restarts
// the TargetPriorityUpdateDelay wait (3.80.2's Tab does this before it picks).
void Refresh();

// Drops the list and rebuilds it on the next Update.
void Invalidate();

// The tab order: the candidates by tab score (the soft score plus the
// casting term), best first. Valid until the next rebuild.
size_t TabSize();
uint64_t TabGuid(size_t rank);
uint32_t TabFlags(size_t rank);

// `guid`'s place in the tab order, or -1 when it is not a candidate.
int TabRank(uint64_t guid);

// Some candidate is a combat-locked enemy inside the camera cone (3.80.2's
// "contextual relaxation" input), as of the last rebuild.
bool LockedInCone();

// TargetPriorityCombatLock, and its test (FUN_1405e3490) for the player and
// the unit `guid`: false for anything that is not a unit.
int CombatLockMode();
bool CombatLockedWith(uint64_t guid);

// TargetPriorityCombatLockContextualRelaxation.
bool CombatLockRelaxation();

// The camera's yaw, the angle the candidate geometry measures from; false
// without a camera.
bool CameraYaw(float &yaw);

// The candidate's flag word, or 0 when `guid` is not a candidate.
uint32_t FlagsOf(uint64_t guid);

// The candidate holds every bit of `mask` (FUN_1405e0f80). With Friend in
// the mask, the player counts too.
bool Has(uint64_t guid, uint32_t mask);

// The first candidate in soft order that holds `type`, is not excluded, is
// in view (and for enemy / friend in range), and is inside the arc (0
// directly in front, 1 the front cone, 2 anywhere on screen); 0 when none.
uint64_t First(uint32_t type, int arc);

} // namespace Target::Priority
