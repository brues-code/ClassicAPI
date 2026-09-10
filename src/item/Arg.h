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

namespace Item::Arg {

// Resolved item-arg shape. `itemID` covers number and link inputs;
// `name` is set only when the arg was a string with no recognizable
// link, for callers that support name-based matching
// (e.g. `C_Item.IsEquippedItem`). Callers that only want the itemID
// can ignore `name` and treat `itemID == 0` as "no usable input".
struct Resolved {
    int itemID;
    int suffix; // random-property / suffix ID (3rd `item:` field), 0 if none
    const char *name;
};

// Resolves a Lua arg at 1-based stack `idx` to an item reference.
// Accepts:
//   - number              → itemID
//   - "item:N..." (bare)  → ID parsed after `item:`
//   - "|cff…|Hitem:N…|h…" → ID parsed after the embedded `item:`
//   - "12345" (numeric)   → atoi → itemID
//   - any other string    → returned via `name` for name-match callers
// Returns `{0, nullptr}` for nil, tables, or other unsupported types.
Resolved Resolve(void *L, int idx);

// String half of `Resolve`, for callers with no Lua stack (the
// `#showtooltip` evaluator runs on the world tick). Same three string forms
// and the same precedence: embedded `item:` link, then plain numeric, then
// name. `name` aliases `s`, so keep `s` alive as long as the result.
//
// NOTE the plain-numeric branch: a caller where a bare number means
// something else (a spellID, an equipment slot) must decide that BEFORE
// calling, because this reads `"6948"` as an itemID.
Resolved ResolveString(const char *s);

// Convenience: itemID only, dropping any name info. Equivalent to
// `Resolve(L, idx).itemID`. Suitable for callers that don't support
// name-based matching.
int ResolveItemID(void *L, int idx);

} // namespace Item::Arg
