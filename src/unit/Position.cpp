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

// UnitPosition backport.
//
// `UnitPosition("unit") → posY, posX, posZ, instanceID` — the modern
// (WoD+) world-position accessor, ported to 1.12. The world position of
// any visible unit already lives on its CGObject; we read it through the
// shared `Unit::Position` helper (the object's GetPosition virtual, vtable
// slot 5 — the same path UnitDistanceSquared / UnitInRange use).
//
// Two semantic notes:
//
//  - **Order matches retail.** The engine's C3Vector is {x, y, z} with the
//    standard WoW convention (x = north, y = west, z = up). Retail's
//    UnitPosition returns (posY, posX, posZ) — west, north, up — so we push
//    the engine's y, then x, then z. An addon ported from retail that reads
//    `posY, posX = UnitPosition(u)` gets coordinates labelled exactly as it
//    expects.
//  - **No group restriction.** Retail returns nil for units outside your
//    party/raid (a taint/privacy guard that doesn't exist in 1.12). We can
//    read any *visible* unit, so this is strictly more permissive — closer
//    to SuperWoW's unit-position access than to retail's restricted form.
//    Units the client can't see (out of range, different zone) have no
//    known position → nil.
//
// `instanceID` is the currently-loaded Map.dbc id (VAR_CURRENT_MAP_ID) —
// every visible unit shares the player's instance, so the player's map id
// is the unit's instance id.

#include "Game.h"
#include "Offsets.h"
#include "unit/Position.h"

#include <cstdint>

namespace Unit::PositionApi {

namespace {

// `UnitPosition("unit") → posY, posX, posZ, instanceID`. nil when the unit
// has no known position (recognized-but-absent token, or a visible-object
// miss). Genuinely unrecognized token strings raise a Lua error, same as
// UnitHealth("garbage") — the engine's resolver owns that contract.
int __fastcall Script_UnitPosition(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitPosition(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);

    float pos[3];
    if (!Unit::Position::ReadToken(token, pos)) {
        Game::Lua::PushNil(L);
        return 1;
    }

    Game::Lua::PushNumber(L, static_cast<double>(pos[1])); // posY (world Y, west)
    Game::Lua::PushNumber(L, static_cast<double>(pos[0])); // posX (world X, north)
    Game::Lua::PushNumber(L, static_cast<double>(pos[2])); // posZ (up)
    const int mapId = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_CURRENT_MAP_ID));
    Game::Lua::PushNumber(L, static_cast<double>(mapId));
    return 4;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("unit", "UnitToken"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("posY", "number", nullptr,
                   "World Y coordinate (west); nil, with the rest, when the unit is not in view."),
    Game::Doc::Opt("posX", "number", nullptr, "World X coordinate (north)."),
    Game::Doc::Opt("posZ", "number", nullptr, "Height."),
    Game::Doc::Opt("instanceID", "number", nullptr, "The map you are on."),
};
const Game::Doc::Function kUnitPosition{
    "Returns the world position of any unit in view.", kArgs, kRets, "UnitGlobals"};
const Game::Doc::Function kPlayerInfoUnitPosition{
    "Returns the world position of any unit in view.", kArgs, kRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitPosition", &Script_UnitPosition, &kUnitPosition);
    Game::Lua::RegisterTableFunction("C_PlayerInfo", "UnitPosition", &Script_UnitPosition,
                                     &kPlayerInfoUnitPosition);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::PositionApi
