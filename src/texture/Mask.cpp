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

// `Texture:SetMask(path)` — apply an alpha-mask texture to a Texture region, the
// way the modern client does (a white-with-alpha mask clips the texture to the
// mask's shape). 1.12 has no generic masking, only the special-cased minimap
// disc (Minimap:SetMaskTexture). This backports it to any Texture.
//
// Mechanism (see the Offsets.h "Texture masking" + "Texgen" blocks for the full
// findings): every textured region draws through the vertex-stream primitive
// FUN_0058a2a0, which is passed the region's corner array as its `positions`.
// We co-hook it; when the positions belong to a region that has a mask set, we
// bind the mask(s) on texture units 1..N (the MODULATE combiner multiplies each
// mask's alpha into the base's alpha) and re-run the engine's own submit.
//
// Two mask sources, ONE sampling path — TEXGEN, the same mechanism as the
// engine's own minimap-disc mask (FUN_004ec440): each unit's UV is generated
// from the raw vertex POSITION and mapped through a per-unit texture matrix.
// No vertex stream is consumed per mask, so up to 7 simultaneous masks
// (device-stage-cap-clamped), and both sources compose (alpha intersection):
//   * SetMask("path") — the matrix is built from the BASE region's own rect
//     (+ its SetRotation, so the mask follows a rotated base). Position-based
//     on purpose: the original recipe here (the minimap BLIP mask's
//     FUN_004eae10, explicit second UV stream with uv1 = uv0) sampled the mask
//     at the base's texcoords, which breaks for SetTexCoord'd sprites — the
//     mask is then sampled at the same sub-rect OF THE MASK IMAGE, showing a
//     corner chunk of the shape instead of the shape. Texgen spans the drawn
//     quad regardless of texcoords (retail semantics); for default 0..1
//     texcoords the two formulas are identical (see BuildMaskMatrix).
//   * AddMaskTexture(maskRegion) — positioned masks: matrix from the mask
//     region's rect (plus its SetRotation, a rotation term in the matrix);
//     each independently positioned, sized, rotated, and animated.
//
// State discipline: the queued draw is only flushed at the indexed submit
// (FUN_GX_PRIM_INDEXED) the engine issues right after this stream call, so the
// texgen/matrix/bind restores CANNOT run inside the stream hook — a second
// co-hook on the indexed submit pops the matrices and the state journal after
// the draw, exactly the op order of the engine's own disc draw.
//
// A cold co-hook on the region ctor clears a region's mask state when its
// pooled memory is reused, so a recycled region can never inherit (or serve) a
// stale mask. Kill switch `_classicapi_TextureMaskEnable`; an SEH latch trips
// it on a draw fault.

#include "Game.h"
#include "Offsets.h"
#include "texture/Transform.h"
#include "ui/FrameObject.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Texture::Mask {

