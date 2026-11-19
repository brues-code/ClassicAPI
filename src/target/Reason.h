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

#include <cstdint>

namespace Target::Reason {

// Why the target was set — the 3.80.2 client's set-target reasons, by its
// own values (docs/notes/soft-targeting-3802.md). Only the ones something
// here sets are listed.
enum Why : int {
    None = 0,
    Locked = 2,    // stored for Attack
    Attack = 3,    // the melee attack's own select; never stored
    Force = 4,     // SoftTargetForce: hidden from `target`, no PLAYER_TARGET_CHANGED
    AutoSpell = 6, // TargetAutoEnemy / TargetAutoFriend: your spell landed on the soft unit
    Chosen = 8,    // every other select: a click, Tab, TargetUnit, ...
};

// The current target, or 0.
uint64_t Selection();

// The current target's reason; None with no target.
Why Current();

// The reason is a loose one (IsTargetLoose): the target was set by soft
// targeting, not chosen.
bool IsLoose();

// The target when it is locked (not loose), else 0.
uint64_t LockedTarget();

// A Force target: the `target` token resolves nothing for it.
bool Hidden();

// Sets (or with 0 clears) the target for `why`, by 3.80.2's rules: a loose
// reason never replaces a locked target, Force over a loose target keeps
// its reason, Attack is stored as Locked, and re-setting the target only
// raises its reason.
void Set(uint64_t guid, Why why);

} // namespace Target::Reason
