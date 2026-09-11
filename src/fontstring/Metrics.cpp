// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// FontString metric backports.
//
// 1.12's FontString method table (32 entries at 0x0087C1D8, registry context
// VAR_FONTSTRING_METHOD_REGISTRY) ships GetStringWidth but none of the later
// metric bindings. The engine already has every internal getter — these are
// the thin Lua bindings Blizzard never wrote for 1.12:
//
//   GetStringHeight()         — 2.3.0 binding; internal getter
//                               FUN_FONTSTRING_STRING_HEIGHT (wrap-aware,
//                               lines×fontH + (lines−1)×spacing, cache
//                               fs+0x100, 0 for empty text).
//   GetUnboundedStringWidth() — the substring measure with the WHOLE string
//                               (FUN_FONTSTRING_MEASURE_SUBSTRING, len 0 =
//                               strlen): the string's width with no wrap cap.
//                               Icon-aware for free — the call routes through
//                               text/InlineTexture.cpp's co-hook.
//   GetWrappedWidth()         — the RENDERED width: the widest wrapped line
//                               (the tooltip auto-size's own recipe — break
//                               array + per-segment substring measures).
//   GetNumLines()             — the node's built line count (render truth,
//                               OFF_TEXT_NODE_BUILT_LINES) once painted; for
//                               an unbuilt node, the engine's own break-array
//                               computer (FUN_FONTSTRING_BREAK_ARRAY — also
//                               icon-aware via the wrap-stepper co-hook).
//   GetLineHeight()           — the fs font height via the same getter the
//                               rebuild feeds the text node (FUN_007727b0);
//                               excludes spacing, like retail (GetSpacing is
//                               its own method).
//   IsTruncated()             — did the engine cut the text off with an
//                               ellipsis? Reads the render's own output: the
//                               built text node's copy of the laid-out string
//                               (OFF_TEXT_NODE_TEXT) vs the source. Same
//                               "displayed vs full" comparison retail does; no
//                               reconstruction of the fit/wrap/maxLines logic.
//                               Reflects the last rendered layout (the display-
//                               text resolver's rect is zero until a render, so
//                               a Lua-time re-call cannot see the bound).
//   SetMaxLines(n)/GetMaxLines() — cap the fontstring to n wrapped lines; text
//                               past the cap is ellipsized. Writes the engine's
//                               own per-fontstring maxLines field (fs+0x128,
//                               already honored by the truncation resolver) and
//                               re-invalidates the layout like SetText does.
//                               1.12 has no SetWordWrap, so SetMaxLines(1) is
//                               how you get a single-line, ellipsized label.
//   SetFormattedText(fmt,...) — string.format + SetText convenience. The format
//                               runs through the engine's OWN str_format C
//                               function (FUN_LUA_STR_FORMAT) under pcall — NOT
//                               _G.string.format, so an addon replacing
//                               string.format can't hijack it (matches retail).
//                               The set goes through FUN_FONTSTRING_SET_TEXT,
//                               keeping the sanitizer/pipe handling identical to
//                               SetText.
//
// Push conversion mirrors Script_GetStringWidth (0x0079E510): resolve self,
// call the internal getter, convert anchor units → UI pixels, push.
//
// No inline-icon adjustment lives HERE: the width/height internal getters are
// already icon-adjusted by their co-hooks (calling the hooked VA routes
// through the detour), and icons never change GetLineHeight.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace FontString::Metrics {
namespace {

// FUN_FONTSTRING_STRING_HEIGHT — ECX = fs, no stack args, float in x87 ST0.
using StringHeightInternal_t = float(__fastcall *)(void *fs);
// FUN_FONTSTRING_MEASURE_SUBSTRING — __thiscall(fs, text, len); dummy-EDX form.
using MeasureSubstring_t = float(__fastcall *)(void *fs, void *edx, const char *text, int len);
// FUN_FONTSTRING_FONT_HEIGHT — __thiscall(fs, mode-on-stack); dummy-EDX form.
using FsFontHeight_t = float(__fastcall *)(void *fs, void *edx, int mode);
// FUN_FONTSTRING_BREAK_ARRAY — __thiscall(fs, text, wrapWidth, outBreaks, cap).
using BreakArray_t = uint32_t(__fastcall *)(void *fs, void *edx, const char *text, float width,
                                            int *outBreaks, int cap);
// FUN_FONTSTRING_SET_TEXT — __thiscall(fs, text, flag = 0); dummy-EDX form.
using FsSetText_t = void(__fastcall *)(void *fs, void *edx, const char *text, int flag);
// FUN_HANDLE_RELEASE — DecRef a ref-counted handle. __fastcall(handle).
using HandleRelease_t = void(__fastcall *)(void *handle);
// FUN_FONTSTRING_LAYOUT_INVALIDATE — region layout invalidate. __thiscall(region,
// int); dummy-EDX form.
using LayoutInvalidate_t = void(__fastcall *)(void *region, void *edx, int arg);

// Invalidate a fontstring's layout exactly as SetText (FUN_00771D80) does after
// a content change, so a layout-affecting property change (max lines) takes
// effect on the next draw: drop the cached measures, release the built node,
// clear the layout-dirty bit, and notify the region to re-lay-out. Re-calling
// SetText would NOT work — it early-outs when the text string is unchanged.
void InvalidateLayout(void *fs) {
    Game::Ref<float>(fs, Offsets::OFF_FONTSTRING_WIDTH_CACHE) = 0.0f;
    Game::Ref<float>(fs, Offsets::OFF_FONTSTRING_HEIGHT_CACHE) = 0.0f;
    void *block = Game::Read<void *>(fs, Offsets::OFF_FONTSTRING_TEXT_BLOCK);
    if (block != nullptr) {
        reinterpret_cast<HandleRelease_t>(Offsets::FUN_HANDLE_RELEASE)(block);
        Game::Ref<void *>(fs, Offsets::OFF_FONTSTRING_TEXT_BLOCK) = nullptr;
    }
    Game::Ref<uint32_t>(fs, Offsets::OFF_FONTSTRING_DIRTY_FLAGS) &= ~1u;
    void *region = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(fs) + 0x24);
    reinterpret_cast<LayoutInvalidate_t>(Offsets::FUN_FONTSTRING_LAYOUT_INVALIDATE)(
        region, nullptr, 0);
}

