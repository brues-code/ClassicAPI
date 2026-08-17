// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// Texture corner transforms — Texture:SetRotation + Texture:SetVertexOffset.
//
// Both backports move the four corner POSITIONS of the drawn quad, stored at
// region+0xD4 (OFF_SIMPLETEXTURE_CORNERS). The draw enqueue FUN_00772fd0 hands
// those four vertices straight to the vertex batch, so moving them warps the
// on-screen quad — no stock 1.12 function exposes this. SetRotation turns the
// corners around a pivot; SetVertexOffset nudges one corner directly. They are
// one primitive: a per-region transform of {rotation, four vertex offsets} that
// both methods write into and that COMPOSE (rotate about the pivot first, then
// add the offsets), matching retail.
//
// The engine re-derives axis-aligned corners from the layout rect on every
// resolve via FUN_REGION_STORE_CORNERS (0x007705B0). We co-hook that store and,
// for a transformed region, overwrite the just-written corners. So a static
// transform is written once and costs nothing per frame; a moving/resizing
// region re-transforms automatically.
//
// Units: rotation is scale-invariant, but a vertex offset has a magnitude, so
// the Lua offset (UI coordinate units, like SetPoint's x/y) is converted to the
// engine's internal layout space with the same factor Tooltip::LinePool uses for
// engine SetPoint (px * MUL / (DIV * 1024)). +offsetY is up (WoW UI space is
// Y-up — the same convention that makes a positive rotation angle turn CCW).
//
// Corner order at +0xD4 is BL, TL, BR, TR. Retail's vertex indices map onto
// those slots via VertexSlot(); the UPPER_LEFT_VERTEX / … globals are published
// so addons pass names, not magic numbers.
//
// Known limitation (accepted): the table keys on the raw region pointer. `/reload`
// and world→glue clear it (PrepareForReload), but a texture freed mid-session
// whose pointer the pool immediately reuses could carry a stale transform until
// its next corner store. Transforms are an explicit addon opt-in on long-lived
// textures, so this is unlikely; the clean fix (evict on the CSimpleTexture
// destructor) is a later hardening.

#include "Game.h"
#include "Offsets.h"
#include "texture/Transform.h"

#include <cmath>
#include <unordered_map>

namespace Texture::Transform {
namespace {

struct Xf {
    float angle = 0.0f;              // radians, 0 = no rotation
    float cxN = 0.5f, cyN = 0.5f;    // rotation pivot, normalized in the rect
    float offX[4] = {0, 0, 0, 0};    // per-corner offset, internal units, BL/TL/BR/TR
    float offY[4] = {0, 0, 0, 0};

