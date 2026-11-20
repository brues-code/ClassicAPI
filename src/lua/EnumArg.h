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

namespace Game::Lua {

// An enum argument as the later clients read one (3.80.2's FUN_140aa8360 /
// FUN_140aa8650): a number, cut to an integer and then to its low byte, which
// must be at most `maxValue`. So 1.9 reads 1 and 256 reads 0. Nil (or none)
// leaves `out` as it is and succeeds; anything else that is not such a number
// fails.
inline bool ToEnumByte(void *L, int idx, int maxValue, int &out) {
    const int type = Type(L, idx);
    if (type <= TYPE_NIL)
        return true;
    if (type != TYPE_NUMBER)
        return false;
    const double n = ToNumber(L, idx);
    if (!(n > -2147483649.0 && n < 2147483648.0))
        return false;
    const int byte = static_cast<int>(n) & 0xFF;
    if (byte > maxValue)
        return false;
    out = byte;
    return true;
}

} // namespace Game::Lua
