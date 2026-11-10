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

// Spell duration in milliseconds, shared by the `GetSpellDuration` /
// `C_Spell.GetSpellDuration` bindings and `Aura::Data`'s `duration`
// fallback.

namespace Spell::Duration {

// The server's pre-modifier duration: the SpellDuration.dbc row's base
// value, with a negative base taken as its absolute value. 0 for an
// unknown spell, a missing row, or an until-cancelled aura (base -1).
// Ignores level scaling, combo points and caster modifiers, matching
// vmangos and tortoise-wow (`SpellEntry::GetDuration`). Combo-point
// finishers therefore report their base, not the 5-point maximum — use
// `AuraData.duration` for the duration an actual aura was applied with.
uint32_t BaseMs(uint32_t spellID);

} // namespace Spell::Duration
