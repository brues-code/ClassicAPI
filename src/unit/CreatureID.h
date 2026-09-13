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

// `Unit::CreatureID` — a unit's creature_template entry (the NPC id), read the
// way the engine reads it for the creature's NAME: the instance-block entry at
// `[[unit + OFF_UNIT_GUID_PTR] + OFF_UNIT_INSTANCE_ENTRY]`. See CreatureID.cpp
// for why the GUID's entry bits are only a fallback (and the descriptor slot
// is not usable at all).
namespace Unit::CreatureID {

// The live template entry of a resolved CGUnit, or 0 for a null unit, a
// missing instance block, or a player (players carry entry 0).
uint32_t ForObject(const void *unit);

// The creature id for a GUID: the live instance-block entry when the object is
// in the object table, else — for a CREATURE GUID only — the entry packed in
// the GUID (the spawn row's first template; exact whenever the spawn has one
// id). 0 for players and every non-unit GUID, and for a pet that is out of
// view (a pet GUID packs the pet number, not a template).
uint32_t ForGuid(uint64_t guid);

} // namespace Unit::CreatureID