// Anchor units → UI pixels: `FUN_0041AE40(FUN_0041AD70() × DAT_007FFD68 × v)`
// = v × [VAR_UI_COORD_SCALE_DIV] × 1024 / [VAR_UI_COORD_SCALE_MUL] — the exact
// push chain Script_GetStringWidth uses (the inverse of
// Tooltip::LinePool::PixelToInternal).
double InternalToPixel(float v) {
    const float mul = Game::Read<float>(Offsets::VAR_UI_COORD_SCALE_MUL);
    const float div = Game::Read<float>(Offsets::VAR_UI_COORD_SCALE_DIV);
    if (mul == 0.0f)
        return 0.0;
    return static_cast<double>(v) * div * Offsets::UI_COORD_SCALE_UNIT / mul;
}

// The fs's wrap budget in the units FUN_FONTSTRING_BREAK_ARRAY expects
// (width-getter units — it multiplies by fs+0x7C itself before gxu, verified
// decompile), or 0 when the rect is unresolved. rectWidth ÷ scale, calibrated
// EMPIRICALLY against the render with a three-point probe (a 100px box at
// scale ~0.91, text whose candidate lines measure 78.1 / 94.6 / 101.6px):
//   raw rectW      → effective budget ~91px  → broke one word EARLY,
//   rectW/scale²   → effective budget ~110px → broke one word LATE,
//   rectW/scale    → effective budget 100px  → breaks exactly where the
//                    render does (94.6 ≤ 100 < 101.6).
// All three observations fit budget_px = pxOf(param) × scale, pinning the
// single ÷scale. (A pure paper derivation from the rebuild's node budget
// predicted /scale² — one hop of that chain models the live behaviour wrong;
// the probe is the arbiter.)
float WrapBudget(void *fs) {
    const float *rc = Game::Ptr<const float>(fs, Offsets::OFF_REGION_RECT);
    float width = (rc[3] > rc[1]) ? (rc[3] - rc[1]) : (rc[1] - rc[3]);
    const float s = Game::Read<float>(fs, Offsets::OFF_LAYOUT_SCALE);
    if (width > 0.0f && s > 0.0f)
        width /= s;
    return width;
}

