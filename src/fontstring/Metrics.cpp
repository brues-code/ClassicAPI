// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// FontString:GetStringHeight() backport.
//
// 1.12's FontString method table (32 entries at 0x0087C1D8, registry context
// VAR_FONTSTRING_METHOD_REGISTRY) ships GetStringWidth but not GetStringHeight
// — Blizzard first shipped the height binding in 2.3.0 (verified present in
// our 3.3.5 catalog: pair at 0x0048DE90/0x0048DF00). The engine ALREADY has
// the internal getter, though:
// FUN_FONTSTRING_STRING_HEIGHT is wrap-aware (same wrap engine the render
// uses), returns lines×fontHeight + (lines−1)×spacing in anchor units, caches
// at fs+0x100, and yields 0 for empty text. This module is the thin Lua
// binding Blizzard never wrote: mirror Script_GetStringWidth (0x0079E510) —
// resolve self, call the internal getter, convert anchor units → UI pixels,
// push.
//
// No inline-icon adjustment here: icons never change the line count (the wrap
// decisions are icon-blind by design — see text/InlineTexture.cpp), so the
// internal height already matches what renders.

#include "Game.h"
#include "Offsets.h"

namespace FontString::Metrics {
namespace {

// FUN_FONTSTRING_STRING_HEIGHT — ECX = fs, no stack args, float in x87 ST0.
using StringHeightInternal_t = float(__fastcall *)(void *fs);

// Anchor units → UI pixels: `FUN_0041AE40(FUN_0041AD70() × DAT_007FFD68 × v)`
// = v × [VAR_UI_COORD_SCALE_DIV] × 1024 / [VAR_UI_COORD_SCALE_MUL] — the exact
// push chain Script_GetStringWidth uses (the inverse of
// Tooltip::LinePool::PixelToInternal).
double InternalToPixel(float v) {
    const float mul = *reinterpret_cast<const float *>(Offsets::VAR_UI_COORD_SCALE_MUL);
    const float div = *reinterpret_cast<const float *>(Offsets::VAR_UI_COORD_SCALE_DIV);
    if (mul == 0.0f)
        return 0.0;
    return static_cast<double>(v) * div * Offsets::UI_COORD_SCALE_UNIT / mul;
}

int __fastcall Script_GetStringHeight(void *L) {
    void *fs = nullptr;
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_TABLE)
        fs = Game::Lua::ResolveObject(L, 1);
    if (fs == nullptr) {
        Game::Lua::Error(L, "Usage: fontstring:GetStringHeight()");
        return 0;
    }
    const float h = reinterpret_cast<StringHeightInternal_t>(
        Offsets::FUN_FONTSTRING_STRING_HEIGHT)(fs);
    Game::Lua::PushNumber(L, InternalToPixel(h));
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"GetStringHeight", &Script_GetStringHeight},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FONTSTRING_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace FontString::Metrics
