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
    Auto = 1,        // set by the game: TargetEnemyAttacker, the interact key on a unit token
    Locked = 2,      // stored for Attack, and for Auto with TargetAutoLock
    Attack = 3,      // the melee attack's own select; never stored
    Force = 4,       // SoftTargetForce: hidden from `target`, no PLAYER_TARGET_CHANGED
    InteractKey = 5, // the interact key on a soft / any token
    AutoSpell = 6,   // TargetAutoEnemy / TargetAutoFriend: your spell landed on the soft unit
    Chosen = 8,      // every other select: TargetUnit, assist, 1.12's own Tab, ...
    Click = 9,       // a click on the object in the world
    Tab = 10,        // Tab on the scored list (TargetNearestUseNew)
};

// The current target, or 0.
uint64_t Selection();

// The current target's reason; None with no target.
Why Current();

// The reason is a loose one (IsTargetLoose): the target was set by soft
// targeting, not chosen.
bool IsLoose();

// A Force target: the `target` token resolves nothing for it.
bool Hidden();

// `guid` is one of the soft targets: the soft enemy, friend or interact.
bool IsSoftTarget(uint64_t guid);

// Sets (or with 0 clears) the target for `why`, by 3.80.2's rules: a loose
// reason never replaces a locked target (and Auto replaces only an Auto
// one), Force over a loose target keeps its reason, Attack (and Auto with
// TargetAutoLock) is stored as Locked, and re-setting the target only raises
// its reason.
void Set(uint64_t guid, Why why);

// For its lifetime, the next select is for `why` (by Set()'s rules) instead
// of the reason its call site implies — for an engine function that selects
// on its own, such as the world right-click, called on a loose reason's
// behalf.
class SelectFor {
  public:
    explicit SelectFor(Why why);
    ~SelectFor();
    SelectFor(const SelectFor &) = delete;
    SelectFor &operator=(const SelectFor &) = delete;
};

// Changes the current target's reason in place: no select, no event
// (3.80.2's Tab over a loose target that already fits).
void Promote(Why why);

// Puts back a target and reason saved with Selection() / Current() before a
// temporary select (a click-cast macro's TargetUnit): a Force target is
// cleared for SoftTargetForce to set again, hidden, next frame; any other is
// selected again with its old reason, whatever replaced it; one whose unit
// is gone, or none, clears.
void Restore(uint64_t guid, Why why);

// The soft-target driver's steps, in its order each frame: a loose target
// whose object is gone is cleared; after the soft picks, SoftTargetForce;
// on a frame that rebuilt the candidate list, the loose-target expiry.
void DropVanished();
void ApplyForce();
void Expire();

} // namespace Target::Reason