// Shared self-resolve for every method here.
void *ResolveSelf(void *L, const char *usage) {
    void *fs = nullptr;
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_TABLE)
        fs = Game::Lua::ResolveFontString(L, 1, /*raiseError=*/false);
    if (fs == nullptr)
        Game::Lua::Error(L, "%s", usage);
    return fs;
}

int __fastcall Script_GetStringHeight(void *L) {
    void *fs = ResolveSelf(L, "Usage: fontstring:GetStringHeight()");
    if (fs == nullptr)
        return 0;
    const float h = reinterpret_cast<StringHeightInternal_t>(
        Offsets::FUN_FONTSTRING_STRING_HEIGHT)(fs);
    Game::Lua::PushNumber(L, InternalToPixel(h));
    return 1;
}

int __fastcall Script_GetUnboundedStringWidth(void *L) {
    void *fs = ResolveSelf(L, "Usage: fontstring:GetUnboundedStringWidth()");
    if (fs == nullptr)
        return 0;
    const char *text = Game::Read<const char *>(fs, Offsets::OFF_FONTSTRING_TEXT);
    if (text == nullptr || *text == '\0') {
        Game::Lua::PushNumber(L, 0.0);
        return 1;
    }
    const float w = reinterpret_cast<MeasureSubstring_t>(
        Offsets::FUN_FONTSTRING_MEASURE_SUBSTRING)(fs, nullptr, text, 0);
    Game::Lua::PushNumber(L, InternalToPixel(w));
    return 1;
}

int __fastcall Script_GetNumLines(void *L) {
    void *fs = ResolveSelf(L, "Usage: fontstring:GetNumLines()");
    if (fs == nullptr)
        return 0;
    const char *text = Game::Read<const char *>(fs, Offsets::OFF_FONTSTRING_TEXT);
    if (text == nullptr || *text == '\0') {
        Game::Lua::PushNumber(L, 0.0);
        return 1;
    }
    // Render truth first: the built node's line count. Zero until the node
    // paints once (hidden fs, mid-rebuild) — fall back below.
    void *block = Game::Read<void *>(fs, Offsets::OFF_FONTSTRING_TEXT_BLOCK);
    if (block != nullptr) {
        void *node = Game::Read<void *>(block, Offsets::OFF_TEXTBLOCK_NODE);
        if (node != nullptr) {
            const int built = Game::Read<int>(node, Offsets::OFF_TEXT_NODE_BUILT_LINES);
            if (built > 0) {
                Game::Lua::PushNumber(L, static_cast<double>(built));
                return 1;
            }
        }
    }
    // Unbuilt: the engine's own break-array computer, fed the fs's wrap
    // budget (see WrapBudget's scale note). An unresolved rect (0 width)
    // means no wrap constraint exists yet — one line, like a fresh
    // single-anchor fs.
    const float width = WrapBudget(fs);
    if (width <= 0.0f) {
        Game::Lua::PushNumber(L, 1.0);
        return 1;
    }
    int breaks[64];
    uint32_t lines = reinterpret_cast<BreakArray_t>(Offsets::FUN_FONTSTRING_BREAK_ARRAY)(
        fs, nullptr, text, width, breaks, 64);
    if (lines > 64u)
        lines = 64u;
    if (lines == 0u)
        lines = 1u;
    Game::Lua::PushNumber(L, static_cast<double>(lines));
    return 1;
}

