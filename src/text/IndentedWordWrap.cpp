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

// The text engine side of IndentedWordWrap (the methods are
// FontString::IndentedWordWrap).
//
// 4.3.4 carries the font string's style bit 0x20000 into its text engine
// as flag 0x2000, and four loops honour it, each from its second line on
// (all lines after the first, hard line breaks included):
//   - every wrap loop - the string layout, the wrapped height, the fit to a
//     height and the line enumeration - narrows the wrap width by
//     g_indentNormWidth = 15 / screen width in pixels;
//   - the string layout places a left-justified line at x = 15 pixels and
//     a right-justified one at -width - 15; a centred one only wraps
//     narrower. A wrapped link's rect takes the same x.
//
// 1.12's font string calls the same four engine loops through five entry
// points that each rebuild the engine flag word from the font string's
// flags and drop the bits they don't know, and each loop measures a line
// with one stepper call (Offsets.h, FUN_TEXT_WRAP_STEPPER). So:
//   - the font string's calls of the five entry points are redirected to
//     functions that note the indent bit (and, for the measures, where the
//     text starts) for the call, and the create's call of the node
//     constructor adds flag 0x2000 to the node it makes;
//   - the three measure loops' stepper calls narrow every line that does
//     not start at the text's start;
//   - in the string layout the node is in EBX and its line count (+0x9C, 4.3.4's
//     m_numLines) says which line this is. A thunk on its stepper call narrows
//     line 1 on and sets a left-justified line's x; a thunk on its justify
//     call moves a right-justified one. Both run before the x is copied into a
//     wrapped link's rect. The stepper itself stays the engine's (and
//     Text::InlineTexture's co-hook of it).
// Units: the stepper's wrap width is normalized X (pen pixels / the
// render-target width, VAR_TEXT_RASTER_X), so 15 / that width is 4.3.4's
// g_indentNormWidth; the layout's line x is in pen pixels.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Text::IndentedWordWrap {

