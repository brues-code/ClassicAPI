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

// RAID_CLASS_COLORS as engine colour words, for engine code that colours by
// class where no Lua may run (the world tick binds nameplates inside the
// strata walk). Classes::ColorWatch refreshes it after every write it sees to
// RAID_CLASS_COLORS; a change inside one colour (`RAID_CLASS_COLORS.MAGE.r =
// x`) is not a write it sees, so the copy keeps the old value until the next
// one.
namespace Classes::ColorCache {

// Reads RAID_CLASS_COLORS[token].r/g/b for every ChrClasses.dbc class, as
// FrameXML's GetClassColor indexes it. Call from Lua only.
void Refresh(void *L);

// The colour RAID_CLASS_COLORS gave `classId` at the last refresh, as
// 0xAARRGGBB with alpha 0xFF, packed the way SetVertexColor packs (each
// channel clamped to 0..1, x 255 + 0.5, truncated; 0x0079ABD0). False when
// it gave no colour. Safe from any context.
bool Get(uint32_t classId, uint32_t &argb);

} // namespace Classes::ColorCache
