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

// `ClosestGameObjectPosition(gameObjectID)` → `xPos, yPos, distance` — the
// world position of the nearest game object with the given
// game-object-template ID, and its distance from the player in yards.
// Returns nothing when none is found (the `MayReturnNothing` contract).
//
// The game-object analog of `ClosestUnitPosition`; same shared scan
// (Object::ClosestByEntry), just filtering on the GameObject GUID type
// (`0xF110`) and `TYPEMASK_GAMEOBJECT`. Same retail-vs-vanilla caveat:
// this returns the nearest CURRENTLY-VISIBLE object of that entry rather
// than reading retail's static starting-zone database (which vanilla
// doesn't have).

#include "Game.h"
#include "Offsets.h"
#include "guid/Guid.h"
#include "object/ClosestByEntry.h"

#include <cmath>
#include <cstdint>

namespace GameObject::ClosestPosition {

namespace {

int __fastcall Script_ClosestGameObjectPosition(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: ClosestGameObjectPosition(gameObjectID)");
        return 0;
    }
    const auto entry = static_cast<uint32_t>(Game::Lua::ToNumber(L, 1));
    const Object::ClosestByEntry::Result r = Object::ClosestByEntry::Find(
        Offsets::TYPEMASK_GAMEOBJECT, Guid::Type::GameObject, entry);
    if (!r.found)
        return 0;

    Game::Lua::PushNumber(L, static_cast<double>(r.x));
    Game::Lua::PushNumber(L, static_cast<double>(r.y));
    Game::Lua::PushNumber(L, std::sqrt(static_cast<double>(r.distSq)));
    return 3;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("gameObjectID", "number", "The game object ID to look for."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("xPos", "number", "World X coordinate."),
    Game::Doc::Req("yPos", "number", "World Y coordinate."),
    Game::Doc::Req("distance", "number", "Yards from the player."),
};
const Game::Doc::Function kClosestGameObjectPosition{
    "Returns the position of the nearest visible game object with an ID, or nothing when none "
    "is in view.",
    kArgs, kRets, "UnitGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("ClosestGameObjectPosition",
                                      &Script_ClosestGameObjectPosition,
                                      &kClosestGameObjectPosition);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace GameObject::ClosestPosition
