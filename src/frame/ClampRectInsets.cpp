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

// `frame:SetClampRectInsets(left, right, top, bottom)` and
// `frame:GetClampRectInsets()`.
//
// A frame clamped to the screen (SetClampedToScreen) can't be moved or
// dragged past the screen's edges. Later clients let it name a different
// rectangle to keep on screen: the frame's own rect with each edge moved by
// its inset, in the frame's units and screen directions (a negative left or
// bottom, or a positive right or top, grows the rect; the opposite lets that
// much of the frame go off screen). 3.3.5's WorldMapFrame keeps clear of the
// experience bars with (0, 0, 0, -60); its chat frames keep their tabs and
// edit box on screen with (-35, 35, 26, -50). Vanilla has the clamp but no
// insets. Zygor Guides Viewer 2.0 sets them in its main frame's OnLoad and
// grows its small anchor frame's rect over the whole viewer.
//
// The engine clamps inside the rect calculation (FUN_LAYOUT_CALC_RECT): it
// resolves the frame's edges and, for a clamped frame, slides the rect onto
// the screen. For a frame with insets the hook takes the unclamped rect, grows
// it by the insets, slides that onto the screen with the engine's own steps
// and order, and moves the frame's rect by the same amount. Every other
// frame goes through the engine's clamp untouched.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <unordered_map>

namespace Frame::ClampRectInsets {

namespace {

struct Insets {
    double left = 0.0, right = 0.0, top = 0.0, bottom = 0.0;
};

// Keyed by the frame object. Frames live until the UI is torn down, and a
// /reload rebuilds every one of them, so the map is cleared on reload.
std::unordered_map<void *, Insets> g_insets;

// rect: bottom, left, top, right, in layout units.
using CalcRect_t = int(__thiscall *)(void *layout, float *rect);
CalcRect_t g_origCalcRect = nullptr;

using Invalidate_t = void(__thiscall *)(void *layout, int arg);
using UnitScale_t = float(__cdecl *)();
using ScreenExtent_t = float(__stdcall *)(float scale);

void *LayoutOf(void *frame) { return static_cast<uint8_t *>(frame) + Offsets::OFF_REGION_LAYOUT; }
void *FrameOf(void *layout) { return static_cast<uint8_t *>(layout) - Offsets::OFF_REGION_LAYOUT; }

// A length in the frame's own units (what GetLeft returns) in layout units:
// the inverse of Script GetLeft's conversion.
float ToLayoutUnits(double length, void *frame) {
    const float effScale = Game::Read<float>(frame, Offsets::OFF_REGION_EFFECTIVE_SCALE);
    const float unit = reinterpret_cast<UnitScale_t>(Offsets::FUN_UI_UNIT_SCALE)();
    const float perUnit = Game::Read<float>(Offsets::VAR_UI_UNITS_PER_SCALE);
    const float width = reinterpret_cast<ScreenExtent_t>(Offsets::FUN_SCREEN_WIDTH)(1.0f);
    if (unit == 0.0f || perUnit == 0.0f)
        return 0.0f;
    return static_cast<float>(length) * effScale * width / (unit * perUnit);
}

int __fastcall CalcRect_h(void *layout, void * /*edx*/, float *rect) {
    const uint32_t flags = Game::Read<uint32_t>(layout, Offsets::OFF_LAYOUT_FLAGS);
    if ((flags & Offsets::LAYOUT_FLAG_CLAMPED) == 0 || g_insets.empty())
        return g_origCalcRect(layout, rect);
    void *frame = FrameOf(layout);
    const auto it = g_insets.find(frame);
    if (it == g_insets.end())
        return g_origCalcRect(layout, rect);

    // The frame's own rect, before any clamping.
    Game::Ref<uint32_t>(layout, Offsets::OFF_LAYOUT_FLAGS) = flags & ~static_cast<uint32_t>(Offsets::LAYOUT_FLAG_CLAMPED);
    const int resolved = g_origCalcRect(layout, rect);
    Game::Ref<uint32_t>(layout, Offsets::OFF_LAYOUT_FLAGS) = flags;
    if (!resolved)
        return resolved;

    const Insets &in = it->second;
    float bottom = rect[0] + ToLayoutUnits(in.bottom, frame);
    float left = rect[1] + ToLayoutUnits(in.left, frame);
    float top = rect[2] + ToLayoutUnits(in.top, frame);
    float right = rect[3] + ToLayoutUnits(in.right, frame);

    // The engine's clamp, step for step, on the grown rect.
    float dx = 0.0f, dy = 0.0f;
    if (left < 0.0f) {
        dx -= left;
        right -= left;
        left = 0.0f;
    }
    if (bottom < 0.0f) {
        dy -= bottom;
        top -= bottom;
        bottom = 0.0f;
    }
    const float width = reinterpret_cast<ScreenExtent_t>(Offsets::FUN_SCREEN_WIDTH)(1.0f);
    if (right > width) {
        dx -= right - width;
        left -= right - width;
        right = width;
    }
    const float height = reinterpret_cast<ScreenExtent_t>(Offsets::FUN_SCREEN_HEIGHT)(1.0f);
    if (top > height) {
        dy -= top - height;
        bottom -= top - height;
        top = height;
    }

    rect[0] += dy;
    rect[1] += dx;
    rect[2] += dy;
    rect[3] += dx;
    return resolved;
}

const Game::HookAutoRegister _hook{Offsets::FUN_LAYOUT_CALC_RECT, reinterpret_cast<void *>(&CalcRect_h),
                                   reinterpret_cast<void **>(&g_origCalcRect)};

int __fastcall Script_SetClampRectInsets(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2) || !Game::Lua::IsNumber(L, 3) ||
        !Game::Lua::IsNumber(L, 4) || !Game::Lua::IsNumber(L, 5)) {
        Game::Lua::Error(L, "Usage: frame:SetClampRectInsets(left, right, top, bottom)");
        return 0;
    }
    Insets in;
    in.left = Game::Lua::ToNumber(L, 2);
    in.right = Game::Lua::ToNumber(L, 3);
    in.top = Game::Lua::ToNumber(L, 4);
    in.bottom = Game::Lua::ToNumber(L, 5);
    if (in.left == 0.0 && in.right == 0.0 && in.top == 0.0 && in.bottom == 0.0)
        g_insets.erase(frame);
    else
        g_insets[frame] = in;
    // Re-lay the frame out, as SetClampedToScreen does after changing the clamp.
    reinterpret_cast<Invalidate_t>(Offsets::FUN_FONTSTRING_LAYOUT_INVALIDATE)(LayoutOf(frame), 0);
    return 0;
}

int __fastcall Script_GetClampRectInsets(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    const auto it = g_insets.find(frame);
    const Insets in = it != g_insets.end() ? it->second : Insets{};
    Game::Lua::PushNumber(L, in.left);
    Game::Lua::PushNumber(L, in.right);
    Game::Lua::PushNumber(L, in.top);
    Game::Lua::PushNumber(L, in.bottom);
    return 4;
}

void PrepareForReload() { g_insets.clear(); }

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetClampRectInsets", &Script_SetClampRectInsets},
    {"GetClampRectInsets", &Script_GetClampRectInsets},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::ReloadAutoRegister _reload{&PrepareForReload};

} // namespace

} // namespace Frame::ClampRectInsets
