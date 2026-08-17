// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// Texture:SetRotation(angle [, cx, cy]) — geometry-corner rotation.
//
// Modern WoW's SetRotation was verified against the 4.3.4 client
// (Script FUN_0052cb50): it rotates the four texture COORDINATES around a
// centre and feeds the 8-arg SetTexCoord. That clamps the corners of an
// off-axis square (the UVs leave [0,1]). 1.12 can't mirror it anyway — its
// CSimpleTexture::SetTexCoord (FUN_00770410) only takes a min/max UV rect.
//
// 1.12 has a better seam. A CSimpleTexture stores its drawn quad as FOUR
// independent corner positions {x,y,z} at region+0xD4 (OFF_SIMPLETEXTURE_CORNERS)
// plus four matching texcoords at +0x104. The draw enqueue FUN_00772fd0 passes
// region+0xD4 straight into the vertex batch as four free vertices — it never
// re-derives an axis-aligned rect. So rotating those four corner positions draws
// a truly rotated quad with NO corner clipping. The engine fills them
// axis-aligned from the layout rect via FUN_REGION_STORE_CORNERS (0x007705B0);
// we co-hook that store and, for a tracked region, overwrite the just-written
// corners with rotated ones. Because the store fires only when the corners
// change (layout resolve / SetTexCoord), a static rotation is written once and
// costs nothing per frame; a moving or resizing region re-rotates automatically.
//
// `angle` is radians, positive = counter-clockwise on screen (WoW UI space is
// Y-up). The optional (cx, cy) is a normalized rotation centre in [0,1] within
// the region rect, matching 4.3.4's rotation-point arg; default is the centre
// (0.5, 0.5).
//
// Known limitation (accepted): the rotation table keys on the raw region
// pointer. `/reload` and world→glue clear it (PrepareForReload), but a texture
// freed mid-session whose pointer the pool immediately reuses could carry a
// stale rotation onto the new texture until its next corner store. Rotation is
// an explicit addon opt-in on long-lived textures, so this is unlikely; the
// clean fix (evict on the CSimpleTexture destructor) is a later hardening.

#include "Game.h"
#include "Offsets.h"
#include "texture/Rotation.h"

#include <cmath>
#include <unordered_map>

