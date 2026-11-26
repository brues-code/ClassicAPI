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

// The set on 0-based action slot `slot0` (the engine's 0..119 or
// Action::Extended's 120..359), or 0 when it holds anything else.
uint32_t SlotSetID(uint32_t slot0);

// The set of an action word, or 0 when the word is not an equipment set.
uint32_t WordSetID(uint32_t word);

// What a button holding `setID` answers, as the readers' set branches do for
// a slot: usable (4.3.4 `UpdateUsableAction`'s set branch), icon path and name
// (null once the set is gone), and use (WEAR_EQUIPMENT_SET while the set
// exists). Action::Extended's slots read these for their set words.
bool Usable(uint32_t setID);
const char *Texture(uint32_t setID);
const char *Name(uint32_t setID);
void Use(uint32_t setID);

// Puts `setID` on the cursor, the way the engine picks up a macro. False
// when there is no such set.
bool Pickup(uint32_t setID);

// "Interface\Icons\<icon>" for `set`. False when it doesn't fit `out`.
bool IconPath(const Set &set, char *out, size_t outSize);

// Name / icon changed — repaint every button holding `setID`, in both ranges.
void Repaint(uint32_t setID);

// Set gone — clear every button holding `setID`, in both ranges.
void ClearButtons(uint32_t setID);

} // namespace EquipmentSet::Action
