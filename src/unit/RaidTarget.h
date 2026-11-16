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

namespace Unit::RaidTarget {

// Number of raid-target markers (star … skull), i.e. the valid `markN`
// range is `1..kMarkCount`.
constexpr int kMarkCount = 8;

// GUID currently wearing raid marker `mark` (`1..kMarkCount`), or `0`
// when that marker is unassigned / `mark` is out of range. Reads the
// engine's marker table directly (`VAR_RAID_TARGET_GUIDS`). Consumed by
// the `markN` branch of the token resolver in `unit/TokenExtensions.cpp`,
// mirroring `NamePlate::Events::GetGUIDByIndex` / `Unit::Focus::Get`.
uint64_t GetGUIDByMark(int mark);

// The marker (`1..kMarkCount`) `guid` wears, or 0 for none and for a zero
// GUID, through the engine's own lookup (FUN_RAID_TARGET_FIND_SLOT).
int MarkOf(uint64_t guid);

} // namespace Unit::RaidTarget
