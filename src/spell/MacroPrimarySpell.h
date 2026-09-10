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

namespace Spell::MacroPrimarySpell {

// Parses the argument(s) immediately after a known function-call
// prefix. `p` points to the first byte past the closing `(` of the
// prefix (so for `CastSpellNoToggle("Shoot")`, `p` would be at the
// `"`). `end` is end-of-line. Returns the resolved spellID, or 0 if
// extraction failed (malformed args, unknown spell, etc.).
using ArgExtractor = int(*)(const char *p, const char *end);

// File-scope static auto-registers a `(prefix, extractor)` pair. Each
// feature module that wants its Lua wrapper to tag macros declares:
//
//   static const Spell::MacroPrimarySpell::PatternAutoRegister _r{
//       "CastSpellNoToggle(", &MyExtractor};
//
// The macro-parser hook walks every line once, checks each registered
// prefix at each position, and on match invokes the extractor to
// resolve a spellID. The resolved ID is written to the macro's
// primary-spell cache field — drives auto-repeat highlight, cooldown
// overlay, etc. in action-bar UIs (pfUI).
struct PatternAutoRegister {
    PatternAutoRegister(const char *prefix, ArgExtractor extract);
    const char *prefix;
    unsigned prefixLen;
    ArgExtractor extract;
    PatternAutoRegister *next;
};

// Post-parse observer. Fires after EVERY engine parse of a macro body —
// create, edit, the world-enter reparse-all and the spellbook-update
// reparse-unresolved passes — with the `MacroEntry *` whose primary-spell
// cache (`+0x564` / `+0x568`) the engine (and the patterns above) just
// rewrote. The observer may not call into Lua: the parse runs from packet
// handlers and world-init paths where the Lua state isn't ready; mark state
// dirty and act on the next WorldTick (what `Macro::ShowTooltip` does).
// Same static-init chain pattern as `PatternAutoRegister`.
using PostParseCallback = void (*)(int macroEntry);

struct PostParseAutoRegister {
    explicit PostParseAutoRegister(PostParseCallback cb);
    PostParseCallback cb;
    PostParseAutoRegister *next;
};

} // namespace Spell::MacroPrimarySpell