namespace {

bool g_enabled = true;

// A region's mask: the loaded HTEXTURE plus the path (so a re-SetMask with the
// same path is a cheap no-op and the value is inspectable). Keyed by the
// CSimpleTexture object pointer — the same pointer the Lua object resolves to
// AND the base of the corner array the draw hook recovers.
struct MaskEntry {
    void *handle = nullptr; // HTEXTURE from the path loader (never null once set)
    std::string path;
};
std::unordered_map<void *, MaskEntry> g_regionMask;

// --- object mask API: MaskTexture regions ------------------------------------
// A base region -> the mask regions it's clipped by, in Add order. Each mask is
// a real (hidden) CSimpleTexture minted by CreateMaskTexture; the draw hook
// reads its live rect + texture + rotation and clips the base to it. Keyed by
// the base region pointer, same as g_regionMask. The draw applies the first
// (stage cap - 1) masks — up to 7 on this device.
std::unordered_map<void *, std::vector<void *>> g_baseMasks;
// Regions minted by CreateMaskTexture — a mask-source allow-list.
std::unordered_set<void *> g_maskRegions;

// --- path -> HTEXTURE loader (mirrors FUN_00770200's SetTexture load) --------

using TexFlagsInit_t = void *(__thiscall *)(void *self, uint32_t blend, int, int, int, int, int,
                                            uint32_t, int);
using TextureLoad_t = uint32_t(__fastcall *)(const char *path, void *desc, uint32_t flags, int,
                                             int);
using GetRenderable_t = void *(__fastcall *)(void *hTex, int force, void *);

struct TexLoadDesc {
    void *vtbl;
    int32_t field4;
    void *self8;
    uint32_t fieldC;
    int32_t field10;
};

void *LoadByPath(const char *path) {
    TexLoadDesc desc;
    desc.vtbl = reinterpret_cast<void *>(Offsets::PTR_TEXLOAD_DESC_VTBL);
    desc.field4 = 8;
    desc.self8 = &desc.self8;
    desc.fieldC = reinterpret_cast<uintptr_t>(&desc.self8) | 1u;
    desc.field10 = 0;
    uint32_t flags = 0;
    const uint32_t blend = Game::Read<uint32_t>(Offsets::VAR_TEXTURE_BLEND_DEFAULT);
    reinterpret_cast<TexFlagsInit_t>(Offsets::FUN_GX_TEXFLAGS_INIT)(&flags, blend, 0, 0, 0, 0, 0,
                                                                    1, 0);
    return reinterpret_cast<void *>(reinterpret_cast<TextureLoad_t>(
        Offsets::FUN_TEXTURE_LOAD_BY_PATH)(path, &desc, flags, 0, 1));
}

// --- stream-primitive co-hook: apply the mask on unit 1 ----------------------

using GxRsSet_t = void(__fastcall *)(int selector, int value);
using GxRsSetPtr_t = void(__fastcall *)(int selector, const void *value);
using GxTexMtxOp_t = void(__fastcall *)(int unit);
using PrimStreams_t = void(__fastcall *)(int count, const void *pos, int posStride, const void *s3,
                                         int s3Stride, const void *colors, int colorStride,
                                         int drop8, int drop9, const void *uv0, int uv0Stride,
                                         const void *uv1, int uv1Stride);

PrimStreams_t g_primStreamsOriginal = nullptr;

// --- region rect resolution ---------------------------------------------------

using RegionGetRect_t = int(__thiscall *)(void *anchor, float *outTLBR);
using RegionResolve_t = void(__thiscall *)(void *anchor, int flag);
using RegionDirty_t = int(__thiscall *)(void *anchor);

void *AnchorOf(void *region) {
    return reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(region) +
                                    Offsets::OFF_REGION_ANCHOR);
}

// Reads a region's resolved rect {top,left,bottom,right}. Mirrors the engine's
// own GetLeft: if the layout is dirty, force a synchronous resolve with flag 1
// (the read-time path that resolves NOW; flag 0 instead defers to the render's
// layout pass, which never runs for a hidden mask). Only resolves when dirty, so
// steady-state frames are a pure read. Returns false if it still can't resolve.
bool RegionRect(void *region, float outTLBR[4]) {
    void *anchor = AnchorOf(region);
    if (reinterpret_cast<RegionDirty_t>(Offsets::FUN_REGION_LAYOUT_DIRTY)(anchor) != 0)
        reinterpret_cast<RegionResolve_t>(Offsets::FUN_FONTSTRING_LAYOUT_INVALIDATE)(anchor, 1);
    return reinterpret_cast<RegionGetRect_t>(Offsets::FUN_REGION_GET_RECT)(anchor, outTLBR) != 0;
}

