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

// `UnitCreatureFamilyID(unit)` — the numeric CreatureFamily id (the
// `CreatureFamily.dbc` row: 1 = Wolf, 3 = Cat, 4 = Bear, …) for a unit,
// or `nil` when the unit has no family.
//
// The stock `UnitCreatureFamily(unit)` returns only the *localized name*
// ("Wolf", "Chat"), which is awkward to compare and locale-dependent. This
// extension exposes the raw id addons actually want for stable checks
// (tameable-pet logic, family-specific ability tables, etc.) — the same
// value that indexes `CreatureFamily.dbc`.
//
// This is exactly the id the engine's `Script_UnitCreatureFamily`
// (`0x0051A310`) reads before its DBC name lookup: its family helper
// `FUN_006055e0` returns `[unit + 0xB30] ? [row + 0x1C] : 0`, then the id is
// bounds-checked against `CreatureFamily.dbc` and used to push the localized
// name. We stop at the id. `nil` for:
//   - Unresolvable unit token (`target` with nothing targeted, empty
//     `partyN` slot, etc.).
//   - Players and non-beast NPCs — family id is 0 (`UnitCreatureFamily`
//     returns nil for these too).
//   - NPCs whose creature data hasn't synced yet (no +0xB30 row).
//
// Raises a Lua error on a missing / non-string `unit` argument — same
// semantics as `UnitCreatureFamily` / `UnitSubName`.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Unit::CreatureFamily {

namespace {

int __fastcall Script_UnitCreatureFamilyID(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitCreatureFamilyID(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);
    if (token == nullptr)
        return 0;

    auto *unit = static_cast<const uint8_t *>(Game::ResolveUnitToken(token));
    if (unit == nullptr)
        return 0;

    auto *cacheRow = *reinterpret_cast<const uint8_t *const *>(
        unit + Offsets::OFF_UNIT_CREATURE_CACHE_ROW);
    if (cacheRow == nullptr)
        return 0; // unsynced NPC / player unit

    const int familyID = *reinterpret_cast<const int *>(
        cacheRow + Offsets::OFF_CREATURE_CACHE_FAMILY);
    if (familyID <= 0)
        return 0; // no family (players, non-beast NPCs)

    Game::Lua::PushNumber(L, static_cast<double>(familyID));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("unit", "UnitToken"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("creatureFamilyID", "number", nullptr,
                   "Nil for players, units with no family, and NPCs whose data has not loaded."),
};
const Game::Doc::Function kUnitCreatureFamilyID{
    "Returns the creature family ID of a unit, such as 1 for Wolf.", kArgs, kRets,
    "UnitGlobals", true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitCreatureFamilyID", &Script_UnitCreatureFamilyID,
                                      &kUnitCreatureFamilyID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::CreatureFamily
