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

// `string.format` takes two-digit argument numbers (`%10$s`).
//
// 1.12's str_format (0x7FD3D0) is Lua 4.0's: a spec may start with `n$`, but
// only one digit, `isdigit(p[0]) && p[1] == '$'`. In "%10$s" the "10" is then
// read as a width and the '$' as the conversion, which raises "invalid
// option in `format'". 4.3.4's str_format adds a second branch for two
// digits, `isdigit(p[1]) && p[2] == '$'`, and its combat log strings need it
// (ACTION_SPELL_PERIODIC_LEECH_RESULT is "(%10$s %8$s Gained)").
//
// The thunk below stands in for the engine's one-digit branch and adds the
// two-digit one. Everything else stays the engine's: its locale isdigit on
// the first digit (already tested before the site), `arg++` after the site,
// the flag/width/precision scan from EBX, and every conversion. Every caller
// goes through it: `string.format`, `format`, and the C callers of
// FUN_LUA_STR_FORMAT (Frame::FormattedText, Text::NumericFormatter).

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace BaseLib::StringFormat {

namespace {

static_assert(Offsets::LUA_STR_FORMAT_FRAME_ARG == 8, "ArgNumberThunk writes [ebp - 8]");

// ESI = the spec after '%', whose first byte is a digit. Sets the argument
// number in [ebp - 8] and moves EBX past the `n$`; leaves both alone when
// the spec has no `n$`.
__declspec(naked) void ArgNumberThunk() {
    __asm {
        cmp byte ptr [esi + 1], '$'
        jne twoDigits
        movsx eax, byte ptr [esi]
        sub eax, '0'
        mov dword ptr [ebp - 8], eax
        lea ebx, [esi + 2]
        ret
    twoDigits:
        movzx eax, byte ptr [esi + 1]
        sub eax, '0'
        cmp eax, 9
        ja done
        cmp byte ptr [esi + 2], '$'
        jne done
        movsx ecx, byte ptr [esi]
        sub ecx, '0'
        lea ecx, [ecx + ecx * 4]
        lea eax, [eax + ecx * 2]
        mov dword ptr [ebp - 8], eax
        lea ebx, [esi + 3]
    done:
        ret
    }
}

// Runs on every registration pass; a site already patched no longer matches
// and is skipped.
void Apply() {
    static const uint8_t kOneDigit[18] = {0x80, 0x7E, 0x01, 0x24, 0x75, 0x0C, 0x0F, 0xBE, 0x06,
                                          0x83, 0xE8, 0x30, 0x89, 0x45, 0xF8, 0x8D, 0x5E, 0x02};
    Common::ReplaceWithCall(Offsets::SITE_LUA_STR_FORMAT_ARG_NUMBER, kOneDigit, sizeof kOneDigit,
                            reinterpret_cast<const void *>(&ArgNumberThunk));
}

const Game::ModuleAutoRegister _autoreg{&Apply};
const Game::GlueModuleAutoRegister _glueAutoreg{&Apply};

} // namespace

} // namespace BaseLib::StringFormat