// --- texgen: every mask samples on its own unit, 1..N -------------------------
//
// Each mask unit samples via texgen (object-space position) through a texture
// matrix mapping the raw vertex position to the mask's UV — the engine's own
// minimap-disc-mask mechanism. Outside the mask's rect the UV leaves [0,1] and
// the sampler clamps to the mask's transparent border, clipping the base
// (verified live on the original uv1 = uv0 SetMask recipe; same sampler state).
//
// The position→UV map is affine, derived from two verified facts:
//   * corner positions and the layout rect share the same anchor-unit space
//     (the engine's corner store writes rect values verbatim — Transform.cpp);
//   * the draw renders a stored y at (rectTop + rectBottom) − y (the y-mirror
//     established by the 2026-08-18 texprobe fixtures — Transform.cpp
//     WriteCorners). Texgen samples the STORED position, so the matrix folds
//     the mirror in: rendered-space y_r = midY − y.
// In rendered space (y-down): un-rotate about the mask's pivot (a mask rotated
// +a CCW-on-screen shows texel R(−a) about the pivot), then map the mask's rect
// to [0,1]². Composed into one row-vector 4×4 (translation in row 3):
//   u = ( c·x + s·y + Tx − maskLeft) / maskW
//   v = ( s·x − c·y + Ty − maskTop ) / maskHd     [maskHd = bottom − top]
// with c = cos a, s = sin a, pivot P in rendered space, and
//   Tx = Px·(1−c) − s·(midY−Py),  Ty = Py + c·(midY−Py) − s·Px
// (a = 0 ⇒ u = (x − left)/W, v = (midY − y − top)/Hd — exactly the retired
// uv1 = uv0 SetMask recipe's formula re-expressed through positions, so a
// path mask on a default-texcoord texture renders identically to that
// shipped-and-verified path).
bool BuildMaskMatrix(const float mr[4], float midY, bool rotated, float angle, float cxN,
                     float cyN, float out[16]) {
    const float mW = mr[3] - mr[1];  // left→right
    const float mHd = mr[2] - mr[0]; // top→bottom (y-down: positive)
    if (mW == 0.0f || mHd == 0.0f)
        return false;
    float c = 1.0f, s = 0.0f, tx = 0.0f, ty = midY;
    if (rotated) {
        c = std::cos(angle);
        s = std::sin(angle);
        const float px = mr[1] + cxN * mW;
        // cyN is measured from the BOTTOM edge on screen (rendered bottom =
        // mr[2], numerically larger in this y-down space).
        const float py = mr[2] + cyN * (mr[0] - mr[2]);
        tx = px * (1.0f - c) - s * (midY - py);
        ty = py + c * (midY - py) - s * px;
    }
    for (int i = 0; i < 16; ++i)
        out[i] = 0.0f;
    out[0] = c / mW;
    out[1] = s / mHd;
    out[4] = s / mW;
    out[5] = -c / mHd;
    out[10] = 1.0f;
    out[12] = (tx - mr[1]) / mW;
    out[13] = (ty - mr[0]) / mHd;
    out[15] = 1.0f;
    return true;
}

using GxTexMtxPushLoad_t = void(__fastcall *)(int unit, const float *m16);
using PrimIndexed_t = void(__fastcall *)(int primType, int indexCount, const void *indices);
using Crop_t = void(__fastcall *)(void *region, void *edx, float *rect);

PrimIndexed_t g_primIndexedOriginal = nullptr;

// Pending post-draw restores: bit 0 = the Gx state journal was pushed, bits
// 1..7 = that unit's texture matrix was pushed. Set by the masked draw,
// consumed AFTER the engine's indexed submit queues the draw (PrimIndexed_h) —
// restoring inside the stream hook would revert the state the queued draw is
// flushed with (the disc draw pops only past its indexed submit; see
// FUN_GX_PRIM_INDEXED in Offsets.h).
uint32_t g_pendingRestore = 0;

void RestoreAfterDraw() {
    const uint32_t bits = g_pendingRestore;
    if (bits == 0)
        return;
    g_pendingRestore = 0;
    for (int u = 1; u <= 7; ++u)
        if ((bits & (1u << u)) != 0)
            reinterpret_cast<GxTexMtxOp_t>(Offsets::FUN_GX_TEXMTX_POP)(u);
    if ((bits & 1u) != 0)
        reinterpret_cast<void(__fastcall *)()>(Offsets::FUN_GX_STATE_POP)();
}

void __fastcall PrimIndexed_h(int primType, int indexCount, const void *indices) {
    g_primIndexedOriginal(primType, indexCount, indices);
    if (g_pendingRestore != 0)
        RestoreAfterDraw();
}

