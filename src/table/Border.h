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

#include "Game.h"
#include "Offsets.h"

// Lua 5.1 table borders on this 5.0 VM — the ONE border search shared by
// `__len` (the transpiled `#`, LuaSyntax) and the `luaL_getn` heal
// (Table::Length). A border is an index `b` with `t[b] ~= nil` and
// `t[b+1] == nil` (0 when `t[1]` is nil). Lua 5.1's `#` returns SOME border,
// found by `luaH_getn`'s doubling-then-bisect search; these helpers return
// the answer that search would. Every probe is a raw `lua_rawgeti` — no
// metamethods, matching 5.1's `#` on tables.
namespace Table::Border {

using RawGetI_t = void(__fastcall *)(void *L, int idx, int n);

// True if t[k] is nil, with `t` at the ABSOLUTE stack index `absIdx` (or a
// pseudo-index). Balances the stack.
inline bool SlotIsNil(void *L, int absIdx, int k) {
    reinterpret_cast<RawGetI_t>(Offsets::LUA_RAWGETI)(L, absIdx, k);
    const bool isNil = Game::Lua::Type(L, -1) == Game::Lua::TYPE_NIL;
    Game::Lua::SetTop(L, -2);
    return isNil;
}

// A border in [lo, hi): the caller guarantees t[lo] is populated (lo == 0
// stands for the virtual populated slot before the array) and t[hi] is nil.
// O(log(hi - lo)) probes. On a table with holes this is SOME border, not
// necessarily the highest — exactly what 5.1's `#` promises.
inline unsigned Bisect(void *L, int absIdx, unsigned lo, unsigned hi) {
    while (hi - lo > 1) {
        const unsigned m = (lo + hi) / 2;
        if (SlotIsNil(L, absIdx, static_cast<int>(m)))
            hi = m;
        else
            lo = m;
    }
    return lo;
}

// A border of the whole table — 5.1's `#t`: double `j` until t[j] is nil,
// then bisect between the last populated `i` and `j`.
inline unsigned Find(void *L, int absIdx) {
    unsigned i = 0, j = 1;
    while (!SlotIsNil(L, absIdx, static_cast<int>(j))) {
        i = j;
        if (j > 0x7FFFFFFFu / 2) {
            // Doubling would overflow the int index: walk linearly from here,
            // as luaH_getn's unbound search does. Unreachable for any real
            // table (2^30 populated slots).
            i = 1;
            while (!SlotIsNil(L, absIdx, static_cast<int>(i)))
                i++;
            return i - 1;
        }
        j *= 2;
    }
    return Bisect(L, absIdx, i, j);
}

} // namespace Table::Border