namespace {

constexpr float kIndentPx = 15.0f; // 4.3.4's g_indentPixelWidth
constexpr int kJustifyLeft = 0;
constexpr int kJustifyRight = 2;

// 4.3.4's g_indentNormWidth.
float IndentWidth() {
    const int rasterX = Game::Read<int>(Offsets::VAR_TEXT_RASTER_X);
    return rasterX > 0 ? kIndentPx / static_cast<float>(rasterX) : 0.0f;
}

bool Indented(uint32_t fontStringFlags) { return (fontStringFlags & Offsets::FONT_STYLE_INDENTED_WORD_WRAP) != 0; }

// ---- The font string's entry points ----------------------------------------

// The text being measured by one of the three measure entry points, and
// whether its font string is indented; null outside one.
struct Measure {
    const char *start;
    bool indented;
};
Measure g_measure{nullptr, false};

// The create entry point is running for an indented font string.
bool g_createIndented = false;

using Create_t = void *(__fastcall *)(void *ecx, void *edx, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                      uint32_t, uint32_t, uint32_t);
using Height_t = float(__fastcall *)(void *ecx, const char *text, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using Fit_t = int(__fastcall *)(void *ecx, const char *text, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                uint32_t, uint32_t);
using CreateString_t = void *(__fastcall *)(void *font, const char *text, uint32_t, uint32_t, uint32_t, uint32_t,
                                            uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using Step_t = void(__fastcall *)(void *font, const char *text, float fontH, float wrapWidth, int *outBreak,
                                  float *outWidth, void *outNext, float shadow, uint32_t flags, uint8_t *state);
using ScreenToPixelWidth_t = float(__fastcall *)(int raw, float value);

// FUN_FS_TEXT_BLOCK_CREATE: font string flags = stack arg 5.
void *__fastcall BlockCreate(void *ecx, void *edx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                             uint32_t flags, uint32_t a6, uint32_t a7) {
    const bool saved = g_createIndented;
    g_createIndented = Indented(flags);
    void *block = reinterpret_cast<Create_t>(Offsets::FUN_FS_TEXT_BLOCK_CREATE)(ecx, edx, a0, a1, a2, a3, a4, flags,
                                                                                a6, a7);
    g_createIndented = saved;
    return block;
}

// FUN_TEXT_CREATE_STRING: engine flags = stack arg 8, stored to the node.
void *__fastcall CreateString(void *font, const char *text, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                              uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t flags, uint32_t a9,
                              uint32_t a10) {
    if (g_createIndented)
        flags |= Offsets::TEXT_FLAG_INDENTED_WORD_WRAP;
    return reinterpret_cast<CreateString_t>(Offsets::FUN_TEXT_CREATE_STRING)(font, text, a0, a1, a2, a3, a4, a5, a6,
                                                                             a7, flags, a9, a10);
}

// FUN_FS_TEXT_BLOCK_HEIGHT: flags = stack arg 4; the height comes back in ST0.
float __fastcall BlockHeight(void *ecx, const char *text, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                             uint32_t flags) {
    const Measure saved = g_measure;
    g_measure = {text, Indented(flags)};
    const float height =
        reinterpret_cast<Height_t>(Offsets::FUN_FS_TEXT_BLOCK_HEIGHT)(ecx, text, a0, a1, a2, a3, flags);
    g_measure = saved;
    return height;
}

// FUN_FS_TEXT_FIT_HEIGHT and FUN_FS_TEXT_WRAP_LINES: flags = stack arg 6.
template <uintptr_t Target>
int __fastcall Fit(void *ecx, const char *text, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                   uint32_t a5, uint32_t flags) {
    const Measure saved = g_measure;
    g_measure = {text, Indented(flags)};
    const int result = reinterpret_cast<Fit_t>(Target)(ecx, text, a0, a1, a2, a3, a4, a5, flags);
    g_measure = saved;
    return result;
}

// ---- The measure loops ------------------------------------------------------

void __fastcall MeasureStep(void *font, const char *text, float fontH, float wrapWidth, int *outBreak,
                            float *outWidth, void *outNext, float shadow, uint32_t flags, uint8_t *state) {
    if (g_measure.indented && text != g_measure.start)
        wrapWidth -= IndentWidth();
    reinterpret_cast<Step_t>(Offsets::FUN_TEXT_WRAP_STEPPER)(font, text, fontH, wrapWidth, outBreak, outWidth,
                                                             outNext, shadow, flags, state);
}

// ---- The string layout ------------------------------------------------------

uintptr_t g_stepper = Offsets::FUN_TEXT_WRAP_STEPPER;
float g_layoutShrink = 0.0f;

bool IndentedLine(const void *node) {
    return (Game::Read<uint32_t>(node, Offsets::OFF_TEXT_NODE_FLAGS) & Offsets::TEXT_FLAG_INDENTED_WORD_WRAP) != 0 &&
           Game::Read<int>(node, Offsets::OFF_TEXT_NODE_BUILT_LINES) != 0;
}

// Before the layout measures a line: the width cut for it, and a
// left-justified line's x in the layout's frame.
void __stdcall PrepareLayoutLine(const void *node, uint8_t *frame) {
    if (!IndentedLine(node)) {
        g_layoutShrink = 0.0f;
        return;
    }
    g_layoutShrink = IndentWidth();
    if (Game::Read<int>(node, Offsets::OFF_TEXT_NODE_JUSTIFY) == kJustifyLeft)
        *reinterpret_cast<float *>(frame - Offsets::DRAW_BUILDER_FRAME_LINE_X) = kIndentPx;
}

// Replaces the layout's stepper call. On entry [esp+8] is the wrap width
// argument; the node is in EBX and the layout's frame pointer in EBP.
__declspec(naked) void LayoutStepThunk() {
    __asm {
        push ecx
        push edx
        push ebp
        push ebx
        call PrepareLayoutLine
        pop edx
        pop ecx
        fld dword ptr [esp + 8]
        fsub dword ptr [g_layoutShrink]
        fstp dword ptr [esp + 8]
        jmp dword ptr [g_stepper]
    }
}

// The layout's right / centre x, moved for a right-justified line.
float __stdcall JustifyX(float value, int raw, const void *node) {
    float x = reinterpret_cast<ScreenToPixelWidth_t>(Offsets::FUN_TEXT_SCREEN_TO_PIXEL_WIDTH)(raw, value);
    if (IndentedLine(node) && Game::Read<int>(node, Offsets::OFF_TEXT_NODE_JUSTIFY) == kJustifyRight)
        x -= kIndentPx;
    return x;
}

// Replaces `FUN_TEXT_SCREEN_TO_PIXEL_WIDTH(raw /*ecx*/, value)`, ret 4;
// the node is in EBX.
__declspec(naked) void JustifyXThunk() {
    __asm {
        push ebx
        push ecx
        push dword ptr [esp + 0Ch]
        call JustifyX
        ret 4
    }
}

void PatchCode() {
    struct Site {
        uintptr_t call;
        uintptr_t expected;
        const void *target;
    };
    const Site sites[] = {
        {Offsets::CALL_FONTSTRING_BLOCK_CREATE, Offsets::FUN_FS_TEXT_BLOCK_CREATE,
         reinterpret_cast<const void *>(&BlockCreate)},
        {Offsets::CALL_FS_TEXT_BLOCK_CREATE_STRING, Offsets::FUN_TEXT_CREATE_STRING,
         reinterpret_cast<const void *>(&CreateString)},
        {Offsets::CALL_FONTSTRING_BLOCK_HEIGHT, Offsets::FUN_FS_TEXT_BLOCK_HEIGHT,
         reinterpret_cast<const void *>(&BlockHeight)},
        {Offsets::CALL_FONTSTRING_FIT_HEIGHT_1, Offsets::FUN_FS_TEXT_FIT_HEIGHT,
         reinterpret_cast<const void *>(&Fit<Offsets::FUN_FS_TEXT_FIT_HEIGHT>)},
        {Offsets::CALL_FONTSTRING_FIT_HEIGHT_2, Offsets::FUN_FS_TEXT_FIT_HEIGHT,
         reinterpret_cast<const void *>(&Fit<Offsets::FUN_FS_TEXT_FIT_HEIGHT>)},
        {Offsets::CALL_FONTSTRING_WRAP_LINES, Offsets::FUN_FS_TEXT_WRAP_LINES,
         reinterpret_cast<const void *>(&Fit<Offsets::FUN_FS_TEXT_WRAP_LINES>)},
        {Offsets::CALL_TEXT_BLOCK_HEIGHT_STEP, Offsets::FUN_TEXT_WRAP_STEPPER,
         reinterpret_cast<const void *>(&MeasureStep)},
        {Offsets::CALL_TEXT_FIT_HEIGHT_STEP, Offsets::FUN_TEXT_WRAP_STEPPER,
         reinterpret_cast<const void *>(&MeasureStep)},
        {Offsets::CALL_TEXT_ENUMERATE_LINES_STEP, Offsets::FUN_TEXT_WRAP_STEPPER,
         reinterpret_cast<const void *>(&MeasureStep)},
        {Offsets::CALL_TEXT_DRAW_BUILDER_STEP, Offsets::FUN_TEXT_WRAP_STEPPER,
         reinterpret_cast<const void *>(&LayoutStepThunk)},
        {Offsets::CALL_TEXT_DRAW_BUILDER_JUSTIFY_X, Offsets::FUN_TEXT_SCREEN_TO_PIXEL_WIDTH,
         reinterpret_cast<const void *>(&JustifyXThunk)},
    };
    for (const Site &site : sites)
        Common::RedirectCall(site.call, site.expected, site.target);
}

const Game::ModuleAutoRegister _autoreg{&PatchCode};

} // namespace

} // namespace Text::IndentedWordWrap
