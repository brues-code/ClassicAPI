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

// C++ entry to the cast-at-unit primitive behind `C_Spell.CastAtUnit`, so
// other C++ modules (the Frame attribute click dispatcher's `spell` verb) can
// cast a spell at a unit without a Lua round-trip through the global table.
//
// Both feed the unit's GUID straight to the engine's cast dispatcher — a
// unit-target/normal spell fires directly on the unit (no current-target
// juggling), a ground-target spell lands at the unit's feet. Returns true when
// the spell was cast. Safe to call from inside a Lua C function: a genuinely
// unknown token raises the engine's standard "Unknown unit" error in the
// resolver, caught by the enclosing protected call.

namespace Spell::AtUnit {

// Cast by name ("(Rank N)"-aware — `"Blizzard"` is the highest known rank).
bool CastByName(const char *spellName, const char *unitToken);

// Cast the exact numeric spellID (a specific rank).
bool CastByID(int spellID, const char *unitToken);

} // namespace Spell::AtUnit
