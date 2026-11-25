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

#include "pvp/Context.h"

#include "Game.h"
#include "Offsets.h"
#include "map/Instance.h"
#include "object/Resolve.h"

#include <cstdint>

namespace Pvp::Context {

namespace {

using ControllingPlayer_t = void *(__fastcall *)(void *unit);

uint32_t UnitFlags(const void *unit) {
    const uint8_t *f = Object::Fields(unit);
    return f ? Game::Read<uint32_t>(f, Offsets::OFF_UNIT_FIELD_FLAGS) : 0;
}

bool IsPlayerObject(const void *unit) {
    return Game::Read<int>(unit, Offsets::OFF_CGOBJECT_TYPE_ID) == Offsets::OBJECT_TYPE_PLAYER;
}

// A player's block (PLAYER_FLAGS, the duel fields); only a player has one.
const uint8_t *PlayerInfo(const void *player) {
    return IsPlayerObject(player) ? Game::Read<const uint8_t *>(player, Offsets::OFF_CGPLAYER_INFO)
                                  : nullptr;
}

uint32_t PlayerFlags(const void *player) {
    const uint8_t *info = PlayerInfo(player);
    return info ? Game::Read<uint32_t>(info, Offsets::OFF_PLAYER_INFO_FLAGS) : 0;
}

int DuelTeam(const void *player) {
    const uint8_t *info = PlayerInfo(player);
    return info ? Game::Read<int>(info, Offsets::OFF_PLAYER_INFO_DUEL_TEAM) : 0;
}

uint64_t DuelArbiter(const void *player) {
    const uint8_t *info = PlayerInfo(player);
    return info ? Game::Read<uint64_t>(info, Offsets::OFF_PLAYER_INFO_DUEL_ARBITER) : 0;
}

// FUN_1414ab560: the two player-controlled units' players duel each other
// (one arbiter, two teams).
bool Dueling(void *a, void *b) {
    if ((UnitFlags(a) & Offsets::UNIT_FLAG_PLAYER_CONTROLLED) == 0 ||
        (UnitFlags(b) & Offsets::UNIT_FLAG_PLAYER_CONTROLLED) == 0)
        return false;
    const auto controller = reinterpret_cast<ControllingPlayer_t>(Offsets::FUN_UNIT_CONTROLLING_PLAYER);
    void *pa = controller(a);
    void *pb = controller(b);
    if (pa == nullptr || pb == nullptr)
        return false;
    const int ta = DuelTeam(pa), tb = DuelTeam(pb);
    return ta != 0 && tb != 0 && ta != tb && DuelArbiter(pa) == DuelArbiter(pb);
}

} // namespace

bool PvpContext(const void *player) {
    return DuelTeam(player) != 0 || (PlayerFlags(player) & Offsets::PLAYER_FLAG_FFA_PVP) != 0 ||
           Map::Instance::CurrentType() == Offsets::MAP_INSTANCE_TYPE_BATTLEGROUND;
}

bool PvpUnit(void *player, void *unit) {
    const uint32_t flags = UnitFlags(unit);
    if (!IsPlayerObject(unit))
        return (flags & Offsets::UNIT_FLAG_PLAYER_CONTROLLED) != 0 &&
               (flags & Offsets::UNIT_FLAG_PVP) != 0;
    if ((flags & Offsets::UNIT_FLAG_PVP) != 0 ||
        (PlayerFlags(unit) & (Offsets::PLAYER_FLAG_FFA_PVP | Offsets::PLAYER_FLAG_PVP_DESIRED)) != 0)
        return true;
    return unit != player && DuelTeam(unit) != 0 && Dueling(player, unit);
}

} // namespace Pvp::Context