// Runs the engine submit with each mask bound on its own unit via texgen. All
// fallible resolution happens before any Gx state is touched; the restores run
// in PrimIndexed_h. Returns false when a mask isn't resolvable yet (caller
// draws the base unmasked this frame and retries next). POD-only body for the
// SEH wrapper.
bool MaskedRegionDrawImpl(void *base, void *pathMaskH, void *const *masks, int maskCount,
                          int count, const void *pos, int posStride, const void *s3,
                          int s3Stride, const void *colors, int colorStride, int drop8,
                          int drop9, const void *uv0, int uv0Stride) {
    // Stage budget: units 1..(cap−1), 7 max. Extra masks are dropped, path
    // mask then Add order — the base still draws with every mask that fits.
    void *dev = Game::Read<void *>(Offsets::VAR_GX_DEVICE);
    if (dev == nullptr)
        return false;
    int usable = Game::Read<int>(dev, Offsets::OFF_GXDEV_STAGE_CAP) - 1;
    if (usable > 7)
        usable = 7;
    if (usable < 1)
        return false;

    float br[4]; // base rect {top, left, bottom, right}
    if (!RegionRect(base, br))
        return false;
    // Mirror the engine corner store's input rect: the texcoord crop applies
    // only when the region opted in (SetTexCoordModifiesRect) — same gate as
    // Transform.cpp's ApplyFromRect, so midY matches the store's midline.
    if (Game::Read<int>(base, Offsets::OFF_REGION_TEXCOORD_MODIFIES_RECT) != 0)
        reinterpret_cast<Crop_t>(Offsets::FUN_REGION_TEXCOORD_CROP)(base, nullptr, br);
    const float midY = br[0] + br[2];

    void *cgx[7];
    float mtx[7][16];
    int units = 0;

    // The path mask (SetMask) spans the base's own rect and follows the base's
    // rotation — the exact matrix a mask region covering the base would get.
    // Per-frame residency reference (force=1), exactly like the minimap's mask.
    if (pathMaskH != nullptr) {
        cgx[units] = reinterpret_cast<GetRenderable_t>(Offsets::FUN_TEXTURE_GET_RENDERABLE)(
            pathMaskH, 1, nullptr);
        if (cgx[units] == nullptr)
            return false;
        float angle = 0.0f, cxN = 0.5f, cyN = 0.5f;
        const bool rotated = Texture::Transform::GetRotation(base, &angle, &cxN, &cyN);
        if (!BuildMaskMatrix(br, midY, rotated, angle, cxN, cyN, mtx[units]))
            return false;
        ++units;
    }

    for (int i = 0; i < maskCount && units < usable; ++i, ++units) {
        // The mask's owned HTEXTURE (stored by SetTexture at +0xCC, present
        // even while hidden — the draw-entry array only exists after a render,
        // which a hidden mask never gets). Resolving to a renderable each frame
        // IS the residency reference, same as the path mask above.
        void *hTex = Game::Read<void *>(masks[i], Offsets::OFF_SIMPLETEXTURE_HTEXTURE);
        if (hTex == nullptr)
            return false;
        cgx[units] = reinterpret_cast<GetRenderable_t>(Offsets::FUN_TEXTURE_GET_RENDERABLE)(
            hTex, 1, nullptr);
        if (cgx[units] == nullptr)
            return false;
        float mr[4];
        if (!RegionRect(masks[i], mr))
            return false;
        float angle = 0.0f, cxN = 0.5f, cyN = 0.5f;
        const bool rotated = Texture::Transform::GetRotation(masks[i], &angle, &cxN, &cyN);
        if (!BuildMaskMatrix(mr, midY, rotated, angle, cxN, cyN, mtx[units]))
            return false;
    }
    if (units == 0)
        return false;

    // Infallible tail. Track each push as it lands so a fault mid-sequence
    // never pops something that wasn't pushed.
    if (g_pendingRestore != 0)
        RestoreAfterDraw(); // stale leftover (shouldn't happen) — clear first
    auto rs = reinterpret_cast<GxRsSet_t>(Offsets::FUN_GX_RS_SET);
    auto rsPtr = reinterpret_cast<GxRsSetPtr_t>(Offsets::FUN_GX_RS_SET_PTR);
    reinterpret_cast<void(__fastcall *)()>(Offsets::FUN_GX_STATE_PUSH)();
    g_pendingRestore = 1u;
    for (int i = 0; i < units; ++i) {
        const int unit = 1 + i;
        rsPtr(Offsets::GXRS_TEXTURE0 + unit, cgx[i]);
        rs(Offsets::GXRS_COMBINER0 + unit, Offsets::GXRS_COMBINE_MODULATE);
        rs(Offsets::GXRS_TEXGEN0 + unit, Offsets::GXRS_TEXGEN_OBJECT_POS);
        rs(Offsets::GXRS_TEXXFORM0 + unit, Offsets::GXRS_TEXXFORM_MATRIX);
        reinterpret_cast<GxTexMtxPushLoad_t>(Offsets::FUN_GX_TEXMTX_PUSH_LOAD)(unit, mtx[i]);
        g_pendingRestore |= 1u << unit;
    }
    g_primStreamsOriginal(count, pos, posStride, s3, s3Stride, colors, colorStride, drop8, drop9,
                          uv0, uv0Stride, nullptr, 0);
    return true;
}

