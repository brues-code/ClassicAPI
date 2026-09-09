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

// Spellbook TABS — the engine's per-skill-line partition of the player
// spellbook array (what `GetNumSpellTabs` / `GetSpellTabInfo` read). Layout
// notes at `Offsets::VAR_SPELL_TAB_ENTRIES`.
namespace Spell::Tabs {

// Live tab count (what `GetNumSpellTabs()` returns). 0 before login.
int Count();

// 1-based index of the tab holding 1-based PLAYER-book slot `slot1`, or 0
// when the slot is past the populated range. Pet-book slots have no tab.
int IndexForSlot(int slot1);

// 1-based index of the tab whose SkillLine.dbc ID is `skillLineID`, or 0
// when the player has no tab for that skill line. `skillLineID <= 0` is
// always 0 — the General tab stores ID 0 and is not addressable by ID.
int IndexForSkillLine(int skillLineID);

} // namespace Spell::Tabs
