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

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Unit::Speed {

namespace {

using GetEffectiveSpeed_t = float(__thiscall *)(void *movementInfo, int forceWalk);

const uint8_t *MovementInfoForToken(const char *token) {
    auto *unit = static_cast<const uint8_t *>(Game::ResolveUnitToken(token));
    if (unit == nullptr)
        return nullptr;
    return *reinterpret_cast<const uint8_t *const *>(
        unit + Offsets::OFF_UNIT_MOVEMENT_INFO_PTR);
}

float ReadSpeedField(const uint8_t *movInfo, int offset) {
    return *reinterpret_cast<const float *>(movInfo + offset);
}

float EffectiveSpeed(const uint8_t *movInfo) {
    auto fn = reinterpret_cast<GetEffectiveSpeed_t>(Offsets::FUN_MOVEMENT_GET_EFFECTIVE_SPEED);
    // const_cast: the engine's selector is __thiscall and takes the
    // info block as its `this` pointer. It only reads — no mutation.
    return fn(const_cast<uint8_t *>(movInfo), 0);
}

} // namespace

// `GetUnitSpeed(unit)` — returns `currentSpeed, runSpeed, flightSpeed,
// swimSpeed` (yards/second). Matches modern WoW's signature; 1.12
// doesn't have flying, so `flightSpeed` is always `0`.
//
// - `currentSpeed` — the speed the engine would apply to this frame's
//   movement step. `0` when the unit is stationary; otherwise the
//   walk/run/swim/backward variant the unit is actually moving with,
//   factoring in movement flags. Read via the engine's own selector
//   (`FUN_MOVEMENT_GET_EFFECTIVE_SPEED`).
// - `runSpeed` — raw forward-run speed from MovementInfo. Includes
//   mount / buff / debuff modifiers (the engine maintains this field
//   as the post-modifier value, updated by SMSG_FORCE_RUN_SPEED_CHANGE
//   and friends).
// - `swimSpeed` — raw forward-swim speed, same shape as runSpeed.
//
// All four returns are `0` if the token doesn't resolve to a live
// CGUnit (out-of-range party member, empty `"target"`, etc.) —
// matches 3.3.5's `Script_GetUnitSpeed` behavior of pushing `0.0`
// rather than nil for non-visible units. Invalid token types raise
// a Lua usage error.
static int __fastcall Script_GetUnitSpeed(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: GetUnitSpeed(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);

    float currentSpeed = 0.0f;
    float runSpeed = 0.0f;
    float swimSpeed = 0.0f;
    if (token != nullptr) {
        const uint8_t *movInfo = MovementInfoForToken(token);
        if (movInfo != nullptr) {
            currentSpeed = EffectiveSpeed(movInfo);
            runSpeed = ReadSpeedField(movInfo, Offsets::OFF_MOVEMENT_RUN_SPEED);
            swimSpeed = ReadSpeedField(movInfo, Offsets::OFF_MOVEMENT_SWIM_SPEED);
        }
    }

    Game::Lua::PushNumber(L, static_cast<double>(currentSpeed));
    Game::Lua::PushNumber(L, static_cast<double>(runSpeed));
    Game::Lua::PushNumber(L, 0.0); // flightSpeed — no flying in 1.12
    Game::Lua::PushNumber(L, static_cast<double>(swimSpeed));
    return 4;
}

static const Game::Doc::Field kGetUnitSpeedArgs[] = {
    Game::Doc::Req("unit", "UnitToken"),
};
static const Game::Doc::Field kGetUnitSpeedRets[] = {
    Game::Doc::Req("currentSpeed", "number",
                   "Speed of the current movement; 0 when standing still or not in view."),
    Game::Doc::Req("runSpeed", "number", "Forward run speed with all modifiers."),
    Game::Doc::Req("flightSpeed", "number", "Always 0."),
    Game::Doc::Req("swimSpeed", "number", "Forward swim speed."),
};
static const Game::Doc::Function kGetUnitSpeed{
    "Returns a unit's movement speeds in yards per second.", kGetUnitSpeedArgs,
    kGetUnitSpeedRets, "UnitGlobals"};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetUnitSpeed", &Script_GetUnitSpeed, &kGetUnitSpeed);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Unit::Speed
