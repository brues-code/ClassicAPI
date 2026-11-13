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

namespace Unit::Sheath {

// `GetSheathState()` — returns which weapon type the local player currently
// has drawn. Takes no arguments. The return is **1-based**, matching modern
// WoW's contract:
//   1 = None   (weapons sheathed / put away)
//   2 = Melee  (melee weapon drawn)
//   3 = Ranged (ranged weapon drawn)
//
// Vanilla 1.12 ships `ToggleSheath` but no getter. The engine keeps the
// sheath state as a plain CGUnit int member at OFF_UNIT_SHEATH_STATE (+0xD40),
// 0-based (0 = sheathed, 1 = melee, 2 = ranged) — the same field the engine's
// own toggler `FUN_005EB480` reads to pick the next state and its setter
// `FUN_00611CF0` writes. We read the local player's value and add 1 for the
// Lua contract.
//
// Returns 1 (None) when the player CGUnit isn't resolvable yet (out of
// world) — the "weapons put away" default. `FUN_RESOLVE_UNIT_TOKEN` returns
// null for `"player"` out of world rather than raising, so this is safe from
// any context.
static int __fastcall Script_GetSheathState(void *L) {
    auto *player = static_cast<const uint8_t *>(Game::ResolveUnitToken("player"));

    int state = 0;
    if (player != nullptr)
        state = Game::Read<int>(player, Offsets::OFF_UNIT_SHEATH_STATE);
    if (state < 0 || state > 2)
        state = 0; // guard against any unexpected value (field is 0/1/2)

    Game::Lua::PushNumber(L, static_cast<double>(state + 1));
    return 1;
}

static const Game::Doc::Field kGetSheathStateRets[] = {
    Game::Doc::Req("result", "number",
                   "1 for weapons put away, 2 for a melee weapon drawn, 3 for a ranged weapon "
                   "drawn."),
};
static const Game::Doc::Function kGetSheathState{
    "Returns which weapon you have drawn.", {}, kGetSheathStateRets, "PlayerGlobals"};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetSheathState", &Script_GetSheathState,
                                      &kGetSheathState);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Unit::Sheath
