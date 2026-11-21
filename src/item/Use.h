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

namespace Item::Use {

// Uses `item` (a `CGItem *`) through the engine's `CGItem::Use`
// (`FUN_ITEM_USE`, `0x5d8d00`), with `targetGuid` as the implicit cast
// target (0 = none). Returns the engine's result: true only when the item's
// spell was cast or began targeting (`0x6e5a90`'s `al`). Every other branch —
// a refusal, opening, unwrapping, reading, the bind-on-use confirmation —
// returns false.
bool UseItem(const uint8_t *item, uint64_t targetGuid);

// The at-cursor use (3.80.2's target "cursor"): cancel any placement already
// up, use `item`, then commit its ground placement at the cursor
// (`Spell::Placement::CommitAtCursor`), only when the engine reports the
// item's spell began. Returns whether the spell went out at the cursor; an
// item with no ground effect is still used, and returns false.
bool UseAtCursor(const uint8_t *item);

// The use at a named unit, as 3.80.2 makes it (a cast request with an
// explicit target places a ground spell at that unit's position): `item`
// goes to the unit `guid`, and a ground-target item is placed at its feet
// (`Spell::Placement::FinishAtUnit`). A unit not in the object table, or
// guid 0, is a use with no target.
bool UseAtUnit(const uint8_t *item, uint64_t guid);

} // namespace Item::Use
