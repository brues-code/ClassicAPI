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

namespace Unit::TokenEvents {

// Re-evaluates whether the engine watches `guid`'s unit for ClassicAPI's
// tokens (focus, nameplateN, markN). The owner of a token calls it after the
// token is given to or taken from a unit; 0 and units out of range are no-ops
// (a unit coming into range is evaluated by the engine itself).
void Refresh(uint64_t guid);

// The engine's per-token unit-event broadcast, then the same event for each
// ClassicAPI token `*guid` has. ClassicAPI code that re-fires a unit event
// calls this instead of FUN_UNIT_EVENT_BROADCAST.
void __fastcall Broadcast(const uint64_t *guid, uint32_t eventId);

} // namespace Unit::TokenEvents