namespace Texture::Rotation {
namespace {

struct Rot {
    float angle;
    float cxN;
    float cyN;
};

std::unordered_map<void *, Rot> g_rot;

// FUN_REGION_STORE_CORNERS — __thiscall(region, float rect[4]); the codebase's
// dummy-EDX form for a __thiscall with one stack arg.
using StoreCorners_t = void(__fastcall *)(void *region, void *edx, float *rect);
StoreCorners_t g_storeCornersOriginal = nullptr;

// FUN_REGION_GET_RECT — __thiscall(region+OFF_REGION_ANCHOR, float out[4]) -> int.
using GetRect_t = int(__fastcall *)(void *anchor, void *edx, float *outRect);
// FUN_REGION_TEXCOORD_CROP — __thiscall(region, float rect[4]) (rect in/out).
using Crop_t = void(__fastcall *)(void *region, void *edx, float *rect);

// Write the four rotated corner positions into region+0xD4, given the
// axis-aligned edges. Corner order matches the engine store: BL, TL, BR, TR.
void WriteRotatedCorners(void *region, float left, float right, float top,
                         float bottom, float angle, float cxN, float cyN) {
    const float cx = left + cxN * (right - left);
    const float cy = bottom + cyN * (top - bottom);
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const float xs[4] = {left, left, right, right};
    const float ys[4] = {bottom, top, bottom, top};
    float *base = reinterpret_cast<float *>(
        reinterpret_cast<char *>(region) + Offsets::OFF_SIMPLETEXTURE_CORNERS);
    for (int i = 0; i < 4; ++i) {
        const float dx = xs[i] - cx;
        const float dy = ys[i] - cy;
        base[i * 3 + 0] = cx + dx * c - dy * s; // x
        base[i * 3 + 1] = cy + dx * s + dy * c; // y
        // z (base[i*3 + 2]) is left at 0 — the store/ctor already zeroed it.
    }
}

// Immediate apply from the region's current layout rect. Idempotent: it always
// derives the axis-aligned edges from the engine rect, never from the (possibly
// already-rotated) corners, so calling SetRotation twice never compounds.
// Returns false when the region isn't laid out yet — the corner-store hook then
// applies the rotation on the first real store.
bool ApplyFromRect(void *region, float angle, float cxN, float cyN) {
    float rect[4];
    void *anchor =
        reinterpret_cast<char *>(region) + Offsets::OFF_REGION_ANCHOR;
    if (reinterpret_cast<GetRect_t>(Offsets::FUN_REGION_GET_RECT)(anchor, nullptr,
                                                                  rect) == 0)
        return false;
    reinterpret_cast<Crop_t>(Offsets::FUN_REGION_TEXCOORD_CROP)(region, nullptr,
                                                                rect);
    // rect = {top, left, bottom, right}
    WriteRotatedCorners(region, rect[1], rect[3], rect[0], rect[2], angle, cxN,
                        cyN);
    return true;
}

// Co-hook on the corner store: the engine writes axis-aligned corners, then we
// overwrite them with rotated ones for a tracked region. Reading the edges back
// from the corners the original just wrote keeps this rotation-source-agnostic
// (SetTexCoord's cropped rect vs. the raw layout rect all resolve here).
void __fastcall StoreCorners_h(void *region, void *edx, float *rect) {
    g_storeCornersOriginal(region, edx, rect);
    if (g_rot.empty())
        return;
    auto it = g_rot.find(region);
    if (it == g_rot.end() || it->second.angle == 0.0f)
        return;
    const float *base = reinterpret_cast<const float *>(
        reinterpret_cast<char *>(region) + Offsets::OFF_SIMPLETEXTURE_CORNERS);
    const float left = base[0];   // corner0 BL.x
    const float bottom = base[1]; // corner0 BL.y
    const float top = base[4];    // corner1 TL.y
    const float right = base[6];  // corner2 BR.x
    WriteRotatedCorners(region, left, right, top, bottom, it->second.angle,
                        it->second.cxN, it->second.cyN);
}

int __fastcall Script_SetRotation(void *L) {
    void *tex = nullptr;
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_TABLE)
        tex = Game::Lua::ResolveObject(L, 1);
    if (tex == nullptr || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: texture:SetRotation(angle [, cx, cy])");
        return 0;
    }
    const float angle = static_cast<float>(Game::Lua::ToNumber(L, 2));
    float cxN = 0.5f;
    float cyN = 0.5f;
    if (Game::Lua::IsNumber(L, 3) && Game::Lua::IsNumber(L, 4)) {
        cxN = static_cast<float>(Game::Lua::ToNumber(L, 3));
        cyN = static_cast<float>(Game::Lua::ToNumber(L, 4));
    }
    if (angle == 0.0f) {
        // Un-rotate: drop the entry so future stores stay axis-aligned, and
        // restore the corners now (cos 0 / sin 0 rebuilds the axis-aligned quad).
        g_rot.erase(tex);
        ApplyFromRect(tex, 0.0f, cxN, cyN);
    } else {
        g_rot[tex] = {angle, cxN, cyN};
        ApplyFromRect(tex, angle, cxN, cyN); // immediate; the hook re-applies later
    }
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetRotation", &Script_SetRotation},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_TEXTURE_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::HookAutoRegister _hookStoreCorners{
    Offsets::FUN_REGION_STORE_CORNERS, reinterpret_cast<void *>(&StoreCorners_h),
    reinterpret_cast<void **>(&g_storeCornersOriginal)};

} // namespace

void PrepareForReload() { g_rot.clear(); }

} // namespace Texture::Rotation
