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

#pragma once

#include <cstdint>

// The names a unit token may be written as: you, your group, and their pets.
//
// 4.3.4's token resolver (Script_GetGUIDFromToken, 0x0044F430) falls back to
// Script_GetGUIDFromString (0x0044EE40) for a string that is no token. That
// compares the string, whole and case-insensitive, with the name behind
// `player`, then `raidN` in a raid or else `partyN`, then `pet`, then
// `raidpetN` or else `partypetN`, and takes the first match. This module keeps
// those names, so the hook on the 1.12 resolver (unit/TokenExtensions.cpp)
// can make the same comparison without walking the group on every call.
namespace Unit::TokenNames {

// The 1.12 resolver below every hook of ours: the slots' GUIDs come from it,
// so a pet is the unit the engine's own `pet` / `partypetN` / `raidpetN`
// names, and building the list never re-enters the hook.
using EngineResolve = uint64_t(__fastcall *)(const char *token);

struct Match {
    uint64_t guid;
    // The engine token naming this unit's pet ("pet", "partypet2",
    // "raidpet7"), or nullptr for a unit with no pet token (a pet itself).
    const char *petToken;
};

// The unit `name[0..len)` names, compared like Script_GetGUIDFromString.
// False when no slot has that name.
bool Find(const char *name, int len, EngineResolve resolve, Match *out);

// The pet token of a group slot's unit, for a `-pet` hop off a unit reached
// some other way (`Name-target-pet`). Nullptr when `guid` holds no member slot.
const char *PetTokenFor(uint64_t guid, EngineResolve resolve);

} // namespace Unit::TokenNames