    bool active() const {
        if (angle != 0.0f)
            return true;
        for (int i = 0; i < 4; ++i)
            if (offX[i] != 0.0f || offY[i] != 0.0f)
                return true;
        return false;
    }
};

std::unordered_map<void *, Xf> g_xf;

// FUN_REGION_STORE_CORNERS — __thiscall(region, float rect[4]); dummy-EDX form.
using StoreCorners_t = void(__fastcall *)(void *region, void *edx, float *rect);
StoreCorners_t g_storeOriginal = nullptr;

// FUN_REGION_GET_RECT — __thiscall(region+OFF_REGION_ANCHOR, float out[4]) -> int.
using GetRect_t = int(__fastcall *)(void *anchor, void *edx, float *outRect);
// FUN_REGION_TEXCOORD_CROP — __thiscall(region, float rect[4]) (rect in/out).
using Crop_t = void(__fastcall *)(void *region, void *edx, float *rect);

// UI coordinate units → internal layout space (the same conversion
// Tooltip::LinePool applies before calling engine SetPoint; inverse of
// FontString::Metrics's InternalToPixel).
float PixelToInternal(float px) {
    const float mul = *reinterpret_cast<const float *>(Offsets::VAR_UI_COORD_SCALE_MUL);
    const float div = *reinterpret_cast<const float *>(Offsets::VAR_UI_COORD_SCALE_DIV);
    if (div == 0.0f)
        return 0.0f;
    return px * mul / (div * Offsets::UI_COORD_SCALE_UNIT);
}
float InternalToPixel(float v) {
    const float mul = *reinterpret_cast<const float *>(Offsets::VAR_UI_COORD_SCALE_MUL);
    const float div = *reinterpret_cast<const float *>(Offsets::VAR_UI_COORD_SCALE_DIV);
    if (mul == 0.0f)
        return 0.0f;
    return v * div * Offsets::UI_COORD_SCALE_UNIT / mul;
}

// Retail vertex index → our +0xD4 corner slot (BL=0, TL=1, BR=2, TR=3). The
// index values (UPPER_LEFT_VERTEX = 1, …) are the FrameXML-style globals defined
// in the embedded addon's Util/Constants.lua; keep the two in sync. -1 = unknown.
int VertexSlot(int vertexIndex) {
    switch (vertexIndex) {
    case 1: return 1; // UPPER_LEFT_VERTEX  → TL
    case 2: return 0; // LOWER_LEFT_VERTEX  → BL
    case 3: return 3; // UPPER_RIGHT_VERTEX → TR
    case 4: return 2; // LOWER_RIGHT_VERTEX → BR
    default: return -1;
    }
}

// Given the region's axis-aligned edges, compute the four transformed corner
// positions (rotate about the pivot, then add the per-corner offsets) and write
// them to region+0xD4. Corner order: BL, TL, BR, TR.
void WriteCorners(void *region, float left, float right, float top, float bottom,
                  const Xf &xf) {
    const float cx = left + xf.cxN * (right - left);
    const float cy = bottom + xf.cyN * (top - bottom);
    const float c = std::cos(xf.angle);
    const float s = std::sin(xf.angle);
    const float xs[4] = {left, left, right, right};
    const float ys[4] = {bottom, top, bottom, top};
    float *base = reinterpret_cast<float *>(
        reinterpret_cast<char *>(region) + Offsets::OFF_SIMPLETEXTURE_CORNERS);
    for (int i = 0; i < 4; ++i) {
        const float dx = xs[i] - cx;
        const float dy = ys[i] - cy;
        base[i * 3 + 0] = cx + dx * c - dy * s + xf.offX[i]; // x
        base[i * 3 + 1] = cy + dx * s + dy * c + xf.offY[i]; // y
        // z (base[i*3 + 2]) is left at 0 — the store/ctor already zeroed it.
    }
}

// Immediate apply from the region's current layout rect. Idempotent: always
// derives the axis-aligned edges from the engine rect, never from the (possibly
// already-transformed) corners, so repeated calls never compound. Returns false
// when the region isn't laid out yet — the corner-store hook then applies on the
// first real store. An identity transform restores the plain axis-aligned quad.
bool ApplyFromRect(void *region, const Xf &xf) {
    float rect[4];
    void *anchor = reinterpret_cast<char *>(region) + Offsets::OFF_REGION_ANCHOR;
    if (reinterpret_cast<GetRect_t>(Offsets::FUN_REGION_GET_RECT)(anchor, nullptr,
                                                                  rect) == 0)
        return false;
    reinterpret_cast<Crop_t>(Offsets::FUN_REGION_TEXCOORD_CROP)(region, nullptr, rect);
    // rect = {top, left, bottom, right}
    WriteCorners(region, rect[1], rect[3], rect[0], rect[2], xf);
    return true;
}

Xf Current(void *region) {
    auto it = g_xf.find(region);
    return it != g_xf.end() ? it->second : Xf{};
}

// Store the transform (or drop it when it became an identity) and apply now.
void UpdateAndApply(void *region, const Xf &xf) {
    if (xf.active())
        g_xf[region] = xf;
    else
        g_xf.erase(region); // future stores stay axis-aligned
    ApplyFromRect(region, xf); // identity xf restores the plain quad
}

// Co-hook on the corner store: the engine writes axis-aligned corners, then we
// overwrite them with the transformed ones for a tracked region. Reading the
// edges back from the corners the original just wrote keeps this
// transform-source-agnostic (SetTexCoord's cropped rect vs. the raw layout rect
// both resolve here).
void __fastcall StoreCorners_h(void *region, void *edx, float *rect) {
    g_storeOriginal(region, edx, rect);
    if (g_xf.empty())
        return;
    auto it = g_xf.find(region);
    if (it == g_xf.end() || !it->second.active())
        return;
    const float *base = reinterpret_cast<const float *>(
        reinterpret_cast<char *>(region) + Offsets::OFF_SIMPLETEXTURE_CORNERS);
    const float left = base[0];   // corner0 BL.x
    const float bottom = base[1]; // corner0 BL.y
    const float top = base[4];    // corner1 TL.y
    const float right = base[6];  // corner2 BR.x
    WriteCorners(region, left, right, top, bottom, it->second);
}

int __fastcall Script_SetRotation(void *L) {
    void *tex = nullptr;
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_TABLE)
        tex = Game::Lua::ResolveObject(L, 1);
    if (tex == nullptr || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: texture:SetRotation(angle [, cx, cy])");
        return 0;
    }
    Xf xf = Current(tex);
    xf.angle = static_cast<float>(Game::Lua::ToNumber(L, 2));
    if (Game::Lua::IsNumber(L, 3) && Game::Lua::IsNumber(L, 4)) {
        xf.cxN = static_cast<float>(Game::Lua::ToNumber(L, 3));
        xf.cyN = static_cast<float>(Game::Lua::ToNumber(L, 4));
    }
    UpdateAndApply(tex, xf);
    return 0;
}

int __fastcall Script_GetRotation(void *L) {
    void *tex = nullptr;
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_TABLE)
        tex = Game::Lua::ResolveObject(L, 1);
    if (tex == nullptr) {
        Game::Lua::Error(L, "Usage: texture:GetRotation()");
        return 0;
    }
    Game::Lua::PushNumber(L, Current(tex).angle);
    return 1;
}

