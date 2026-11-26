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

namespace Action::ExtendedUse {

// UseActionSlot (0x004E5EE0) for one of Action::Extended's slots: place a
// held payload when `checkCursor`, else run the macro, cast or take back the
// spell, or use / equip the item. `onSelf` casts at the active player.
void Use(int slot0, bool checkCursor, bool onSelf);

} // namespace Action::ExtendedUse
