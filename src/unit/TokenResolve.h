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

namespace Unit::TokenResolve {

// Is `s` a unit token, as opposed to a literal character name?
//
// The engine's resolver (`FUN_TOKEN_TO_GUID`) raises "Unknown unit name: %s"
// for anything that matches no token; the hook on it turns that into "no
// unit", as 4.3.4 and retail do. This resolves the string and reports whether
// the engine had to raise for it (Unit::TokenExtensions::IsKnown).
//
// Answering through the engine's own resolver (rather than matching a token
// list here) means the answer stays correct for every family it knows —
// player / pet / target / mouseover / npc / partyN / raidN / their pet
// variants / the `target` suffix walker, this client's native `0x<guid>`
// literals, and ClassicAPI's own `focus`, `nameplateN` and `markN`, which
// are hooked onto the same function by `unit/TokenExtensions.cpp`. That hook
// also takes the name of you, a group member or one of your pets, as 4.3.4's
// resolver does, so such a name is true here too; any other name is false.
//
// True for a VALID token that currently points at nothing ("target" with no
// target, "party3" while solo) — the question is "is this a token", not "is
// there a unit there".
//
// Needs a live Lua state; returns false without one, and for null/empty
// input.
bool IsUnitToken(const char *s);

} // namespace Unit::TokenResolve