int __fastcall Script_SetVertexOffset(void *L) {
    void *tex = nullptr;
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_TABLE)
        tex = Game::Lua::ResolveObject(L, 1);
    const int slot = Game::Lua::IsNumber(L, 2)
                         ? VertexSlot(static_cast<int>(Game::Lua::ToNumber(L, 2)))
                         : -1;
    if (tex == nullptr || slot < 0) {
        Game::Lua::Error(
            L, "Usage: texture:SetVertexOffset(vertexIndex, offsetX, offsetY)");
        return 0;
    }
    const float ox = Game::Lua::IsNumber(L, 3)
                         ? static_cast<float>(Game::Lua::ToNumber(L, 3))
                         : 0.0f;
    const float oy = Game::Lua::IsNumber(L, 4)
                         ? static_cast<float>(Game::Lua::ToNumber(L, 4))
                         : 0.0f;
    Xf xf = Current(tex);
    xf.offX[slot] = PixelToInternal(ox);
    xf.offY[slot] = PixelToInternal(oy);
    UpdateAndApply(tex, xf);
    return 0;
}

int __fastcall Script_GetVertexOffset(void *L) {
    void *tex = nullptr;
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_TABLE)
        tex = Game::Lua::ResolveObject(L, 1);
    const int slot = Game::Lua::IsNumber(L, 2)
                         ? VertexSlot(static_cast<int>(Game::Lua::ToNumber(L, 2)))
                         : -1;
    if (tex == nullptr || slot < 0) {
        Game::Lua::Error(L, "Usage: texture:GetVertexOffset(vertexIndex)");
        return 0;
    }
    const Xf xf = Current(tex);
    Game::Lua::PushNumber(L, InternalToPixel(xf.offX[slot]));
    Game::Lua::PushNumber(L, InternalToPixel(xf.offY[slot]));
    return 2;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetRotation", &Script_SetRotation},
    {"GetRotation", &Script_GetRotation},
    {"SetVertexOffset", &Script_SetVertexOffset},
    {"GetVertexOffset", &Script_GetVertexOffset},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_TEXTURE_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
    // The UPPER_LEFT_VERTEX / … constants that VertexSlot maps are defined
    // FrameXML-style in the embedded addon's Util/Constants.lua, not here.
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::HookAutoRegister _hookStoreCorners{
    Offsets::FUN_REGION_STORE_CORNERS, reinterpret_cast<void *>(&StoreCorners_h),
    reinterpret_cast<void **>(&g_storeOriginal)};

} // namespace

void PrepareForReload() { g_xf.clear(); }

} // namespace Texture::Transform
