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

// `UnitSubName(unit)` — returns the NPC's subname / title (the small
// italic text under their name in the tooltip): "Innkeeper",
// "Stable Master", "<Master Engineer>", etc. Modern WoW exposes this
// via `UnitName`'s second return; vanilla 1.12's `UnitName` returns
// only one value, so addons that want the subtitle have to roll their
// own — until now.
//
// Returns `nil` for:
//   - Unresolvable unit token (`target` with nothing targeted, empty
//     `partyN` slot, etc.).
//   - Player units — they don't have a row in the creature cache.
//   - NPCs whose creature data hasn't synced yet (rare; happens when
//     the unit comes into view but the SMSG_CREATURE_QUERY_RESPONSE
//     hasn't arrived).
//   - NPCs with no subname (most non-vendor / non-trainer creatures).
//
// Raises a Lua error on missing / non-string `unit` argument — same
// semantics as the engine's `UnitName`.
//
// Implementation: walks the creature cache row at `[unit + 0xB30]` →
// `[+0x10]` (subname C string). Verified path; same one
// VanillaMinimapTracking's blip subname filter uses.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Unit::SubName {

namespace {

int __fastcall Script_UnitSubName(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitSubName(\"unit\")");
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
        return 0;

    auto *subName = *reinterpret_cast<const char *const *>(
        cacheRow + Offsets::OFF_CREATURE_CACHE_SUB_NAME);
    if (subName == nullptr || *subName == '\0')
        return 0;

    Game::Lua::PushString(L, subName);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("unit", "UnitToken"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("subName", "string", nullptr,
                   "Nil for players, NPCs with no subtitle, and NPCs whose data has not loaded."),
};
const Game::Doc::Function kUnitSubName{
    "Returns the subtitle shown under an NPC's name, such as \"Innkeeper\".", kArgs, kRets,
    "UnitGlobals", true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitSubName", &Script_UnitSubName, &kUnitSubName);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::SubName
