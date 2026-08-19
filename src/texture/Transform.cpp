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
// A transform composes with the 8-argument corner form of SetTexCoord, which is
// how a caller warps the SHAPE and says what each moved corner SAMPLES in one
// paired write (WeakAuras' radial wedges are built this way). That only holds
// because the offsets are measured against the same rect the engine's own store
// uses — the untouched layout rect, unless the region opted into
// SetTexCoordModifiesRect — see the note in ApplyFromRect.
//
// Units: rotation is scale-invariant, but a vertex offset has a magnitude. The
// Lua offset is in the texture's LOCAL pixels (like SetPoint's x/y), stored
// raw, and converted to the engine's internal layout space inside WriteCorners
// — see the y-down and effective-scale notes there. The retail contract holds
// at the API surface: +offsetY moves a corner up on screen, a positive angle
// turns counter-clockwise on screen.
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
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Texture::Transform {
namespace {

struct Xf {
    float angle = 0.0f;              // radians, 0 = no rotation
    float cxN = 0.5f, cyN = 0.5f;    // rotation pivot, normalized in the rect
    float offX[4] = {0, 0, 0, 0};    // per-corner offset, CALLER's local px
    float offY[4] = {0, 0, 0, 0};    // (+y up), BL/TL/BR/TR; converted at write

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

// 1024-normalized UI pixels → internal layout (anchor) units — the same
// conversion Tooltip::LinePool applies before calling engine SetPoint. A
// region's LOCAL pixels must be folded through its effective scale (+0x7C)
// before this factor applies; WriteCorners does that.
float PixelToInternal(float px) {
    const float mul = *reinterpret_cast<const float *>(Offsets::VAR_UI_COORD_SCALE_MUL);
    const float div = *reinterpret_cast<const float *>(Offsets::VAR_UI_COORD_SCALE_DIV);
    if (div == 0.0f)
        return 0.0f;
    return px * mul / (div * Offsets::UI_COORD_SCALE_UNIT);
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
//
// The corner/anchor space is Y-DOWN — a box's rect reads top NUMERICALLY
// SMALLER than bottom (verified in-game via the GetCorners probe: t=0.222,
// b=0.277). Two consequences the math below encodes:
//   - the retail contract is "positive angle = counter-clockwise ON SCREEN",
//     which lands CCW through the mirrored-partner store below with the
//     y-down-transposed rotation matrix (settled by a rotated-bar fixture:
//     the standard matrix rendered clockwise);
//   - the retail contract is "+offsetY = up on screen", which is numerically
//     NEGATIVE here.
// Offsets are stored in the caller's LOCAL pixels and converted at write time:
// PixelToInternal maps 1024-normalized UI pixels to anchor units, so the
// region's live effective-scale chain (+0x7C) folds local px into that space
// first. Without it a region scaled 0.9 got every offset 1/0.9 too large
// (verified in-game: a 24px offset landed at 26.7px). Reading the scale at
// write time keeps offsets correct across later scale changes.
void WriteCorners(void *region, float left, float right, float top, float bottom,
                  const Xf &xf) {
    const float cx = left + xf.cxN * (right - left);
    const float cy = bottom + xf.cyN * (top - bottom);
    const float c = std::cos(xf.angle);
    const float s = std::sin(xf.angle);
    float scale = *reinterpret_cast<const float *>(
        reinterpret_cast<const char *>(region) + Offsets::OFF_REGION_EFFECTIVE_SCALE);
    if (!(scale > 0.0f))
        scale = 1.0f;
    const float xs[4] = {left, left, right, right};
    const float ys[4] = {bottom, top, bottom, top};
    float *base = reinterpret_cast<float *>(
        reinterpret_cast<char *>(region) + Offsets::OFF_SIMPLETEXTURE_CORNERS);
    // Compute each corner's TARGET rect-space position first.
    float tx[4], ty[4];
    for (int i = 0; i < 4; ++i) {
        const float dx = xs[i] - cx;
        const float dy = ys[i] - cy;
        tx[i] = cx + dx * c + dy * s + PixelToInternal(xf.offX[i] * scale);
        ty[i] = cy - dx * s + dy * c - PixelToInternal(xf.offY[i] * scale);
    }
    // The draw consumes this array with y INVERTED about the rect's horizontal
    // midline (a vertex stored at rect-space y renders at (top+bottom)-y), and
    // it CULLS a quad whose slot parity flips (mirroring values in place made
    // every fixture vanish -- inverted winding). Both established empirically
    // via the 2026-08-18 texprobe fixture series: offset quads rendered as
    // exact y-mirrors of their stored corners, the engine's own axis-aligned
    // stores being mirror-invariant as a point set is what kept it invisible
    // for plain rects and rotated squares. So each corner's mirrored data is
    // written into its VERTICAL PARTNER's slot (BL<->TL, BR<->TR): positions
    // land on target after the draw's flip, and the slot y-parity stays the
    // engine's, keeping the winding it draws. For an axis-aligned rect this
    // degenerates to exactly what the engine itself writes.
    const float mid = top + bottom;
    for (int i = 0; i < 4; ++i) {
        const int p = i ^ 1; // vertical partner slot
        base[i * 3 + 0] = tx[p];
        base[i * 3 + 1] = mid - ty[p];
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
    // Apply the texcoord crop (FUN_REGION_TEXCOORD_CROP) exactly as the
    // engine's own store paths do: gated on the region's
    // SetTexCoordModifiesRect flag (+0x124), which is off by default. With the
    // flag clear, a plain texture with a partial or corner-form texcoord draws
    // at its FULL size — verified in-game against .25-span, inset, sheared and
    // wedge-shaped corner texcoords, every one of which left the drawn quad
    // untouched — and the layout-resolve store (FUN_00770670) feeds the raw
    // anchor rect to the corner store. Calling the crop UNCONDITIONALLY here
    // bypassed that gate, so a region carrying both a transform and a partial
    // texcoord had its offsets measured against a shrunken rect the engine
    // never used: WeakAuras-style radial wedges, whose texcoords and vertex
    // offsets are one paired corner move, collapsed onto the rect's edge (a
    // 64px wedge that should have spanned x = 0.5..1 of its region landed
    // entirely on x = 0.5). Keeping the flag-gated call matches the engine in
    // BOTH states, so this immediate apply and the corner-store co-hook below
    // (which re-derives edges from whatever rect the engine stored) can never
    // disagree about the base rect.
    if (*reinterpret_cast<const int *>(reinterpret_cast<char *>(region) +
                                       Offsets::OFF_REGION_TEXCOORD_MODIFIES_RECT) != 0)
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
        Game::Lua::Error(L, "Usage: region:GetRotation()");
        return 0;
    }
    Game::Lua::PushNumber(L, Current(tex).angle);
    return 1;
}

// --- FontString glyph-vert rotation ---------------------------------------
//
// A fontstring's glyph verts are re-baked (axis-aligned) by the engine only on a
// TEXT/layout change — never on an angle change. So SetRotation can't lean on a
// rebuild the way the texture path leans on the corner-store hook; it must
// re-rotate the LIVE verts immediately (else a spin rotates once and sticks). We
// keep an axis-aligned baseline per rotated fontstring and, on each apply,
// restore→rotate. The baseline is (re)captured on every fresh bake (the
// DrawBuilder co-hook), so a text change keeps it correct. Keyed by fs (same
// pointer-reuse caveat as g_xf); cleared on reload.
std::unordered_map<void *, std::vector<float>> g_fsBaseline;

constexpr int kVertStride = Offsets::TEXT_VERT_STRIDE / 4; // floats per vertex

// Calls fn(float *xy) for every baked glyph vertex across the node's ≤8 font-page
// buffers (xy[0]=x, xy[1]=y, mutable in place). Returns the vertex count visited.
template <class Fn>
int ForEachVert(uint8_t *n, Fn fn) {
    int visited = 0;
    for (int page = 0; page < Offsets::TEXT_NODE_PAGE_COUNT; ++page) {
        auto *buf = *reinterpret_cast<uint8_t *const *>(
            n + Offsets::OFF_TEXT_NODE_PAGE_BUFFERS + page * 4);
        if (buf == nullptr)
            continue;
        const int count =
            *reinterpret_cast<const int *>(buf + Offsets::OFF_TEXT_PAGE_VERT_COUNT);
        auto *verts = *reinterpret_cast<float *const *>(buf + Offsets::OFF_TEXT_PAGE_VERTS);
        if (verts == nullptr || count <= 0)
            continue;
        for (int i = 0; i < count; ++i) {
            fn(verts + i * kVertStride);
            ++visited;
        }
    }
    return visited;
}

int NodeVertCount(uint8_t *n) {
    return ForEachVert(n, [](float *) {});
}

// Rotates every vertex (x, y) about the vertex bounding-box pivot. Same matrix as
// WriteCorners — positive angle = CCW.
void RotateVerts(uint8_t *n, float angle, float cxN, float cyN) {
    float minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool any = false;
    ForEachVert(n, [&](float *v) {
        if (!any) {
            minX = maxX = v[0];
            minY = maxY = v[1];
            any = true;
        } else {
            if (v[0] < minX) minX = v[0];
            else if (v[0] > maxX) maxX = v[0];
            if (v[1] < minY) minY = v[1];
            else if (v[1] > maxY) maxY = v[1];
        }
    });
    if (!any)
        return;
    const float cx = minX + cxN * (maxX - minX);
    const float cy = minY + cyN * (maxY - minY);
    const float c = std::cos(angle);
    // Standard matrix: a positive angle renders COUNTER-CLOCKWISE on screen —
    // the retail contract, and matching Texture:SetRotation. Confirmed by direct
    // comparison against the (retail-verified) texture rotation: both turn the
    // same way with the same-signed angle. (Node-local glyph-vert Y handedness
    // is NOT determinable from GetGlyphVerts alone — an earlier negate-the-sine
    // attempt off that dump inverted the direction; the visual comparison is the
    // arbiter.)
    const float s = std::sin(angle);
    ForEachVert(n, [&](float *v) {
        const float dx = v[0] - cx;
        const float dy = v[1] - cy;
        v[0] = cx + dx * c - dy * s;
        v[1] = cy + dx * s + dy * c;
    });
}

void CaptureBaseline(uint8_t *n, std::vector<float> &out) {
    out.clear();
    ForEachVert(n, [&](float *v) {
        out.push_back(v[0]);
        out.push_back(v[1]);
    });
}

// Restores verts from a baseline. No-op + false when the current vert count no
// longer matches (a text change re-baked the node) — the DrawBuilder co-hook
// refreshes the baseline on that fresh bake, so the next apply lines up.
bool RestoreBaseline(uint8_t *n, const std::vector<float> &base) {
    if (static_cast<size_t>(NodeVertCount(n)) * 2 != base.size())
        return false;
    size_t k = 0;
    ForEachVert(n, [&](float *v) {
        v[0] = base[k];
        v[1] = base[k + 1];
        k += 2;
    });
    return true;
}

// The CSimpleFontString's current live text node (fs+0xF8 → block, block+8), or
// null when the text isn't laid out yet.
void *FontStringNode(void *fs) {
    void *block = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(fs) +
                                             Offsets::OFF_FONTSTRING_TEXT_BLOCK);
    if (block == nullptr)
        return nullptr;
    return *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(block) +
                                      Offsets::OFF_TEXTBLOCK_NODE);
}

// Applies the fontstring's stored rotation to `node`'s verts, starting from an
// axis-aligned state. `fresh` = true when the emitter JUST baked the verts (they
// are axis-aligned → capture the baseline); false for an immediate SetRotation
// (restore the baseline first). Invariant: g_fsBaseline[fs] holds the
// axis-aligned verts iff the live verts are rotated.
void ApplyToNode(void *node, void *fs, bool fresh) {
    if (node == nullptr || fs == nullptr)
        return;
    auto *n = reinterpret_cast<uint8_t *>(node);
    if (NodeVertCount(n) <= 0)
        return;
    auto xit = g_xf.find(fs);
    const bool rotated = (xit != g_xf.end() && xit->second.angle != 0.0f);

    if (fresh) {
        if (rotated) {
            CaptureBaseline(n, g_fsBaseline[fs]);
            RotateVerts(n, xit->second.angle, xit->second.cxN, xit->second.cyN);
        } else {
            g_fsBaseline.erase(fs);
        }
        return;
    }
    // Immediate (SetRotation): return to axis-aligned, then rotate.
    auto bit = g_fsBaseline.find(fs);
    if (bit != g_fsBaseline.end()) {
        if (!RestoreBaseline(n, bit->second))
            return; // node re-baked out from under us; the fresh path will fix it
    } else {
        CaptureBaseline(n, g_fsBaseline[fs]); // first rotation: current IS axis-aligned
    }
    if (rotated) {
        RotateVerts(n, xit->second.angle, xit->second.cxN, xit->second.cyN);
    } else {
        g_fsBaseline.erase(fs); // rotation cleared → leave verts axis-aligned
    }
}

// Immediately (re)applies a fontstring's rotation to its live verts — the
// SetRotation path, so a spin updates every frame with no engine rebuild.
void ReapplyFontStringRotation(void *fs) {
    ApplyToNode(FontStringNode(fs), fs, /*fresh=*/false);
}

// FontString:SetRotation(angle [, cx, cy]). Stores the rotation (shared Xf table)
// and re-rotates the live verts immediately. Rotation is VISUAL ONLY —
// GetStringWidth/Height, GetRect, and SetPoint stay axis-aligned, exactly like
// retail (verified in-game). cx/cy are an optional pivot (normalized within the
// text's vertex bounds; default centre) — a superset of retail, which is
// centre-only.
int __fastcall Script_SetRotationFS(void *L) {
    void *fs = nullptr;
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_TABLE)
        fs = Game::Lua::ResolveObject(L, 1);
    if (fs == nullptr || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: fontstring:SetRotation(angle [, cx, cy])");
        return 0;
    }
    Xf xf = Current(fs);
    xf.angle = static_cast<float>(Game::Lua::ToNumber(L, 2));
    if (Game::Lua::IsNumber(L, 3) && Game::Lua::IsNumber(L, 4)) {
        xf.cxN = static_cast<float>(Game::Lua::ToNumber(L, 3));
        xf.cyN = static_cast<float>(Game::Lua::ToNumber(L, 4));
    }
    if (xf.active())
        g_xf[fs] = xf;
    else
        g_xf.erase(fs); // SetRotation(0) → ReapplyFontStringRotation restores axis-aligned
    ReapplyFontStringRotation(fs);
    return 0;
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
    xf.offX[slot] = ox;
    xf.offY[slot] = oy;
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
    Game::Lua::PushNumber(L, xf.offX[slot]);
    Game::Lua::PushNumber(L, xf.offY[slot]);
    return 2;
}

// Debug binding: dump the drawn-quad corner array (+0xD4) and the resolved
// layout rect, raw internal units, no conversion. Returns 12 numbers:
// BLx,BLy, TLx,TLy, BRx,BRy, TRx,TRy, rectTop, rectLeft, rectBottom, rectRight.
// Diagnostic-only — for corner-transform triage from Lua.
int __fastcall Script_GetCorners(void *L) {
    void *tex = nullptr;
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_TABLE)
        tex = Game::Lua::ResolveObject(L, 1);
    if (tex == nullptr) {
        Game::Lua::Error(L, "Usage: texture:GetCorners()");
        return 0;
    }
    const float *base = reinterpret_cast<const float *>(
        reinterpret_cast<char *>(tex) + Offsets::OFF_SIMPLETEXTURE_CORNERS);
    for (int i = 0; i < 4; ++i) {
        Game::Lua::PushNumber(L, base[i * 3 + 0]);
        Game::Lua::PushNumber(L, base[i * 3 + 1]);
    }
    float rect[4] = {0, 0, 0, 0};
    void *anchor = reinterpret_cast<char *>(tex) + Offsets::OFF_REGION_ANCHOR;
    reinterpret_cast<GetRect_t>(Offsets::FUN_REGION_GET_RECT)(anchor, nullptr, rect);
    for (int i = 0; i < 4; ++i)
        Game::Lua::PushNumber(L, rect[i]);
    return 12;
}

