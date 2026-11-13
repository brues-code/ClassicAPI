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

// `UnitCreatureTypeID(unit)` — the numeric CreatureType id (the
// `CreatureType.dbc` row: 1 = Beast, 3 = Demon, 7 = Humanoid, …) for a
// unit, or `nil` when it has none.
//
// The stock `UnitCreatureType(unit)` returns only the *localized name*
// ("Humanoid", "Humanoïde"), which is awkward to compare and locale-
// dependent. This extension exposes the raw id — the twin of
// `UnitCreatureFamilyID`.
//
// It's exactly the value the engine's `Script_UnitCreatureType`
// (`0x0051A280`) resolves before its DBC name lookup, so we just call that
// same inner helper (`FUN_UNIT_CREATURE_TYPE`). The helper handles all three
// engine paths itself — a display-override, the creature-cache type, and the
// player-race type (players resolve to Humanoid via their race) — so a
// straight call matches `UnitCreatureType` for every unit kind. `nil` for:
//   - Unresolvable unit token (`target` with nothing targeted, etc.).
//   - A unit with no resolvable type (helper returns 0).
//
// Raises a Lua error on a missing / non-string `unit` argument — same
// semantics as `UnitCreatureType` / `UnitCreatureFamilyID`.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Unit::CreatureType {

namespace {

using CreatureType_t = int(__fastcall *)(void *unit);

int __fastcall Script_UnitCreatureTypeID(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitCreatureTypeID(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);
    if (token == nullptr)
        return 0;

    void *unit = Game::ResolveUnitToken(token);
    if (unit == nullptr)
        return 0;

    auto creatureType = reinterpret_cast<CreatureType_t>(
        Offsets::FUN_UNIT_CREATURE_TYPE);
    const int typeID = creatureType(unit);
    if (typeID <= 0)
        return 0; // no resolvable type

    Game::Lua::PushNumber(L, static_cast<double>(typeID));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("unit", "UnitToken"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("creatureTypeID", "number", nullptr,
                   "Players are 7 (Humanoid); nil when the unit has no type."),
};
const Game::Doc::Function kUnitCreatureTypeID{
    "Returns the creature type ID of a unit, such as 1 for Beast.", kArgs, kRets,
    "UnitGlobals", true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitCreatureTypeID", &Script_UnitCreatureTypeID,
                                      &kUnitCreatureTypeID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::CreatureType
