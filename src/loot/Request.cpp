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

#include "Request.h"

#include "../Game.h"
#include "../Offsets.h"

namespace Loot::Request {

namespace {

// `FUN_CMSG_LOOT_UNIT` is `__thiscall(player, target, useDistanceCheck)`.
// MSVC `__thiscall` puts `player` in ECX and the rest on the stack —
// matching the engine's own callsite at `FUN_0060FA20`. The trailing
// `char` argument is padded to 4 stack bytes by the ABI.
using LootUnit_t = void(__thiscall *)(void *player, void *target,
                                      char useDistanceCheck);
using UpdateBaseAnimation_t = void(__thiscall *)(void *unit, uint32_t mask);
using CloseLootInner_t = void(__fastcall *)(int sendRelease,
                                             char earlyReturnOnGameObject,
                                             char showError);

uint64_t HeldTarget(const void *player) {
    return Game::Read<uint64_t>(player, Offsets::OFF_PLAYER_LOOT_TARGET_GUID);
}

} // namespace

bool IsMounted(const void *player) {
    auto *fields = Game::Read<const uint8_t *>(player, Offsets::OFF_UNIT_DESCRIPTOR);
    return Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_MOUNTDISPLAYID) != 0;
}

bool Send(void *player, void *target, uint64_t guid) {
    if (IsMounted(player))
        return false;
    // `useDistanceCheck = 0` so the engine doesn't auto-walk the player
    // toward a corpse just out of range; the server enforces real range.
    auto LootUnit = reinterpret_cast<LootUnit_t>(Offsets::FUN_CMSG_LOOT_UNIT);
    LootUnit(player, target, /*useDistanceCheck=*/0);
    // The engine holds the GUID only on the path that sent.
    return HeldTarget(player) == guid;
}

void ReleaseIfUnanswered(void *player, uint64_t guid) {
    if (HeldTarget(player) != guid)
        return;
    // Mirrors the release response (`0x005EC090`) exactly: clear the
    // target, end the kneel, end the loot.
    *reinterpret_cast<uint64_t *>(static_cast<uint8_t *>(player) +
                                  Offsets::OFF_PLAYER_LOOT_TARGET_GUID) = 0;
    auto UpdateBaseAnimation = reinterpret_cast<UpdateBaseAnimation_t>(
        Offsets::FUN_UNIT_UPDATE_BASE_ANIMATION);
    UpdateBaseAnimation(player, 0xFFFFFFFFu);
    auto Close = reinterpret_cast<CloseLootInner_t>(Offsets::FUN_CLOSE_LOOT_INNER);
    Close(0, '\0', '\0');
}

} // namespace Loot::Request
