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

} // namespace Item::Use
