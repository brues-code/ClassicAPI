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

// `GetCritChance()` / `GetRangedCritChance()` — the server-computed melee and
// ranged crit percentages. Same shape as the engine's own GetDodgeChance
// (0x00516F00) and 4.3.4's Script_GetCritChance: read the float from the
// CGPlayer sub-struct, 0 without a player.

#include "Game.h"
#include "Offsets.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Player::CritChance {

namespace {

int PushPercent(void *L, uint32_t field) {
    const uint8_t *player = Unit::Identity::PlayerObject();
    float value = 0.0f;
    if (player != nullptr) {
        const uint8_t *info = Game::Read<const uint8_t *>(player, Offsets::OFF_CGPLAYER_INFO);
        value = Game::Read<float>(info, field);
    }
    Game::Lua::PushNumber(L, value);
    return 1;
}

int __fastcall Script_GetCritChance(void *L) {
    return PushPercent(L, Offsets::OFF_PLAYER_FIELD_CRIT_PERCENTAGE);
}

int __fastcall Script_GetRangedCritChance(void *L) {
    return PushPercent(L, Offsets::OFF_PLAYER_FIELD_RANGED_CRIT_PERCENTAGE);
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kChanceRets[] = {
    Game::Doc::Req("critChance", "number", "The chance as a percent, for example 12.5."),
};
const Game::Doc::Function kGetCritChance{
    "Returns your chance to get a critical strike with melee attacks.",
    {}, kChanceRets, "PaperDollInfo"};
const Game::Doc::Function kGetRangedCritChance{
    "Returns your chance to get a critical strike with ranged attacks.",
    {}, kChanceRets, "PaperDollInfo"};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetCritChance", &Script_GetCritChance,
                                      &kGetCritChance);
    Game::Lua::RegisterGlobalFunction("GetRangedCritChance", &Script_GetRangedCritChance,
                                      &kGetRangedCritChance);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Player::CritChance
