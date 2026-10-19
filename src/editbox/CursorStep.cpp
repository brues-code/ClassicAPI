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

// Two edit box caret faults in the step from glyphs to bytes.
//
// The freeze on a text that ends in markup.
//
// FUN_EDITBOX_STEP_BYTES turns cursor steps into bytes. A forward step first
// skips every leading colour (class 0) and link-start (class 4) token, and
// with `atomic` (every keyboard move and delete) every token inside a link,
// until it has consumed a visible one. That loop has no end test. The slot
// after the last byte holds 0, which reads as a colour token of length 0, so
// a text whose last token is a colour escape (`|cffffffff`), or that ends
// inside an unclosed link, makes one Right arrow or Delete spin forever.
// Typing cannot build such a text (a typed `|` is stored as `||`), but
// SetText and Insert can (benilla-re ui/editboxmarkup "Faults").
//
// 4.3.4's GetNumToLen has the same loop, but its classes start with
// CODE_INVALIDCODE = 0 for a plain character, so the terminator's 0 is a
// class the loop does not skip, and the step stops there. This hook replaces
// the forward branch with the engine's walk plus that stop; a backward step
// is already bounded by the table's start and runs the engine's own code.
//
// The right-justified click. The hit test finds how many glyphs fit between
// the click and the line's right end, then converts that count to bytes by
// stepping forward from the line's START and subtracts the result from the
// line's end. With markup or multibyte characters the first N glyphs of a
// line and the last N cover different byte counts, so the caret lands on the
// wrong character (benilla-re ui/editboxmarkup "Faults"; 4.3.4 has the same
// fault). Its one conversion call is redirected to step back from the line's
// end instead.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace EditBox::CursorStep {

namespace {

constexpr uint32_t kClassColorOn = 0;
constexpr uint32_t kClassColorRestore = 1;
constexpr uint32_t kClassLinkStart = 4;
constexpr uint32_t kClassLinkStop = 5;

using StepBytes_t = int(__fastcall *)(void *editBox, void *edx, int offset, int steps, int atomic);
StepBytes_t g_origStepBytes = nullptr;

uint32_t ClassOf(uint32_t info) { return (info >> 16) & 0xFF; }
uint32_t LengthOf(uint32_t info) { return info & 0xFFFF; }

int __fastcall StepBytes_h(void *editBox, void *edx, int offset, int steps, int atomic) {
    if (steps <= 0)
        return g_origStepBytes(editBox, edx, offset, steps, atomic);

    // The engine's forward walk (0x0077BB52..0x0077BBC3), token by token.
    const uint32_t *info = Game::Read<const uint32_t *>(editBox, Offsets::OFF_EDITBOX_CHAR_CLASS) + offset;
    const bool linkAtomic = static_cast<uint8_t>(atomic) != 0;
    int bytes = 0;
    while (steps-- > 0) {
        if (*info == 0)
            break;
        uint32_t token;
        do {
            token = *info;
            bytes += LengthOf(token);
            info += LengthOf(token);
            // 4.3.4's stop: the terminator ends the skip.
            if (token == 0)
                break;
        } while (ClassOf(token) == kClassColorOn || ClassOf(token) == kClassLinkStart ||
                 (linkAtomic && (token & 0x80000000u) != 0 && ClassOf(token) != kClassLinkStop));
        while (ClassOf(*info) == kClassColorRestore || ClassOf(*info) == kClassLinkStop) {
            bytes += LengthOf(*info);
            info += LengthOf(*info);
        }
    }
    return bytes;
}

const Game::HookAutoRegister _hook{Offsets::FUN_EDITBOX_STEP_BYTES, reinterpret_cast<void *>(&StepBytes_h),
                                   reinterpret_cast<void **>(&g_origStepBytes)};

// Called as `FUN_EDITBOX_STEP_BYTES(lineStart, glyphs, 0)`; returns the bytes
// that `glyphs` steps cover backwards from the end of the line that starts at
// lineStart, which the hit test subtracts from that end.
int __fastcall RightJustifiedStep(void *editBox, void *edx, int lineStart, int glyphs, int atomic) {
    if (glyphs <= 0)
        return 0;
    const int count = Game::Read<int>(editBox, Offsets::OFF_EDITBOX_LINE_COUNT);
    const int *starts = Game::Read<const int *>(editBox, Offsets::OFF_EDITBOX_LINE_STARTS);
    // A line that fits a glyph is not empty, so its start is followed by a
    // larger entry: its end.
    for (int i = 0; i + 1 < count; ++i) {
        if (starts[i] == lineStart && starts[i + 1] > lineStart)
            return reinterpret_cast<StepBytes_t>(Offsets::FUN_EDITBOX_STEP_BYTES)(editBox, edx, starts[i + 1],
                                                                                 -glyphs, atomic);
    }
    return reinterpret_cast<StepBytes_t>(Offsets::FUN_EDITBOX_STEP_BYTES)(editBox, edx, lineStart, glyphs, atomic);
}

void PatchCode() {
    Common::RedirectCall(Offsets::CALL_EDITBOX_HIT_RIGHT_STEP, Offsets::FUN_EDITBOX_STEP_BYTES,
                         reinterpret_cast<const void *>(&RightJustifiedStep));
}

const Game::ModuleAutoRegister _autoreg{&PatchCode};

} // namespace

} // namespace EditBox::CursorStep
