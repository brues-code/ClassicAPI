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

// `UnitFactionGroup("player")` while addons load.
//
// The engine's `Script_UnitFactionGroup` (0x00516630) resolves "player"
// through the in-world unit descriptor, which does not exist until the player
// object spawns. So during addon loading — file scope, ADDON_LOADED,
// VARIABLES_LOADED — it returns nil, and it only starts answering at
// PLAYER_LOGIN. UnitName, UnitRace and UnitClass already answer at that point
// (UnitRace/UnitClass special-case "player" to the login-time race/class byte
// globals, see VAR_PLAYER_RACE_BYTE), and later clients answer
// UnitFactionGroup there too, so ported addons read it at file scope:
// AceDB-3.0 builds its "<faction> - <realm>" profile key from it and fails to
// load on nil.
//
// Co-hook: run the engine function; when it answers nothing for the literal
// "player" token, fall back to the login-time race byte and the same
// race -> faction-group chain the engine's player branch uses
// (Creature::Info::FactionGroupForRace). Once the player object exists the
// engine answers by itself and this passes straight through, so in-world
// results are unchanged.

#include "Game.h"
#include "Offsets.h"
#include "creature/Info.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Unit::FactionGroup {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);
ScriptFn_t g_origUnitFactionGroup = nullptr;

int __fastcall UnitFactionGroup_h(void *L) {
    const int base = Game::Lua::GetTop(L);
    const int n = g_origUnitFactionGroup(L);
    if (n > 0 && Game::Lua::Type(L, -n) != Game::Lua::TYPE_NIL)
        return n; // the engine answered
    if (!Game::Lua::IsString(L, 1))
        return n;
    const char *token = Game::Lua::ToString(L, 1);
    if (token == nullptr || !Unit::Identity::IsPlayerToken(token))
        return n;

    const uint8_t race = Game::Read<uint8_t>(Offsets::VAR_PLAYER_RACE_BYTE);
    const char *english = nullptr;
    const char *localized = nullptr;
    if (race == 0 ||
        !Creature::Info::FactionGroupForRace(race, &english, &localized))
        return n;

    Game::Lua::SetTop(L, base);
    Game::Lua::PushString(L, english);
    Game::Lua::PushString(L, localized);
    return 2;
}

const Game::HookAutoRegister _unitFactionGroupHook{
    Offsets::FUN_SCRIPT_UNIT_FACTION_GROUP,
    reinterpret_cast<void *>(&UnitFactionGroup_h),
    reinterpret_cast<void **>(&g_origUnitFactionGroup)};

} // namespace

} // namespace Unit::FactionGroup
