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

// Storm's string functions the engine's parsers use.

#include "Offsets.h"

#include <cstdint>

namespace Storm {

// SStrTokenize (FUN_STORM_STR_TOKENIZE, `__stdcall`, RET 0x14): skips leading
// characters of `delims`, copies the next token NUL-terminated into `out`
// (`outSize` bytes, honouring `"` quoting) and advances `*cursor` past it.
// `outQuoted`, when given, says whether the token was quoted. An empty `out`
// means the text ran out.
inline void Tokenize(const char **cursor, char *out, unsigned outSize, const char *delims,
                     int *outQuoted = nullptr) {
    using Fn = void(__stdcall *)(const char **, char *, unsigned, const char *, int *);
    reinterpret_cast<Fn>(Offsets::FUN_STORM_STR_TOKENIZE)(cursor, out, outSize, delims, outQuoted);
}

// SStrToFloat (FUN_SSTR_TO_FLOAT, `__stdcall`, RET 4): the frame loader's
// number conversion.
inline float ToFloat(const char *s) {
    using Fn = float(__stdcall *)(const char *);
    return reinterpret_cast<Fn>(Offsets::FUN_SSTR_TO_FLOAT)(s);
}

// SStrHashHT (FUN_SSTR_HASH_HT, `__stdcall`): the case-insensitive string
// hash of Storm's HASHKEY_STRI tables.
inline uint32_t HashHT(const char *s) {
    using Fn = uint32_t(__stdcall *)(const char *);
    return reinterpret_cast<Fn>(Offsets::FUN_SSTR_HASH_HT)(s);
}

} // namespace Storm
