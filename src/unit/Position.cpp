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

// `UnitPosition(unit) → positionX, positionY, positionZ, mapID`, as the 3.80.2
// client's Script_UnitPosition (0x141B83360) returns it:
//   - Always four values. A string the resolver does not know is no unit (its
//     resolver 0x141B8E5B0 takes no Lua state and raises nothing).
//   - A unit in view: its position vector in stored order (x north, y west,
//     z up; retail's docs call them positionX/Y/Z), and the map you are on.
//   - A member out of view: its member record's x, y, z (whole yards) and
//     AreaTable's map column for the record's area, nil for an unknown area.
//     1.12's record has no height, so positionZ is 0.
//   - No position: nil, nil, nil, then the map when one was found (3.80.2
//     finds it before its gate and still returns it when the gate hides the
//     position), else nil.
// Its gate (0x141698520) passes you and group members on open-world maps, and
// with the same flag on it rounds x and y to 0.1 yd and returns z as 0. Here
// there is no gate: any unit in view is measured, at full precision, so a map
// is only ever found with a position and no position is four nils.

#include "Game.h"
#include "Offsets.h"
#include "group/MemberStats.h"
#include "object/Resolve.h"
#include "unit/Identity.h"
#include "unit/Position.h"

#include <cstdint>

namespace Unit::PositionApi {

namespace {

int __fastcall Script_UnitPosition(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitPosition(\"unit\")");
        return 0;
    }
    const uint64_t guid = Unit::Identity::GuidForToken(Game::Lua::ToString(L, 1));

    float pos[3] = {};
    int map = -1;
    bool known = Unit::Position::Read(Object::ByGuid(Offsets::TYPEMASK_UNIT, guid), pos);
    if (known)
        map = Game::Read<int>(Offsets::VAR_CURRENT_MAP_ID);
    else
        known = Group::MemberStats::Position(guid, &pos[0], &pos[1], &map);

    for (const float v : pos) {
        if (known)
            Game::Lua::PushNumber(L, static_cast<double>(v));
        else
            Game::Lua::PushNil(L);
    }
    if (map != -1)
        Game::Lua::PushNumber(L, static_cast<double>(map));
    else
        Game::Lua::PushNil(L);
    return 4;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("unit", "UnitToken"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("positionX", "number", nullptr,
                   "World X coordinate (north); nil, with positionY and positionZ, when the position is not "
                   "known."),
    Game::Doc::Opt("positionY", "number", nullptr, "World Y coordinate (west)."),
    Game::Doc::Opt("positionZ", "number", nullptr, "Height; 0 for a group member out of view."),
    Game::Doc::Opt("mapID", "number", nullptr, "The map the unit is on."),
};
const Game::Doc::Function kUnitPosition{
    "Returns the world position of a unit in view or a group member.", kArgs, kRets, "UnitGlobals"};
const Game::Doc::Function kPlayerInfoUnitPosition{
    "Returns the world position of a unit in view or a group member.", kArgs, kRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitPosition", &Script_UnitPosition, &kUnitPosition);
    Game::Lua::RegisterTableFunction("C_PlayerInfo", "UnitPosition", &Script_UnitPosition,
                                     &kPlayerInfoUnitPosition);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::PositionApi
