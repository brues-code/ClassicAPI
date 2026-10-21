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

namespace Target::Loose {

// The current target, or 0.
uint64_t Selection();

// True while the target is loose: set by the soft targets, not chosen.
bool IsLoose();

// The target when it is locked (chosen), else 0 — what the soft-target
// rules (SoftTargetWithLocked, SoftTargetMatchLocked) call the locked target.
uint64_t Locked();

// FUN_SET_SELECTION_BY_GUID for a select that is not a choice: re-selecting
// the loose target leaves it loose. The attack's own selects use it.
void SelectKeepingLoose(uint64_t guid);

// Makes `guid` the loose target, or with 0 clears a loose target.
void SetLoose(uint64_t guid);

} // namespace Target::Loose
