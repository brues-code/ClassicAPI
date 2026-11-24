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

// `UnitLevel("player")` while addons load.
//
// Stock `Script_UnitLevel` (0x00517FC0) needs the unit's object; without it
// it pushes the level of the token's party/raid record, else 0. The player
// object doesn't exist during addon loading (file scope, ADDON_LOADED,
// VARIABLES_LOADED), so `UnitLevel("player")` is 0 there — while UnitName,
// UnitRace, UnitClass and UnitSex already answer from the engine's
// selected-character record (VAR_SELECTED_CHAR_RECORD).
//
// 3.80.2 answers it from its record: its UnitLevel/UnitEffectiveLevel core
// (0x141B7B320) computes the level when the unit's object resolves; when it
// doesn't and the token is "player" (0x141B7B427, compared with "player" at
// 0x143DED760), it pushes the record's level byte (0x144C74F70, record
// +0x720, copied from the selected character's list entry at Enter World)
// instead of the party/raid fallback other tokens take.
//
// Co-hook: run the engine function; when the token is the literal "player"
// and the player object doesn't exist, replace its answer with the level byte
// of the same record. Once the object exists the engine's answer stands.

#include "Game.h"
#include "Offsets.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Unit::Level {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);
ScriptFn_t g_origUnitLevel = nullptr;

int __fastcall UnitLevel_h(void *L) {
    const int base = Game::Lua::GetTop(L);
    const int n = g_origUnitLevel(L);
    if (!Game::Lua::IsString(L, 1))
        return n;
    const char *token = Game::Lua::ToString(L, 1);
    if (token == nullptr || !Unit::Identity::IsPlayerToken(token))
        return n;
    if (Unit::Identity::PlayerObject() != nullptr)
        return n; // in world: the engine read UNIT_FIELD_LEVEL

    const uint8_t level = Game::Read<uint8_t>(Offsets::VAR_SELECTED_CHAR_RECORD +
                                              Offsets::OFF_SELECTED_CHAR_RECORD_LEVEL);
    Game::Lua::SetTop(L, base);
    Game::Lua::PushNumber(L, static_cast<double>(level));
    return 1;
}

const Game::HookAutoRegister _unitLevelHook{Offsets::FUN_SCRIPT_UNIT_LEVEL,
                                            reinterpret_cast<void *>(&UnitLevel_h),
                                            reinterpret_cast<void **>(&g_origUnitLevel)};

} // namespace

} // namespace Unit::Level