int __fastcall Script_GetWrappedWidth(void *L) {
    void *fs = ResolveSelf(L, "Usage: fontstring:GetWrappedWidth()");
    if (fs == nullptr)
        return 0;
    const char *text = Game::Read<const char *>(fs, Offsets::OFF_FONTSTRING_TEXT);
    if (text == nullptr || *text == '\0') {
        Game::Lua::PushNumber(L, 0.0);
        return 1;
    }
    auto measure =
        reinterpret_cast<MeasureSubstring_t>(Offsets::FUN_FONTSTRING_MEASURE_SUBSTRING);
    // The fs's wrap budget (see WrapBudget's scale note). Without one
    // (unresolved rect) nothing wraps, and the rendered width IS the
    // one-line width.
    const float budget = WrapBudget(fs);
    if (budget <= 0.0f) {
        Game::Lua::PushNumber(L, InternalToPixel(measure(fs, nullptr, text, 0)));
        return 1;
    }
    // The tooltip auto-size's own recipe (FUN_00530640): break positions from
    // the break-array computer, then the widest per-segment measure. Both
    // callees are icon-aware through their co-hooks, and a segment-final icon
    // reports its ink edge — so this is the rendered width, wrap and icons
    // included.
    int breaks[64];
    uint32_t segs = reinterpret_cast<BreakArray_t>(Offsets::FUN_FONTSTRING_BREAK_ARRAY)(
        fs, nullptr, text, budget, breaks, 64);
    if (segs > 64u)
        segs = 64u;
    if (segs <= 1u) {
        Game::Lua::PushNumber(L, InternalToPixel(measure(fs, nullptr, text, 0)));
        return 1;
    }
    int len = 0;
    while (len < 0x4000 && text[len] != '\0')
        ++len;
    float widest = 0.0f;
    for (uint32_t i = 0; i < segs; ++i) {
        const int start = breaks[i];
        int end = (i + 1 < segs) ? breaks[i + 1] : len;
        if (start < 0 || start >= end || end > len)
            continue;
        // A segment runs to the NEXT segment's start, which includes the
        // whitespace the wrap consumed — the render never draws it, and a
        // trailing space also flips the last real letter from ink to
        // full-advance treatment (+~1.5px, measured). Trim to what renders.
        while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\n' ||
                               text[end - 1] == '\r' || text[end - 1] == '\t'))
            --end;
        if (start >= end)
            continue;
        const float w = measure(fs, nullptr, text + start, end - start);
        if (w > widest)
            widest = w;
    }
    Game::Lua::PushNumber(L, InternalToPixel(widest));
    return 1;
}

int __fastcall Script_GetLineHeight(void *L) {
    void *fs = ResolveSelf(L, "Usage: fontstring:GetLineHeight()");
    if (fs == nullptr)
        return 0;
    const float h = reinterpret_cast<FsFontHeight_t>(Offsets::FUN_FONTSTRING_FONT_HEIGHT)(
        fs, nullptr, 1);
    Game::Lua::PushNumber(L, InternalToPixel(h));
    return 1;
}

int __fastcall Script_IsTruncated(void *L) {
    void *fs = ResolveSelf(L, "Usage: fontstring:IsTruncated()");
    if (fs == nullptr)
        return 0;
    // Retail computes this by running the display-text resolver and strcmp'ing
    // its result against the source (5.4.8 inner fn 0x0045E001). 1.12's resolver
    // needs the LAID-OUT rect (fs+0x64), which is zero until a render pass, so a
    // Lua-time re-call always sees an unbounded box and never truncates. Read the
    // render's OWN output instead: the built text node keeps a copy of exactly
    // the string it laid out (OFF_TEXT_NODE_TEXT) — the ellipsized "<prefix>..."
    // form on truncation, else the source verbatim. So this reflects the last
    // rendered layout (the fontstring must have drawn once), matching retail's
    // "displayed vs full" semantics.
    bool truncated = false;
    void *block = Game::Read<void *>(fs, Offsets::OFF_FONTSTRING_TEXT_BLOCK);
    const char *src = Game::Read<const char *>(fs, Offsets::OFF_FONTSTRING_TEXT);
    if (block != nullptr && src != nullptr) {
        void *node = Game::Read<void *>(block, Offsets::OFF_TEXTBLOCK_NODE);
        const char *shown =
            (node != nullptr) ? Game::Read<const char *>(node, Offsets::OFF_TEXT_NODE_TEXT)
                              : nullptr;
        if (shown != nullptr) {
            // A truncated display is exactly <source-prefix> + "...". Require the
            // "..." suffix, a length strictly shorter than the source, and a
            // matching byte-prefix. This rejects a STALE node — one left from a
            // previous, different text after a fresh SetText, not yet re-laid-out
            // — which would otherwise read as a truncation of the new text.
            const size_t nlen = std::strlen(shown);
            const size_t slen = std::strlen(src);
            if (nlen >= 3 && nlen < slen && shown[nlen - 3] == '.' &&
                shown[nlen - 2] == '.' && shown[nlen - 1] == '.' &&
                std::memcmp(shown, src, nlen - 3) == 0)
                truncated = true;
        }
    }
    Game::Lua::PushBool(L, truncated);
    return 1;
}

