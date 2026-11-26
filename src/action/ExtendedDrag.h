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

// Dropping the cursor on, and picking up from, the action slots of
// Action::Extended (121..360): the engine's PlaceOnActionSlot and
// PickupActionSlot, mirrored for slots it bounds out.
namespace Action::ExtendedDrag {

// The gate PickupActionSlot (0x004E6130) and UseActionSlot (0x004E5EE0) open
// with: the cursor holds a spell, a bag item, a macro or an action-bar item
// (kind 7), so the slot takes it instead.
bool CursorWouldPlace();

// FUN_ACTION_PLACE (PlaceOnActionSlot, 0x004E62E0) for one of our slots.
void Place(int slot0);

// FUN_ACTION_PICKUP (PickupActionSlot, 0x004E6130) for one of our slots.
void Pickup(int slot0);

} // namespace Action::ExtendedDrag
