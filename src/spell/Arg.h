// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

#pragma once

namespace Spell::Arg {

// Resolved spell-arg shape, modeled after `Item::Arg::Resolved`.
// `spellID` covers number and link inputs; `name` is set only when
// the arg was a string with no recognizable link, for callers that
// support name-based matching (`C_Spell.GetSpellCooldown` etc. run
// the name through the engine's spellbook resolver).
struct Resolved {
    int spellID;
    const char *name;
};

// Resolves a Lua arg at 1-based stack `idx` to a spell reference.
// Accepts:
//   - number              → spellID
//   - "|c…|Hspell:N…|h…"  → ID parsed after the embedded `spell:`
//   - "spell:N" (bare)    → ID parsed after `spell:`
//   - "12345" (numeric)   → atoi → spellID
//   - any other string    → returned via `name` for name-match callers
// Returns `{0, nullptr}` for nil, tables, or other unsupported types.
Resolved Resolve(void *L, int idx);

// Convenience: number / link inputs return the parsed spellID
// directly; name inputs run through the engine's
// `FUN_RESOLVE_SPELL_NAME_TO_BOOK_ID` (respects `(Rank N)` suffixes
// via `FUN_004B3950`'s parser, bounded to the player's known
// spellbook). Returns 0 on any failure.
int ResolveSpellID(void *L, int idx);

// Standalone name → spellID resolver — same engine helper
// `ResolveSpellID` uses for its name-input branch. Exposed for
// callers that already have a name string (macro-body scanners,
// etc.). Returns 0 if `name` isn't in the player's spellbook.
// `outIsPet`, when given, is set to 1 when the name matched in the pet
// spellbook (the engine's own answer), else 0.
int NameToSpellID(const char *name, int *outIsPet = nullptr);

} // namespace Spell::Arg