int __fastcall Script_SetMaxLines(void *L) {
    void *fs = ResolveSelf(L, "Usage: fontstring:SetMaxLines(maxLines)");
    if (fs == nullptr)
        return 0;
    // <= 0 or a non-number means "no cap" — the engine treats 0 as unlimited.
    int maxLines = 0;
    if (Game::Lua::IsNumber(L, 2)) {
        const double n = Game::Lua::ToNumber(L, 2);
        if (n > 0.0)
            maxLines = static_cast<int>(n);
    }
    if (maxLines != Game::Read<int>(fs, Offsets::OFF_FONTSTRING_MAX_LINES)) {
        Game::Ref<int>(fs, Offsets::OFF_FONTSTRING_MAX_LINES) = maxLines;
        InvalidateLayout(fs);
    }
    return 0;
}

int __fastcall Script_GetMaxLines(void *L) {
    void *fs = ResolveSelf(L, "Usage: fontstring:GetMaxLines()");
    if (fs == nullptr)
        return 0;
    Game::Lua::PushNumber(
        L, static_cast<double>(Game::Read<int>(fs, Offsets::OFF_FONTSTRING_MAX_LINES)));
    return 1;
}

int __fastcall Script_SetFormattedText(void *L) {
    void *fs = nullptr;
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_TABLE)
        fs = Game::Lua::ResolveFontString(L, 1, /*raiseError=*/false);
    if (fs == nullptr || !Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: fontstring:SetFormattedText(\"format\"[, ...])");
        return 0;
    }
    const int top = Game::Lua::GetTop(L);
    Game::Lua::CheckStack(L, top + 4);
    Game::Lua::PushCClosure(
        L, reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_LUA_STR_FORMAT), 0);
    for (int i = 2; i <= top; ++i)
        Game::Lua::PushValue(L, i);
    if (Game::Lua::PCall(L, top - 1, 1, 0) != 0) {
        const char *msg = Game::Lua::ToString(L, -1);
        Game::Lua::Error(L, "%s", (msg != nullptr) ? msg : "SetFormattedText: format failed");
        return 0;
    }
    const char *result = Game::Lua::ToString(L, -1);
    reinterpret_cast<FsSetText_t>(Offsets::FUN_FONTSTRING_SET_TEXT)(
        fs, nullptr, (result != nullptr) ? result : "", 0);
    Game::Lua::SetTop(L, top);
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"GetStringHeight", &Script_GetStringHeight},
    {"GetUnboundedStringWidth", &Script_GetUnboundedStringWidth},
    {"GetWrappedWidth", &Script_GetWrappedWidth},
    {"GetNumLines", &Script_GetNumLines},
    {"GetLineHeight", &Script_GetLineHeight},
    {"IsTruncated", &Script_IsTruncated},
    {"SetMaxLines", &Script_SetMaxLines},
    {"GetMaxLines", &Script_GetMaxLines},
    {"SetFormattedText", &Script_SetFormattedText},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FONTSTRING_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace FontString::Metrics
