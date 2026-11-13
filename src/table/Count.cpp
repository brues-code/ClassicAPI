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

#include "Game.h"

#include <cmath>
#include <vector>

namespace Table::Count {

// `numTableNodes, numArrayNodes, maxArrayIndex = table.count(tbl)` — a modern
// WoW diagnostic (added retail 11.2.5) reporting how a table is populated.
// These are COUNTS of live entries, not the table's allocated capacity:
//
//   numTableNodes  total number of key/value pairs.
//   numArrayNodes  count of pairs whose key is an integer in [1..numTableNodes]
//                  (the "looks like a contiguous array part" heuristic).
//   maxArrayIndex  the largest positive integral key (>= 1), or 0 if none.
//                  Verified against retail: negative/zero integral keys do
//                  NOT count ({[-3]=x} -> 1, 0, 0).
//
// Pure iteration — no Lua-internal struct layout needed. Note we only ever
// read a key via ToNumber, never ToString: lua_tostring on a NUMERIC key
// rewrites it to a string in place and corrupts lua_next's traversal.
static int __fastcall Script_table_count(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: table.count(table)");
        return 0;
    }

    std::vector<double> integralKeys;
    int total = 0;
    double maxKey = 0.0; // largest positive integral key seen; 0 = none

    Game::Lua::PushNil(L);
    while (Game::Lua::Next(L, 1) != 0) {
        ++total;
        // key at -2, value at -1.
        if (Game::Lua::Type(L, -2) == Game::Lua::TYPE_NUMBER) {
            const double k = Game::Lua::ToNumber(L, -2);
            if (k == std::floor(k)) { // integral key
                integralKeys.push_back(k);
                if (k >= 1.0 && k > maxKey) // only positive keys count
                    maxKey = k;
            }
        }
        Game::Lua::SetTop(L, -2); // pop value, keep key for the next Next()
    }

    int numArray = 0;
    for (double k : integralKeys)
        if (k >= 1.0 && k <= static_cast<double>(total))
            ++numArray;

    Game::Lua::PushNumber(L, static_cast<double>(total));
    Game::Lua::PushNumber(L, static_cast<double>(numArray));
    Game::Lua::PushNumber(L, maxKey);
    return 3;
}

static const Game::Doc::Field kCountArgs[] = {
    Game::Doc::Req("tbl", "table"),
};
static const Game::Doc::Field kCountRets[] = {
    Game::Doc::Req("numTableNodes", "number", "The number of key/value pairs."),
    Game::Doc::Req("numArrayNodes", "number",
                   "How many keys are whole numbers from 1 to numTableNodes."),
    Game::Doc::Req("maxArrayIndex", "number", "The largest positive whole-number key, or 0."),
};
static const Game::Doc::Function kCount{"Returns counts of the entries in a table.", kCountArgs,
                                        kCountRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("table", "count", &Script_table_count, &kCount);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Table::Count