bool SafeMaskedRegionDraw(void *base, void *pathMaskH, void *const *masks, int maskCount,
                          int count, const void *pos, int posStride, const void *s3,
                          int s3Stride, const void *colors, int colorStride, int drop8,
                          int drop9, const void *uv0, int uv0Stride) {
    __try {
        return MaskedRegionDrawImpl(base, pathMaskH, masks, maskCount, count, pos, posStride, s3,
                                    s3Stride, colors, colorStride, drop8, drop9, uv0, uv0Stride);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        RestoreAfterDraw(); // pop exactly what was pushed before the fault
        g_enabled = false;
        return true;
    }
}

void __fastcall PrimStreams_h(int count, const void *pos, int posStride, const void *s3,
                              int s3Stride, const void *colors, int colorStride, int drop8,
                              int drop9, const void *uv0, int uv0Stride, const void *uv1,
                              int uv1Stride) {
    // Fast path: nothing masked, feature off, or not a plain textured quad.
    if (g_enabled && count == 4 && uv0 != nullptr && uv1 == nullptr && pos != nullptr &&
        (!g_regionMask.empty() || !g_baseMasks.empty())) {
        // The region draw passes the region's corner array as `positions`, so
        // the owning region is positions - OFF_SIMPLETEXTURE_CORNERS. A false
        // match needs a non-region draw whose positions pointer numerically
        // equals a masked region's corner array — an exact-pointer collision,
        // effectively impossible.
        void *region = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(pos) -
                                                Offsets::OFF_SIMPLETEXTURE_CORNERS);
        // A path mask (SetMask) and positioned mask regions (AddMaskTexture)
        // compose — every mask MODULATEs into the same draw.
        void *pathMaskH = nullptr;
        if (!g_regionMask.empty()) {
            auto it = g_regionMask.find(region);
            if (it != g_regionMask.end())
                pathMaskH = it->second.handle;
        }
        void *const *masks = nullptr;
        int maskCount = 0;
        if (!g_baseMasks.empty()) {
            auto mit = g_baseMasks.find(region);
            if (mit != g_baseMasks.end() && !mit->second.empty()) {
                masks = mit->second.data();
                maskCount = static_cast<int>(mit->second.size());
            }
        }
        if ((pathMaskH != nullptr || maskCount > 0) &&
            SafeMaskedRegionDraw(region, pathMaskH, masks, maskCount, count, pos, posStride, s3,
                                 s3Stride, colors, colorStride, drop8, drop9, uv0, uv0Stride))
            return;
    }
    g_primStreamsOriginal(count, pos, posStride, s3, s3Stride, colors, colorStride, drop8, drop9,
                          uv0, uv0Stride, uv1, uv1Stride);
}

static const Game::HookAutoRegister _primHook{Offsets::FUN_GX_PRIM_STREAMS,
                                              reinterpret_cast<void *>(&PrimStreams_h),
                                              reinterpret_cast<void **>(&g_primStreamsOriginal)};

