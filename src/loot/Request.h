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

// The one place the codebase sends `CMSG_LOOT` for a unit. Every caller of
// `FUN_CMSG_LOOT_UNIT` goes through here, because a loot request is not
// self-contained: the engine HOLDS the target on the player
// (`OFF_PLAYER_LOOT_TARGET_GUID`) as it sends, and its `CanLootNow` refuses
// every later loot while that is held. With no window open, only a server
// reply clears it — `CloseLoot()` and a movement start do not. So a request
// the server never answers blocks all looting until relog (issue #113).
namespace Loot::Request {

// Whether the player is mounted — the gate the engine's right-click handler
// (`CGUnit_C::Interact`, `0x0060BEA0`) applies before its loot leg.
bool IsMounted(const void *player);

// Whether the server grants us loot on `unit` (`UNIT_DYNFLAG_LOOTABLE`).
bool IsLootable(const void *unit);

// Whether `target` is within the reach `CanLootNow` (`0x005EC110`) demands
// when the player is not walked up: both units' `+0x1F0` reach plus the
// engine's live leeway, at least its live minimum, against the 3D distance.
// `Send` never walks, so a target outside this reach is one the server
// refuses with "too far away".
bool InReach(void *player, void *target);

// The player's held loot target. The engine writes it when the request goes
// out and clears it when the server answers with a failure or a release, so
// a caller waiting on `guid` sees the refusal as this no longer being `guid`.
uint64_t HeldTarget(const void *player);

// Sends the loot request the way a right-click would: refused while mounted
// (the click's gate, which `FUN_CMSG_LOOT_UNIT` itself lacks), never
// auto-walks. Returns whether the request went out — false when mounted or
// when the engine's own pre-checks refused (another loot held, moving,
// falling, not in control, ...), in which case no response is coming.
bool Send(void *player, void *target, uint64_t guid);

// For a caller that gave up waiting: if the player still holds `guid` as
// its loot target, the server never answered — release it the way the
// engine's release response does, so looting is not left blocked.
void ReleaseIfUnanswered(void *player, uint64_t guid);

} // namespace Loot::Request
