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

// Mirrors of 4.3.4 Storm's `SStrLenUTF8` (0x004B3320) and `SStrCmpUTF8I`
// (0x004B2F90) with its decoder `SStrCmpGetNextTextUpper` (0x004B2E30),
// transcribed from `Engine/Source/Storm/Source/SStr.cpp` in the 4.3.4
// decompilation, quirks included: a sequence cut short by the terminator
// decodes as 0, a code point above U+FFFF folds to U+FFFD, and the Cyrillic
// fold shifts А..Е down by one so that Ё and ё land on Е's slot. Whatever
// the newer client answers, this answers.

#include "text/Utf8.h"

#include <cstdint>

namespace Text::Utf8 {

namespace {

// Total sequence length by lead byte (0 for the terminator). Continuation
// and invalid lead bytes count as one-byte sequences, as in Storm.
const uint8_t kBytesFromUtf8[256] = {
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6,
};

// The lead-byte marker bits accumulated by the shift-and-add decode below,
// indexed by sequence length.
const uint32_t kOffsetsFromUtf8[7] = {
    0x00000000, 0x00000000, 0x00003080, 0x000E2080, 0x03C82080, 0xFA082080, 0x82082080,
};

// `SStrCmpGetNextTextUpper`: decodes the code point at `*s`, advances `*s`
// past it, and writes the case-folded value to `*upper`. Returns the
// sequence length, or 0 when the terminator cut the sequence short (the
// pointer then rests on the terminator and `*upper` is 0).
int32_t NextUpper(const char **s, uint32_t *upper) {
    const int32_t bytes = kBytesFromUtf8[static_cast<uint8_t>(**s)];
    uint32_t code = 0;
    *upper = 0;

    switch (bytes) {
    case 6:
        code += static_cast<uint8_t>(*(*s)++);
        if (**s == '\0')
            return 0;
        code <<= 6;
        [[fallthrough]];
    case 5:
        code += static_cast<uint8_t>(*(*s)++);
        if (**s == '\0')
            return 0;
        code <<= 6;
        [[fallthrough]];
    case 4:
        code += static_cast<uint8_t>(*(*s)++);
        if (**s == '\0')
            return 0;
        code <<= 6;
        [[fallthrough]];
    case 3:
        code += static_cast<uint8_t>(*(*s)++);
        if (**s == '\0')
            return 0;
        code <<= 6;
        [[fallthrough]];
    case 2:
        code += static_cast<uint8_t>(*(*s)++);
        if (**s == '\0')
            return 0;
        code <<= 6;
        [[fallthrough]];
    case 1:
        code += static_cast<uint8_t>(*(*s)++);
        break;
    default:
        break;
    }

    code -= kOffsetsFromUtf8[bytes];
    if (code > 0xFFFF)
        code = 0xFFFD;

    uint32_t ch = code;
    if (bytes == 1) {
        if (ch >= 'a' && ch <= 'z')
            ch -= 32;
    } else if (bytes == 2) {
        if (ch >= 0xE0 && ch <= 0xFE) {
            ch -= 32; // Latin-1 à..þ → À..Þ
        } else if (ch == 0x153) {
            *upper = 0x152; // œ → Œ
            return bytes;
        } else if (ch == 0x401 || ch == 0x451) {
            *upper = 0x415; // Ё / ё → Е's slot
            return bytes;
        } else if (ch >= 0x410 && ch <= 0x415) {
            *upper = ch - 1; // А..Е shift down one, making room for Ё above
            return bytes;
        } else if (ch >= 0x430 && ch <= 0x44F) {
            *upper = ch - (ch <= 0x435 ? 1 : 0) - 32; // а..я → the same slots
            return bytes;
        }
    }
    *upper = ch;
    return bytes;
}

} // namespace

size_t Length(const char *s) {
    if (s == nullptr)
        return 0;
    size_t length = 0;
    for (; *s != '\0'; ++s) {
        if ((static_cast<uint8_t>(*s) & 0xC0) != 0x80)
            ++length;
    }
    return length;
}

int CompareI(const char *a, const char *b) {
    while (*a != '\0' || *b != '\0') {
        uint32_t upperA;
        NextUpper(&a, &upperA);
        uint32_t upperB;
        NextUpper(&b, &upperB);
        if (upperA != upperB)
            return static_cast<int32_t>(upperA - upperB);
    }
    return 0;
}

} // namespace Text::Utf8
