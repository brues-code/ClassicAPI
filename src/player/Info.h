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

// Shared GUID <-> name resolution, factored out of `UnitNameFromGUID` /
// `C_PlayerCache.GetPlayerInfoByName` so other modules (e.g. `Spell::Cast`'s
// `UnitSpellTargetName`) can convert without a Lua round-trip.

#include <cstddef>
#include <cstdint>

namespace Player::Info {

// Resolve any object GUID to a display name -- players AND creatures. Chain:
// object manager (the engine's polymorphic name getter) -> friends list ->
// persistent NameCache. Writes a NUL-terminated name into `buf` and returns
// true iff resolved; returns false (buf untouched) for GUID 0, a non-unit
// object, or an unknown / uncached GUID. Passive -- never triggers a name
// query.
bool NameFromGuid(uint64_t guid, char *buf, size_t bufSize);

// Resolve a player's character name to their GUID, or 0. Chain: the engine's
// by-name search (party and raid members, including out-of-range ones, then
// visible players; whole name, case-insensitive) -> friends list ->
// persistent NameCache. Players only, never creatures. Passive -- never
// triggers a name query.
uint64_t GuidFromName(const char *name);

} // namespace Player::Info