// Debug binding (fontstring analog of GetCorners): the first glyph's 4 baked
// vertex (x,y) positions plus the bounding box of ALL glyph verts. Returns 12
// numbers: v0x,v0y, v1x,v1y, v2x,v2y, v3x,v3y, minX,minY,maxX,maxY. These are
// node-local coords — and because the text paint (FUN_005c8710) is
// TRANSLATE-ONLY, they track the on-screen positions directly (no mirror /
// inversion to undo, unlike the texture corners). So a 0°-vs-90° dump yields the
// rotation direction arithmetically. Diagnostic-only.
int __fastcall Script_GetGlyphVerts(void *L) {
    void *fs = nullptr;
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_TABLE)
        fs = Game::Lua::ResolveObject(L, 1);
    if (fs == nullptr) {
        Game::Lua::Error(L, "Usage: fontstring:GetGlyphVerts()");
        return 0;
    }
    float first[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int got = 0;
    float minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool any = false;
    void *node = FontStringNode(fs);
    if (node != nullptr) {
        ForEachVert(reinterpret_cast<uint8_t *>(node), [&](float *v) {
            if (got < 4) {
                first[got * 2 + 0] = v[0];
                first[got * 2 + 1] = v[1];
                ++got;
            }
            if (!any) {
                minX = maxX = v[0];
                minY = maxY = v[1];
                any = true;
            } else {
                if (v[0] < minX) minX = v[0];
                else if (v[0] > maxX) maxX = v[0];
                if (v[1] < minY) minY = v[1];
                else if (v[1] > maxY) maxY = v[1];
            }
        });
    }
    for (int i = 0; i < 8; ++i)
        Game::Lua::PushNumber(L, first[i]);
    Game::Lua::PushNumber(L, minX);
    Game::Lua::PushNumber(L, minY);
    Game::Lua::PushNumber(L, maxX);
    Game::Lua::PushNumber(L, maxY);
    return 12;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetRotation", &Script_SetRotation},
    {"GetRotation", &Script_GetRotation},
    {"SetVertexOffset", &Script_SetVertexOffset},
    {"GetVertexOffset", &Script_GetVertexOffset},
    {"GetCorners", &Script_GetCorners},
};

