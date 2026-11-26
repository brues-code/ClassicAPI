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

namespace Item::Cooldown {

// The engine's item cooldown query (FUN_ITEM_QUERY_COOLDOWN): start tick and
// duration in engine ms (GetTime() epoch, unsigned — see Time::Clock), and
// the enable flag (0 while the cooldown waits to start). All 0 for
// `itemID <= 0`.
void Query(int itemID, uint32_t *startMs, uint32_t *durationMs, uint32_t *enable);

// Pushes `(startTime, duration, enable)` onto `L` as three numbers —
// matching the shape of vanilla's existing `GetContainerItemCooldown` /
// `GetInventoryItemCooldown`. `itemID <= 0` pushes `(0, 0, 0)` so the
// caller can blindly return 3 values without a nil branch.
//
// `startTime` and `duration` are `GetTime()`-compatible seconds;
// `enable` is `1` for "ready or counting down" / `0` for "used but
// cooldown is on hold" (potion-in-combat case).
void PushCooldown(void *L, int itemID);

} // namespace Item::Cooldown
