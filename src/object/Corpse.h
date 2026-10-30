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

// Player corpses (`TYPEMASK_CORPSE`) and the player each belongs to, read
// from the corpse's CORPSE_FIELD_OWNER.
namespace Object::Corpse {

// The visible corpse `owner` left, not counting bones, or nullptr — 4.3.4's
// ClntObjMgrFindCorpseByOwner, over the visible objects.
void *ByOwner(uint64_t owner);

// The player who left the corpse `guid`, or 0 when `guid` is not a visible
// corpse.
uint64_t OwnerOf(uint64_t guid);

} // namespace Object::Corpse
