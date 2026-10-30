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

// A font string as tall as its text keeps all of its lines.
//
// A font string with a rect height cuts its text to the lines that fit and
// ends it with "..." (CSimpleFontString::FitText, 0x771EC0). The count of
// what fits (GxuFontGetMaxCharsWithinHeight, 0x5C21C0) adds each line's
// height and gap to a running float sum, and stops at the first line where
// `blockHeight + 2^-20 < sum + fontHeight`. A font string with no height of
// its own gets its text's height (0x5C2070), and that height is a product,
// n * fontHeight + (n - 1) * gap. Over many lines the sum drifts from the
// product by more than 2^-20 at most UI scales. Then the last line "does not
// fit", and FitText cuts the text: a long one to its first 4092 bytes plus
// "...", with the rest of the box blank. A multi-line edit box shows this
// once its text passes 4 KB (ScrollingEditBoxTemplate, chat copy boxes).
//
// 4.3.4 keeps the product and lets the margin grow with the lines tried:
// `blockHeight + numLines * 2^-20` (GxuFontGetMaxCharsWithinWidthAndHeight).
// The thunk below does the same with the engine's own 2^-20. The first line
// keeps 1.12's margin, and each line adds a millionth of the screen height,
// so text that really overflows is still cut. A float32 model of the chain
// (fit after the round trip through the font string's cached height) lost
// the last line in 1276 of 3000 multi-line fits with 1.12's margin and in
// none with 4.3.4's.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Text::FitHeight {

namespace {

// 4.3.4's numLines: the line the fit is trying.
int g_line = 0;

constexpr uintptr_t addrEpsilon = Offsets::VAR_TEXT_FIT_HEIGHT_EPSILON;
static_assert(Offsets::TEXT_FIT_HEIGHT_FRAME_FITTED_BYTES == 8, "LineMarginThunk reads [ebp - 8]");

// Stands in for `fadd dword ptr [VAR_TEXT_FIT_HEIGHT_EPSILON]`, with the
// block height in ST0. The bytes fitted so far ([ebp - 8]) are 0 only while
// the first line is tried.
__declspec(naked) void LineMarginThunk() {
    __asm {
        cmp dword ptr [ebp - 8], 0
        jne counted
        mov dword ptr [g_line], 0
    counted:
        inc dword ptr [g_line]
        fild dword ptr [g_line]
        mov eax, addrEpsilon
        fmul dword ptr [eax]
        faddp st(1), st(0)
        ret
    }
}

// Runs on every registration pass; a site already patched no longer matches
// and is skipped.
void Apply() {
    static const uint8_t kMarginAdd[6] = {0xD8, 0x05, 0xBC, 0x26, 0x80, 0x00};
    Common::ReplaceWithCall(Offsets::SITE_TEXT_FIT_HEIGHT_TOLERANCE, kMarginAdd, sizeof kMarginAdd,
                            reinterpret_cast<const void *>(&LineMarginThunk));
}

const Game::ModuleAutoRegister _autoreg{&Apply};
const Game::GlueModuleAutoRegister _glueAutoreg{&Apply};

} // namespace

} // namespace Text::FitHeight
