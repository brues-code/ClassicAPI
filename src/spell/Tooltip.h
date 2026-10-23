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

namespace Spell::Tooltip {

// Resolves the `GameTooltip` self at Lua stack[1] and runs the engine's
// spell-tooltip builder for `spellID` with the rank shown in the top-right
// corner of the name line (as `SetAction` shows it) and no cooldown line.
// Silent no-op if `spellID <= 0` or stack[1] isn't a tooltip.
//
// Used by `GameTooltip:SetTalentByID` as a fallback for cross-class talents
// (where the engine's per-player TalentEntry tables don't apply, so we
// render the rank-1 spell tooltip instead) and by `spell:` hyperlinks.
// `GameTooltip:SetSpellByID` has its own 4.3.4-shaped arguments.
void ShowByID(void *L, int spellID);

} // namespace Spell::Tooltip
