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

// The set of spells that belong to talents, derived once per session from
// Talent.dbc (static data). Shared by `C_SpellBook.GetCurrentLevelSpells`
// (skips talents) and `C_SpellBook.IsClassTalentSpellBookItem`.
namespace Talent::SpellSet {

// True iff `spellID` appears in a Talent.dbc rank column — the spell a
// talent point grants directly (any rank of a passive, or rank 1 of an
// active talent ability such as Mortal Strike 12294).
bool IsTalentRank(int spellID);

// `IsTalentRank` extended to the trained higher ranks of a talent ability
// (Mortal Strike 21551, 21552, ...), which a spellbook lists next to the
// talent-granted rank 1. Two sources: SkillLineAbility's next-rank links
// (see `Offsets::OFF_SLA_SUPERCEDED_BY_SPELL`), and — for chains without
// links, like Mind Flay — a shared localized name with a talent rank
// spell, the engine's own name-cast notion of a rank set. Meant for spells
// in the player's spellbook; a name match there is a rank of that talent.
bool IsTalentLine(int spellID);

} // namespace Talent::SpellSet
