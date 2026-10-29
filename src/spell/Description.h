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

namespace Spell::Description {

// Writes `spellRecord`'s description into `out` with its `$s1` / `$d` / ...
// tokens filled in, through the engine's own parser
// (FUN_FORMAT_SPELL_DESCRIPTION) with the arguments the engine's Lua getters
// pass for a player spell: `(record, out, size, 0, 0, 0, 1, 0)`, as
// Script_GetCraftDescription (0x004F7A90) does outside beast training. That
// is 3.3.5's `SpellParserParseText(spell, out, size, 0, 0, 0, 0, 1, 0)`
// without the spellLevel argument 1.12 doesn't have. `out` is "" when the
// record has no description in the client's locale.
void Expand(const uint8_t *spellRecord, char *out, int outSize);

} // namespace Spell::Description
