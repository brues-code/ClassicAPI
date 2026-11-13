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

// `ClosestUnitPosition(creatureID)` → `xPos, yPos, distance` — the world
// position of the nearest creature with the given NPC (creature-template)
// ID, and its distance from the player in yards. Returns nothing when no
// matching creature is found (the modern `MayReturnNothing` contract).
//
// Semantic note vs. retail: modern WoW's `ClosestUnitPosition` reads a
// static client-side spawn database and only works for starting-zone mobs
// (a tutorial/help helper). Vanilla 1.12 has no such database, so we
// implement the useful, 1.12-native reading: the closest creature of that
// entry that is CURRENTLY VISIBLE (in the client's object manager /
// broadcast window). The shared scan lives in Object::ClosestByEntry.

#include "Game.h"
#include "Offsets.h"
#include "guid/Guid.h"
#include "object/ClosestByEntry.h"

#include <cmath>
#include <cstdint>

namespace Unit::ClosestPosition {

namespace {

int __fastcall Script_ClosestUnitPosition(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: ClosestUnitPosition(creatureID)");
        return 0;
    }
    const auto entry = static_cast<uint32_t>(Game::Lua::ToNumber(L, 1));
    const Object::ClosestByEntry::Result r = Object::ClosestByEntry::Find(
        Offsets::TYPEMASK_UNIT, Guid::Type::Creature, entry);
    if (!r.found)
        return 0;

    Game::Lua::PushNumber(L, static_cast<double>(r.x));
    Game::Lua::PushNumber(L, static_cast<double>(r.y));
    Game::Lua::PushNumber(L, std::sqrt(static_cast<double>(r.distSq)));
    return 3;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("creatureID", "number", "The NPC ID to look for."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("xPos", "number", "World X coordinate."),
    Game::Doc::Req("yPos", "number", "World Y coordinate."),
    Game::Doc::Req("distance", "number", "Yards from the player."),
};
const Game::Doc::Function kClosestUnitPosition{
    "Returns the position of the nearest visible creature with an NPC ID, or nothing when none "
    "is in view.",
    kArgs, kRets, "UnitGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("ClosestUnitPosition", &Script_ClosestUnitPosition,
                                      &kClosestUnitPosition);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::ClosestPosition
