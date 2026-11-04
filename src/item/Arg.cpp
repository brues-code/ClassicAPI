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

#include "item/Arg.h"

#include "Game.h"

#include <cstdlib>
#include <cstring>

namespace Item::Arg {

Resolved Resolve(void *L, int idx) {
    Resolved out{0, 0, nullptr};
    if (Game::Lua::IsNumber(L, idx)) {
        out.itemID = static_cast<int>(Game::Lua::ToNumber(L, idx));
        return out;
    }
    if (Game::Lua::Type(L, idx) != Game::Lua::TYPE_STRING) {
        return out;
    }
    return ResolveString(Game::Lua::ToString(L, idx));
}

Resolved ResolveString(const char *s) {
    Resolved out{0, 0, nullptr};
    if (s == nullptr) {
        return out;
    }
    if (const char *m = std::strstr(s, "item:")) {
        m += 5;
        out.itemID = std::atoi(m);
        // The enchant and random suffix are the 2nd and 3rd colon-separated
        // fields: itemID:enchant:suffix. Walk the colons (stopping at the link
        // boundary) and read the number after each. Bare "item:N" leaves both 0.
        int colons = 0;
        for (const char *p = m; *p != '\0' && *p != '|'; ++p) {
            if (*p != ':')
                continue;
            if (++colons == 1) {
                out.enchant = std::atoi(p + 1);
            } else {
                out.suffix = std::atoi(p + 1);
                break;
            }
        }
        return out;
    }
    // Plain string — try numeric, otherwise expose as a name for
    // callers that support name-based matching.
    const int numeric = std::atoi(s);
    if (numeric > 0) {
        out.itemID = numeric;
    } else {
        out.name = s;
    }
    return out;
}

int ResolveItemID(void *L, int idx) {
    return Resolve(L, idx).itemID;
}

} // namespace Item::Arg
