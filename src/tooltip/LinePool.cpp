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

// Lift GameTooltip's line cap — 4.3.4's grow-on-demand, pure C++.
//
// Vanilla's add-line body (FUN_00530270) silently drops any line once
// `numLines` reaches `numLinesAllocated`, and that count is just "how many
// contiguous `<name>TextLeftN`/`TextRightN` FontStrings the FrameXML
// template declared" (30), counted once by the XML-OnLoad scan
// (FUN_00529650). Stat-heavy tooltips (pfUI's eqcompare block) lose their
// tail.
//
// 4.3.4's CGTooltip::AddLine (wowdecomp15595 Tooltip.cpp) grows the
// pool inside the gate: when the line about to be added takes the last
// slot, it first builds the next `TextLeft/Right<max+1>` pair (alloc → ctor
// → SetName → previous line's font → template anchor → Hide) and appends it
// with CGTooltip::AddFontStrings. We co-hook the same gate and do the same,
// appending through 1.12's own AddFontStrings body (FUN_00529940, the
// `GameTooltip:AddFontStrings` method's core). FUN_00530270 is the one
// gate every line goes through — the Lua AddLine/AddDoubleLine methods and
// the native spell/item/unit builders all call it — so every tooltip grows
// with no cap and no frame list.
//
// After every add the pool keeps one spare line, as 4.3.4's does;
// Tooltip::CompareItem's "Currently Equipped" prepend relies on it.

#include "Game.h"
#include "Offsets.h"
#include "frame/Region.h"

#include <cstdint>
#include <cstdio>

