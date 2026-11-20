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

#include <cstddef>
#include <cstdint>

// The C_EncodingUtil argument readers and errors, as the 3.80.2 client's
// (UIUtil\EncodingUtil.cpp) reads and raises them.
namespace Encoding::Args {

// `C_EncodingUtil` limits: `Constants.EncodingLimits` (3.80.2 registers both
// at 0x140C1EF60).
constexpr std::size_t kDecompressSizeLimit = 0x6400000; // 100 MiB
constexpr int kStackSizeLimit = 100;

// A `stringView` source (3.80.2's reader 0x140B27EE0): a string, or a number
// read as its string, binary-safe. Anything else, nil and a missing argument
// included, raises the usage error. False when it raised.
inline bool Source(void *L, int idx, const char *usage, const uint8_t **data, std::size_t *len) {
    if (Game::Lua::Type(L, idx) <= Game::Lua::TYPE_NIL || !Game::Lua::IsString(L, idx)) {
        Game::Lua::ArgError(L, idx, usage);
        return false;
    }
    *data = reinterpret_cast<const uint8_t *>(Game::Lua::ToString(L, idx));
    *len = Game::Lua::StrLen(L, idx);
    return true;
}

// An optional enum argument (3.80.2's readers 0x140BB8000 / 0x140C215F0 /
// 0x140C218C0): nil or missing keeps `*out`; otherwise a number (a numeric
// string included) truncated to an int, from 0 to `maxValue`. A value outside
// the int range raises the overflow error; any other bad value raises the
// usage error. False when it raised.
inline bool Enum(void *L, int idx, int maxValue, const char *usage, int *out) {
    if (Game::Lua::Type(L, idx) <= Game::Lua::TYPE_NIL)
        return true;
    if (!Game::Lua::IsNumber(L, idx)) {
        Game::Lua::ArgError(L, idx, usage);
        return false;
    }
    const double v = Game::Lua::ToNumber(L, idx);
    if (!(v > -2147483649.0 && v < 2147483648.0)) {
        Game::Lua::Error(L, "integer overflow attempting to store %f", v);
        return false;
    }
    const int value = static_cast<int>(v);
    if (static_cast<unsigned>(value) > static_cast<unsigned>(maxValue)) {
        Game::Lua::ArgError(L, idx, usage);
        return false;
    }
    *out = value;
    return true;
}

// An error a function body reports, as 3.80.2's 0x1405AC990 raises it:
// "<function>(): <message>". 3.80.2 adds a taint line, which 1.12 has no
// counterpart for.
inline void Raise(void *L, const char *function, const char *message) {
    Game::Lua::Error(L, "%s(): %s", function, message);
}

} // namespace Encoding::Args