// FontString rotation: same GetRotation reader, a fontstring-specific
// SetRotation (glyph-vert rotation, not corner rewrite). No SetVertexOffset —
// retail has no fontstring vertex-offset API.
const Game::Lua::FrameMethodEntry g_fontStringMethods[] = {
    {"SetRotation", &Script_SetRotationFS},
    {"GetRotation", &Script_GetRotation},
    {"GetGlyphVerts", &Script_GetGlyphVerts},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_TEXTURE_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FONTSTRING_METHOD_REGISTRY), g_fontStringMethods,
        static_cast<int>(sizeof(g_fontStringMethods) / sizeof(g_fontStringMethods[0])));
    // The UPPER_LEFT_VERTEX / … constants that VertexSlot maps are defined
    // FrameXML-style in the embedded addon's Util/Constants.lua, not here.
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::HookAutoRegister _hookStoreCorners{
    Offsets::FUN_REGION_STORE_CORNERS, reinterpret_cast<void *>(&StoreCorners_h),
    reinterpret_cast<void **>(&g_storeOriginal)};

} // namespace

void PrepareForReload() {
    g_xf.clear();
    g_fsBaseline.clear();
}

// Fresh-bake entry (the inline-texture DrawBuilder co-hook): the emitter just
// baked axis-aligned verts, so capture the baseline and apply the rotation. See
// ApplyToNode / Transform.h for the timing contract.
void RotateFontStringNode(void *node, void *fs) { ApplyToNode(node, fs, /*fresh=*/true); }

} // namespace Texture::Transform