// The indexed submit paired with every stream call — where the texgen-path
// restores run (see g_pendingRestore).
static const Game::HookAutoRegister _primIdxHook{
    Offsets::FUN_GX_PRIM_INDEXED, reinterpret_cast<void *>(&PrimIndexed_h),
    reinterpret_cast<void **>(&g_primIndexedOriginal)};

// --- region ctor co-hook: drop a stale mask on pooled reuse ------------------

// __thiscall(mem, parent, layer, sublayer) -> region; dummy-EDX __fastcall.
using Ctor_t = void *(__fastcall *)(void *mem, void *edx, void *parent, int layer, int sublayer);
Ctor_t g_ctorOriginal = nullptr;

void *__fastcall Ctor_h(void *mem, void *edx, void *parent, int layer, int sublayer) {
    if (mem != nullptr) {
        // A (re)constructed region owns no mask and is no longer a mask source.
        g_regionMask.erase(mem);
        g_baseMasks.erase(mem);
        // If the recycled memory WAS a mask region, detach it from every base
        // still holding it — otherwise the draw hook would read the recycled
        // region as a mask (stale rect/texture at best, garbage at worst).
        if (g_maskRegions.erase(mem) != 0 && !g_baseMasks.empty()) {
            for (auto it = g_baseMasks.begin(); it != g_baseMasks.end();) {
                auto &v = it->second;
                v.erase(std::remove(v.begin(), v.end(), mem), v.end());
                if (v.empty())
                    it = g_baseMasks.erase(it);
                else
                    ++it;
            }
        }
    }
    return g_ctorOriginal(mem, edx, parent, layer, sublayer);
}

static const Game::HookAutoRegister _ctorHook{Offsets::FUN_SIMPLETEXTURE_CTOR,
                                              reinterpret_cast<void *>(&Ctor_h),
                                              reinterpret_cast<void **>(&g_ctorOriginal)};

// --- Lua surface -------------------------------------------------------------

// texture:SetMask("path") sets the mask; SetMask(nil) / SetMask("") clears it.
int __fastcall Script_SetMask(void *L) {
    void *region = Game::Lua::ResolveObject(L, 1);
    if (region == nullptr)
        return 0;
    const char *path = (Game::Lua::GetTop(L) >= 2 && Game::Lua::Type(L, 2) == Game::Lua::TYPE_STRING)
                           ? Game::Lua::ToString(L, 2)
                           : nullptr;
    if (path == nullptr || path[0] == '\0') {
        g_regionMask.erase(region);
        return 0;
    }
    auto it = g_regionMask.find(region);
    if (it != g_regionMask.end() && it->second.path == path)
        return 0; // unchanged
    MaskEntry e;
    e.handle = LoadByPath(path); // never null (engine substitutes a fallback)
    e.path = path;
    g_regionMask[region] = std::move(e);
    return 0;
}

using ScriptFn_t = int(__fastcall *)(void *L);
using RegionHide_t = void(__fastcall *)(void *region);

// frame:CreateMaskTexture([name, layer, ...]) — mint a MaskTexture. Delegates to
// the engine's own CreateTexture (it reads the args off this same stack and
// pushes the new region), then hides it (a mask is a source, never drawn) and
// records it as a valid mask source.
int __fastcall Script_CreateMaskTexture(void *L) {
    const int rc = reinterpret_cast<ScriptFn_t>(Offsets::FUN_SCRIPT_CREATE_TEXTURE)(L);
    if (rc != 1)
        return rc; // engine raised an error, or pushed nothing
    void *region = Game::Lua::ResolveObject(L, Game::Lua::GetTop(L));
    if (region != nullptr) {
        Game::Ref<uint32_t>(region, Offsets::OFF_REGION_DESIRED_SHOWN) = 0;
        reinterpret_cast<RegionHide_t>(Offsets::FUN_FONTSTRING_HIDE)(region);
        g_maskRegions.insert(region);
    }
    return 1; // the region object the engine pushed is our return value
}

