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

namespace Unit::StandState {

namespace {

// `UnitStandState(unit)` — returns the unit's current standstate as
// the integer encoded in the low byte of `UNIT_BYTES_1`
// (descriptor + 0x210):
//
//   0  STANDING
//   1  SITTING
//   2  SITTING_CHAIR
//   3  SLEEPING
//   4  SITTING_LOW_CHAIR
//   5  SITTING_MEDIUM_CHAIR
//   6  SITTING_HIGH_CHAIR
//   7  DEAD
//   8  KNEELING
//
// Works for any synced unit (player, target, party*, raid*,
// mouseover, etc.) — the field is a broadcast UpdateField, so
// remote units' standstate is current.
//
// Returns 0 (STANDING) for: missing or invalid unit tokens (e.g.
// `partyN` slot empty, `target` with no target), and unit
// descriptors that aren't loaded yet. Matches modern semantics
// where unresolvable units don't error.
int __fastcall Script_UnitStandState(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitStandState(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);
    if (token == nullptr) {
        Game::Lua::PushNumber(L, 0);
        return 1;
    }

    auto *unit = static_cast<const uint8_t *>(Game::ResolveUnitToken(token));
    if (unit == nullptr) {
        Game::Lua::PushNumber(L, 0);
        return 1;
    }
    auto *fields = *reinterpret_cast<const uint8_t *const *>(
        unit + Offsets::OFF_UNIT_DESCRIPTOR);
    if (fields == nullptr) {
        Game::Lua::PushNumber(L, 0);
        return 1;
    }
    const uint32_t bytes1 = *reinterpret_cast<const uint32_t *>(
        fields + Offsets::OFF_UNIT_FIELD_BYTES_1);
    Game::Lua::PushNumber(L, static_cast<double>(bytes1 & 0xFF));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("unit", "UnitToken"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("standState", "number",
                   "0 standing, 1 sitting, 2 and 4 to 6 on a chair, 3 sleeping, 7 dead, "
                   "8 kneeling; 0 when the token names no unit."),
};
const Game::Doc::Function kUnitStandState{
    "Returns the stand state of a unit, such as standing, sitting, or sleeping.", kArgs, kRets,
    "UnitGlobals", true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitStandState", &Script_UnitStandState,
                                      &kUnitStandState);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::StandState