namespace Tooltip::LinePool {

namespace {

// --- engine entry points (all __thiscall; see Offsets.h) -------------------
using GetName_t = const char *(__thiscall *)(void *self);
using SetName_t = void(__thiscall *)(void *region, const char *name);
using SetFont_t = void(__thiscall *)(void *fontHolder, void *fontObject);
using SetPoint_t = void(__thiscall *)(void *anchor, int point, void *relAnchor, int relPoint,
                                      float x, float y, int flag);
using HideFontString_t = void(__fastcall *)(void *fs);
using AddFontStrings_t = void(__thiscall *)(void *tooltip, void *left, void *right);

// The gate we co-hook: __thiscall(tooltip, left, right, lColor*, rColor*,
// wrap), `ret 0x14`. Hooked via the standard __fastcall(ecx, edx, …) shim —
// __thiscall passes `this` in ECX and the rest on the stack, so the
// trailing stack args line up and the phantom `edx` is harmless.
using AddLine_t = void(__fastcall *)(void *tooltip, void *edx, const char *left,
                                     const char *right, const void *leftColor,
                                     const void *rightColor, int wrap);
AddLine_t g_addLineOriginal = nullptr;

bool HasText(const char *text) {
    return text != nullptr && text[0] != '\0';
}

void *LineAt(uint8_t *tooltip, int descOffset, uint32_t index) {
    return Game::Read<void **>(tooltip, descOffset + 8)[index];
}

// Builds one hidden CSimpleFontString: alloc → ctor(parent) → name → font
// (copied from `fontSource`) → anchor → hide. Mirrors 4.3.4's AddLine grow.
// Returns nullptr on allocation failure.
void *CreateLine(void *parent, const char *name, void *fontSource, int point,
                 void *relRegion, int relPoint, float x, float y) {
    void *fs = Frame::Region::CreateFontString(parent, Offsets::DRAWLAYER_ARTWORK, 1);
    if (fs == nullptr)
        return nullptr;
    auto *fsBytes = reinterpret_cast<uint8_t *>(fs);

    reinterpret_cast<SetName_t>(Offsets::FUN_REGION_SET_NAME)(fs, name);

    void *fontObject = Game::Read<void *>(fontSource, Offsets::OFF_FONTSTRING_FONT_OBJECT);
    if (fontObject != nullptr)
        reinterpret_cast<SetFont_t>(Offsets::FUN_FONTSTRING_SET_FONT)(
            fsBytes + Offsets::OFF_FONTSTRING_FONT_HOLDER, fontObject);

    reinterpret_cast<SetPoint_t>(Offsets::FUN_REGION_SET_POINT)(
        fsBytes + Offsets::OFF_REGION_ANCHOR, point,
        reinterpret_cast<uint8_t *>(relRegion) + Offsets::OFF_REGION_ANCHOR, relPoint,
        Game::PixelToInternal(x), Game::PixelToInternal(y), 1);

    // The clear only hides lines below `numLines`, so a spare line has to
    // start hidden — the same `+0xC4 = 0` + hide the clear does.
    Game::Ref<int>(fs, Offsets::OFF_FONTSTRING_SHOWN_FLAG) = 0;
    reinterpret_cast<HideFontString_t>(Offsets::FUN_FONTSTRING_HIDE)(fs);
    return fs;
}

// Appends line `max + 1` to a tooltip whose pool holds `max` lines. The
// previous line is the anchor target and the font source, as in the
// template: left TOPLEFT → previous left BOTTOMLEFT at (0, line gap), right
// RIGHT → its own left LEFT at (40, 0). False when allocation fails.
bool GrowOne(uint8_t *tooltip, uint32_t max) {
    void *prevLeft = LineAt(tooltip, Offsets::OFF_GAMETOOLTIP_TEXTLEFT_DESC, max - 1);
    void *prevRight = LineAt(tooltip, Offsets::OFF_GAMETOOLTIP_TEXTRIGHT_DESC, max - 1);
    if (prevLeft == nullptr || prevRight == nullptr)
        return false;

    auto vtable = *reinterpret_cast<void ***>(tooltip);
    const char *baseName = reinterpret_cast<GetName_t>(vtable[1])(tooltip); // slot 1 = GetName
    if (baseName == nullptr)
        baseName = "<unnamed>"; // the scan's own fallback

    char nameLeft[256];
    char nameRight[256];
    std::snprintf(nameLeft, sizeof nameLeft, "%sTextLeft%u", baseName, max + 1);
    std::snprintf(nameRight, sizeof nameRight, "%sTextRight%u", baseName, max + 1);

    void *left = CreateLine(tooltip, nameLeft, prevLeft, Offsets::FRAMEPOINT_TOPLEFT, prevLeft,
                            Offsets::FRAMEPOINT_BOTTOMLEFT, 0.0f,
                            Game::Read<float>(Offsets::VAR_TOOLTIP_LINE_GAP));
    if (left == nullptr)
        return false;
    void *right = CreateLine(tooltip, nameRight, prevRight, Offsets::FRAMEPOINT_RIGHT, left,
                             Offsets::FRAMEPOINT_LEFT, 40.0f, 0.0f);
    if (right == nullptr)
        return false;

    reinterpret_cast<AddFontStrings_t>(Offsets::FUN_GAMETOOLTIP_ADD_FONT_STRINGS)(tooltip, left,
                                                                                  right);
    return true;
}

void __fastcall AddLine_h(void *tooltip, void *edx, const char *left, const char *right,
                          const void *leftColor, const void *rightColor, int wrap) {
    // 4.3.4 skips the grow for an empty line and for a tooltip whose
    // template declared no lines; the original then no-ops on its own.
    if (HasText(left) || HasText(right)) {
        auto *t = static_cast<uint8_t *>(tooltip);
        const uint32_t numLines = Game::Read<uint32_t>(t, Offsets::OFF_GAMETOOLTIP_NUM_LINES);
        uint32_t max = Game::Read<uint32_t>(t, Offsets::OFF_GAMETOOLTIP_NUM_LINES_ALLOC);
        // A loop rather than 4.3.4's single `==`, so a pool a direct
        // `numLines` writer (the compare prepend) left with no spare still
        // recovers it.
        while (max != 0 && numLines + 1 >= max && GrowOne(t, max))
            ++max;
    }
    g_addLineOriginal(tooltip, edx, left, right, leftColor, rightColor, wrap);
}

static const Game::HookAutoRegister _hookreg{
    Offsets::FUN_GAMETOOLTIP_ADD_LINE,
    reinterpret_cast<void *>(&AddLine_h),
    reinterpret_cast<void **>(&g_addLineOriginal)};

} // namespace

} // namespace Tooltip::LinePool