// texture:AddMaskTexture(mask) — clip this texture to the mask region. Repeat
// calls add further masks (drawn on units 1..7; extras past the device's stage
// budget are stored but not applied). Adding the same mask twice is a no-op.
int __fastcall Script_AddMaskTexture(void *L) {
    void *base = Game::Lua::ResolveObject(L, 1);
    void *mask = Game::Lua::ResolveObject(L, 2);
    if (base == nullptr || mask == nullptr || base == mask)
        return 0;
    auto &v = g_baseMasks[base];
    for (void *m : v)
        if (m == mask)
            return 0;
    v.push_back(mask);
    return 0;
}

// texture:RemoveMaskTexture([mask]) — drop the given mask, or all if omitted.
int __fastcall Script_RemoveMaskTexture(void *L) {
    void *base = Game::Lua::ResolveObject(L, 1);
    if (base == nullptr)
        return 0;
    void *mask = Game::Lua::ResolveObject(L, 2);
    auto it = g_baseMasks.find(base);
    if (it == g_baseMasks.end())
        return 0;
    if (mask == nullptr) {
        g_baseMasks.erase(it);
        return 0;
    }
    auto &v = it->second;
    v.erase(std::remove(v.begin(), v.end(), mask), v.end());
    if (v.empty())
        g_baseMasks.erase(it);
    return 0;
}

// texture:GetNumMaskTextures() -> count.
int __fastcall Script_GetNumMaskTextures(void *L) {
    void *base = Game::Lua::ResolveObject(L, 1);
    int n = 0;
    if (base != nullptr) {
        auto it = g_baseMasks.find(base);
        if (it != g_baseMasks.end())
            n = static_cast<int>(it->second.size());
    }
    Game::Lua::PushNumber(L, n);
    return 1;
}

// texture:GetMaskTexture(index) -> the index-th mask region object (or nil).
int __fastcall Script_GetMaskTexture(void *L) {
    void *base = Game::Lua::ResolveObject(L, 1);
    const int index =
        (Game::Lua::GetTop(L) >= 2) ? static_cast<int>(Game::Lua::ToNumber(L, 2)) : 1;
    if (base != nullptr && index >= 1) {
        auto it = g_baseMasks.find(base);
        if (it != g_baseMasks.end() && static_cast<size_t>(index) <= it->second.size()) {
            UI::FrameObject::Push(L, it->second[index - 1]);
            return 1;
        }
    }
    Game::Lua::PushNil(L);
    return 1;
}

// _classicapi_TextureMaskEnable([on]) -> enabled. Kill switch (also what the
// SEH latch trips on a draw fault).
int __fastcall Script_TextureMaskEnable(void *L) {
    if (Game::Lua::GetTop(L) == 0)
        g_enabled = true;
    else
        g_enabled = Game::Lua::ToBoolean(L, 1) != 0;
    Game::Lua::PushBool(L, g_enabled);
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetMask", &Script_SetMask},
    {"AddMaskTexture", &Script_AddMaskTexture},
    {"RemoveMaskTexture", &Script_RemoveMaskTexture},
    {"GetNumMaskTextures", &Script_GetNumMaskTextures},
    {"GetMaskTexture", &Script_GetMaskTexture},
};

const Game::Lua::FrameMethodEntry g_frameMethods[] = {
    {"CreateMaskTexture", &Script_CreateMaskTexture},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_TEXTURE_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_frameMethods,
        static_cast<int>(sizeof(g_frameMethods) / sizeof(g_frameMethods[0])));
    Game::Lua::RegisterGlobalFunction("_classicapi_TextureMaskEnable", &Script_TextureMaskEnable);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void PrepareForReload() {
    // /reload frees every region; forget the mask maps so a recycled address
    // can't be mistaken for a still-masked region before its ctor re-clears it.
    g_regionMask.clear();
    g_baseMasks.clear();
    g_maskRegions.clear();
}

// The masked-draw path's own gate, minus the per-draw shape tests: if this is
// false, PrimStreams_h took the fast path on every draw and this module bound
// nothing on any texture stage.
bool AnyMaskActive() { return g_enabled && (!g_regionMask.empty() || !g_baseMasks.empty()); }

static const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

} // namespace Texture::Mask
