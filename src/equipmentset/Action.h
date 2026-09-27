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

#include "Set.h"

#include <cstddef>
#include <cstdint>

// Equipment sets as action-bar buttons — the 3.3.5 action type
// `0x20000000 | setID` (`Offsets::ACTION_TYPE_EQUIPMENT_SET`) and cursor
// type 13 (`Offsets::CURSOR_TYPE_EQUIPMENT_SET`).
namespace EquipmentSet::Action {

// The set on the cursor, or 0 when the cursor holds anything else.
uint32_t CursorSetID();

// The set on 0-based action slot `slot0`, or 0 when it holds anything else.
uint32_t SlotSetID(uint32_t slot0);

// Puts `setID` on the cursor, the way the engine picks up a macro. False
// when there is no such set.
bool Pickup(uint32_t setID);

// "Interface\Icons\<icon>" for `set`. False when it doesn't fit `out`.
bool IconPath(const Set &set, char *out, size_t outSize);

// Name / icon changed — repaint every button holding `setID`.
void Repaint(uint32_t setID);

// Set gone — clear every button holding `setID`.
void ClearButtons(uint32_t setID);

} // namespace EquipmentSet::Action
