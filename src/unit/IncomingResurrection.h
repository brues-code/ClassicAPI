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

// 4.3.4's CGIncomingResurrection: the resurrections being cast at each unit
// or corpse. Backs `UnitHasIncomingResurrection` and
// `INCOMING_RESURRECT_CHANGED`.
namespace Unit::IncomingResurrection {

// `caster` stopped casting `spellID`: CGUnit_C::ClearCastingSpell or a
// failure packet, both owned by `Spell::Cast`. Ends the resurrection it was
// casting, if that is one.
void OnCastEnded(uint64_t caster, int spellID);

} // namespace Unit::IncomingResurrection
