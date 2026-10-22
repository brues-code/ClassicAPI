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

#include "Game.h"
#include "Offsets.h"

// Lua 5.1 table borders on this 5.0 VM — the ONE border search shared by
// `__len` (the transpiled `#`, LuaSyntax) and the `luaL_getn` heal
// (Table::Length). A border is an index `b` with `t[b] ~= nil` and
// `t[b+1] == nil` (0 when `t[1]` is nil). Lua 5.1's `#` returns SOME border,
// found by `luaH_getn`; Find mirrors that search.
//
// Every probe is the engine's own raw integer read, `luaH_getnum` — the
// function `lua_rawgeti` itself resolves through — called on the Table*
// directly. No metamethods (5.1's `#` on tables has none), and no stack
// traffic: the former probe was rawgeti + lua_type + lua_settop, three API
// calls per slot, on every `#t` and every table.insert / getn / unpack.
namespace Table::Border {

using IndexAcceptable_t = const uint8_t *(__fastcall *)(void *L, int idx);
using GetNum_t = const uint8_t *(__fastcall *)(void *t, int key);

// The Table* at a stack or pseudo index, or nullptr when the value there is
// not a table (or the index is past the top).
inline void *TableAt(void *L, int idx) {
    const uint8_t *o = reinterpret_cast<IndexAcceptable_t>(Offsets::LUA_INDEX_ACCEPTABLE)(L, idx);
    if (o == nullptr ||
        *reinterpret_cast<const int *>(o + Offsets::OFF_LUA_TVALUE_TT) != Game::Lua::TYPE_TABLE)
        return nullptr;
    return *reinterpret_cast<void *const *>(o + Offsets::OFF_LUA_TVALUE_VALUE);
}

// True if t[k] is nil (raw).
inline bool SlotIsNil(void *t, int k) {
    const uint8_t *v = reinterpret_cast<GetNum_t>(Offsets::FUN_LUAH_GETNUM)(t, k);
    return *reinterpret_cast<const int *>(v + Offsets::OFF_LUA_TVALUE_TT) == Game::Lua::TYPE_NIL;
}

// A border in [lo, hi): the caller guarantees t[lo] is populated (lo == 0
// stands for the virtual populated slot before the array) and t[hi] is nil.
// O(log(hi - lo)) probes. On a table with holes this is SOME border, not
// necessarily the highest — exactly what 5.1's `#` promises.
inline unsigned Bisect(void *t, unsigned lo, unsigned hi) {
    while (hi - lo > 1) {
        const unsigned m = (lo + hi) / 2;
        if (SlotIsNil(t, static_cast<int>(m)))
            hi = m;
        else
            lo = m;
    }
    return lo;
}

// A border of the whole table — 5.1's `#t`, by luaH_getn's search: a nil in
// the last array slot means a border inside the array part, bisected there;
// otherwise double `j` from the array size until t[j] is nil (unbound_search),
// then bisect between the last populated `i` and `j`. 5.1 also short-cuts an
// empty node part (`node == dummynode`); here that case costs one probe of
// t[sizearray+1], which misses the same way.
inline unsigned Find(void *t) {
    unsigned j = static_cast<unsigned>(
        *reinterpret_cast<const int *>(static_cast<const uint8_t *>(t) +
                                       Offsets::OFF_LUA_TABLE_SIZEARRAY));
    if (j > 0 && SlotIsNil(t, static_cast<int>(j)))
        return Bisect(t, 0, j);
    unsigned i = j;
    j++;
    while (!SlotIsNil(t, static_cast<int>(j))) {
        i = j;
        if (j > 0x7FFFFFFFu / 2) {
            // Doubling would overflow the int index: walk linearly from here,
            // as luaH_getn's unbound search does. Unreachable for any real
            // table (2^30 populated slots).
            i = 1;
            while (!SlotIsNil(t, static_cast<int>(i)))
                i++;
            return i - 1;
        }
        j *= 2;
    }
    return Bisect(t, i, j);
}

} // namespace Table::Border
