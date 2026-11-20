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
// disc (Minimap:SetMaskTexture). This backports it to any Texture. XML sets it
// with the Texture attribute `mask="path"`, read after the engine's texture
// loader (Xml::TypeLoad).
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
// No vertex stream is consumed per mask, and both sources compose:
//   * SetMask("path") — the matrix is built from the BASE region's own drawn
//     corners, so the mask follows a rotated or warped base. Position-based
//     on purpose: the original recipe here (the minimap BLIP mask's
//     FUN_004eae10, explicit second UV stream with uv1 = uv0) sampled the mask
//     at the base's texcoords, which breaks for SetTexCoord'd sprites — the
//     mask is then sampled at the same sub-rect OF THE MASK IMAGE, showing a
//     corner chunk of the shape instead of the shape. Texgen spans the drawn
//     quad regardless of texcoords; for default 0..1 texcoords the two
//     formulas are identical (see BuildCornerMatrix). As the 3.80.2 client's
//     bake does (0x140577CF0), the mask's alpha REPLACES the texture's own and
//     its colour is ignored: stage 0 takes combiner preset 0 (the vertex alpha
//     alone) and the mask's stage a preset of our own past the engine's six
//     (kPresetMaskAlpha, applied by the FUN_GX_D3D_APPLY_COMBINER co-hook).
//   * AddMaskTexture(maskRegion) — positioned masks: the matrix maps the mask
//     region's drawn corners (its rotation and vertex offsets included, as
//     3.80.2 builds the frame from the drawn corners, 0x14057A2E0) to the
//     mask's UV square; each independently positioned, sized, rotated and
//     animated. At most 3 per texture, as 3.80.2 allows (0x140577610).
//
// A MaskTexture (CreateMaskTexture, XML <MaskTexture>) is a texture region
// given its own copy of CSimpleTexture's vtable, as 3.80.2's CSimpleMaskTexture
// is the texture constructor plus a vtable swap: GetObjectType answers
// "MaskTexture", IsObjectType("MaskTexture") is true, and the four mask methods
// are not on it. It stays shown and is never drawn (the draw hook skips it, as
// 3.80.2's batch slot 0x140579F50 skips IsMask), and a hidden mask still masks.
//
// State discipline: the queued draw is only flushed at the indexed submit
// (FUN_GX_PRIM_INDEXED) the engine issues right after this stream call, so the
// texgen/matrix/bind restores CANNOT run inside the stream hook — a second
// co-hook on the indexed submit pops the matrices and the state journal after
// the draw, exactly the op order of the engine's own disc draw.
//
// A Texture::Construct listener clears a region's mask state when its
// pooled memory is reused, so a recycled region can never inherit (or serve) a
// stale mask. Kill switch `_classicapi_TextureMaskEnable`; an SEH latch trips
// it on a draw fault.

#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "frame/AlphaGradient.h"
#include "frame/Layout.h"
#include "gx/Device.h"
#include "texture/Construct.h"
#include "texture/CornerStore.h"
#include "texture/Load.h"
#include "texture/Mask.h"
#include "texture/Slice.h"
#include "texture/Transform.h"
#include "xml/FrameLoad.h"
#include "xml/TypeLoad.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Texture::Mask {

namespace {

bool g_enabled = true;

// A region's mask: the loaded HTEXTURE plus the path (so a re-SetMask with the
// same path is a cheap no-op and the value is inspectable). The entry owns one
// reference on the handle, closed when the entry is replaced or dropped, the
// way SetTexture closes the handle it held. Keyed by the
// CSimpleTexture object pointer — the same pointer the Lua object resolves to
// AND the base of the corner array the draw hook recovers.
struct MaskEntry {
    void *handle = nullptr; // HTEXTURE from the path loader (never null once set)
    std::string path;
};
std::unordered_map<void *, MaskEntry> g_regionMask;

// --- object mask API: MaskTexture regions ------------------------------------
// A base region -> the mask regions it's clipped by, in Add order, at most
// kMaxMasks. Each mask is a MaskTexture; the draw hook reads its live corners
// and texture and clips the base to it. Keyed by the base region pointer, same
// as g_regionMask.
constexpr size_t kMaxMasks = 3;
std::unordered_map<void *, std::vector<void *>> g_baseMasks;
// Every MaskTexture region: what the draw hook skips, and what a pooled reuse
// detaches.
std::unordered_set<void *> g_maskRegions;

// --- the MaskTexture vtable ------------------------------------------------------

using GetScriptMethod_t = int(__thiscall *)(void *self, void *L, const char *name);
using IsObjectType_t = int(__thiscall *)(void *self, const char *name);

uintptr_t g_maskVtable[Offsets::VTBL_SIMPLETEXTURE_SLOTS];
const char kMaskTextureType[] = "MaskTexture";

uintptr_t TextureSlot(uint32_t offset) {
    return reinterpret_cast<const uintptr_t *>(Offsets::VTBL_SIMPLETEXTURE)[offset / 4];
}

// The four mask methods are a Texture's only: a MaskTexture's lookup misses
// them, so they read as nil on it.
int __fastcall MaskGetScriptMethod(void *self, void *, void *L, const char *name) {
    static const char *const kTextureOnly[] = {"AddMaskTexture", "RemoveMaskTexture",
                                               "GetNumMaskTextures", "GetMaskTexture"};
    for (const char *m : kTextureOnly)
        if (name != nullptr && std::strcmp(name, m) == 0)
            return 0;
    return reinterpret_cast<GetScriptMethod_t>(TextureSlot(Offsets::VTBL_FRAME_GET_SCRIPT_METHOD))(
        self, L, name);
}

int __fastcall MaskIsObjectType(void *self, void *, const char *name) {
    if (name != nullptr && Ascii::EqualCI(name, kMaskTextureType))
        return 1;
    return reinterpret_cast<IsObjectType_t>(TextureSlot(Offsets::VTBL_FRAME_IS_OBJECT_TYPE))(self,
                                                                                         name);
}

const char *__fastcall MaskGetObjectType(void *) { return kMaskTextureType; }

const void *MaskVtable() {
    if (g_maskVtable[0] == 0) {
        for (int i = 0; i < Offsets::VTBL_SIMPLETEXTURE_SLOTS; ++i)
            g_maskVtable[i] = TextureSlot(i * 4);
        g_maskVtable[Offsets::VTBL_FRAME_GET_SCRIPT_METHOD / 4] =
            reinterpret_cast<uintptr_t>(&MaskGetScriptMethod);
        g_maskVtable[Offsets::VTBL_FRAME_IS_OBJECT_TYPE / 4] =
            reinterpret_cast<uintptr_t>(&MaskIsObjectType);
        g_maskVtable[Offsets::VTBL_FRAME_GET_OBJECT_TYPE / 4] =
            reinterpret_cast<uintptr_t>(&MaskGetObjectType);
    }
    return g_maskVtable;
}


using Texture::LoadByPath;
using Game::ReleaseHandle;

// Drops a region's path mask and its handle reference.
void ClearPathMask(void *region) {
    auto it = g_regionMask.find(region);
    if (it == g_regionMask.end())
        return;
    Game::ReleaseHandle(it->second.handle);
    g_regionMask.erase(it);
}

} // namespace

bool SetPath(void *region, const char *path) {
    if (path == nullptr || path[0] == '\0') {
        ClearPathMask(region);
        return true;
    }
    auto it = g_regionMask.find(region);
    if (it != g_regionMask.end() && it->second.path == path)
        return true; // unchanged
    ClearPathMask(region);
    void *handle = LoadByPath(path);
    if (handle == nullptr)
        return false; // a path that does not load clears the mask, as 3.80.2's
    
    g_regionMask[region] = MaskEntry{handle, path};
    return true;
}

void MakeMask(void *region) {
    Game::Ref<const void *>(region, 0) = MaskVtable();
    g_maskRegions.insert(region);
}

bool IsMask(const void *region) {
    return region != nullptr && Game::Read<const void *>(region, 0) == MaskVtable();
}

AddResult Add(void *base, void *mask) {
    auto &v = g_baseMasks[base];
    // The cap is checked before the duplicate, as 3.80.2's (0x140577610).
    if (v.size() >= kMaxMasks)
        return AddResult::Full;
    for (void *m : v)
        if (m == mask)
            return AddResult::Added;
    v.push_back(mask);
    return AddResult::Added;
}

namespace {

// --- stream-primitive co-hook: apply the mask on unit 1 ----------------------

using PrimStreams_t = void(__fastcall *)(int count, const void *pos, int posStride, const void *s3,
                                         int s3Stride, const void *colors, int colorStride,
                                         int drop8, int drop9, const void *uv0, int uv0Stride,
                                         const void *uv1, int uv1Stride);

PrimStreams_t g_primStreamsOriginal = nullptr;

// A mask's rect comes from Frame::Layout::GetResolvedRect: the layout pass
// skips hidden regions, and a hidden mask still masks.

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
// In rendered space (y-down) the mask's drawn corners give an affine frame,
// and a point's coordinates in that frame are the mask's UV
// (BuildCornerMatrix). For an axis-aligned mask that is u = (x - left) / W,
// v = (midY - y - top) / H — exactly the retired uv1 = uv0 SetMask recipe's
// formula re-expressed through positions, so a path mask on a default-texcoord
// texture renders where that shipped-and-verified path put it.
// The texgen matrix for a mask whose drawn corners (rect space, y-down; BL,
// TL, BR, TR) are tx/ty: the affine frame of its bottom-left corner and its
// bottom and left edges maps a rendered point to the mask's UV square, u along
// the bottom edge and v down from the top (3.80.2's 0x14057A1D0 takes the same
// three corners). Texgen samples the STORED position, rendered at
// (x, midY - y), so the mirror is folded in.
bool BuildCornerMatrix(const float tx[4], const float ty[4], float midY, float out[16]) {
    const float ox = tx[0], oy = ty[0];         // BL
    const float ax = tx[2] - ox, ay = ty[2] - oy; // bottom edge, BL -> BR
    const float bx = tx[1] - ox, by = ty[1] - oy; // left edge, BL -> TL
    const float det = ax * by - bx * ay;
    if (std::fabs(det) < 1e-12f)
        return false;
    // [u w] = inverse([a b]) * (P - O), w measured up from the bottom edge.
    const float i11 = by / det, i12 = -bx / det;
    const float i21 = -ay / det, i22 = ax / det;
    // P = (x, midY - y): u = i11 x - i12 y + i12 (midY - oy) - i11 ox;
    // v = 1 - w = -i21 x + i22 y + 1 - i22 (midY - oy) + i21 ox.
    for (int i = 0; i < 16; ++i)
        out[i] = 0.0f;
    out[0] = i11;
    out[4] = -i12;
    out[12] = i12 * (midY - oy) - i11 * ox;
    out[1] = -i21;
    out[5] = i22;
    out[13] = 1.0f - i22 * (midY - oy) + i21 * ox;
    out[10] = 1.0f;
    out[15] = 1.0f;
    return true;
}

// `region`'s drawn corners (its rotation and vertex offsets), from its resolved
// rect as the corner store takes it ({top, left, bottom, right} in `rect`).
bool DrawnCorners(void *region, float (&rect)[4], float tx[4], float ty[4]) {
    if (!Frame::Layout::GetResolvedRect(region, rect))
        return false;
    Texture::CornerStore::CropInput(region, rect);
    Texture::Transform::TargetCorners(region, rect[1], rect[3], rect[0], rect[2], tx, ty);
    return true;
}

// COLOROP SELECTARG2, ALPHAOP MODULATE: the colour so far, its alpha times the
// mask texture's. One past the engine's six presets (FUN_GX_D3D_APPLY_COMBINER).
constexpr int kPresetMaskAlpha = 6;

using ApplyCombiner_t = void(__fastcall *)(void *dev, void *edx, int stage, int preset);
using SetStageState_t = void(__fastcall *)(void *dev, void *edx, int stateId, int value);
ApplyCombiner_t g_origApplyCombiner = nullptr;

void __fastcall ApplyCombiner_h(void *dev, void *edx, int stage, int preset) {
    if (preset != kPresetMaskAlpha) {
        g_origApplyCombiner(dev, edx, stage, preset);
        return;
    }
    if (static_cast<uint32_t>(stage) >= Game::Read<uint32_t>(dev, Offsets::OFF_GXDEV_D3D_STAGE_COUNT) ||
        Game::Read<uint8_t>(dev, Offsets::OFF_GXDEV_D3D_STAGE_ENABLED + stage) == 0)
        return;
    const auto set = reinterpret_cast<SetStageState_t>(Offsets::FUN_GX_D3D_SET_STAGE_STATE);
    set(dev, nullptr, Offsets::GXD3D_STATE_COLOROP0 + stage, Offsets::D3DTOP_SELECTARG2);
    set(dev, nullptr, Offsets::GXD3D_STATE_ALPHAOP0 + stage, Offsets::D3DTOP_MODULATE);
}

const Game::HookAutoRegister _combinerHook{Offsets::FUN_GX_D3D_APPLY_COMBINER,
                                           reinterpret_cast<void *>(&ApplyCombiner_h),
                                           reinterpret_cast<void **>(&g_origApplyCombiner)};

using PrimIndexed_t = void(__fastcall *)(int primType, int indexCount, const void *indices);

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
            Gx::PopTransform(u);
    if ((bits & 1u) != 0)
        Gx::PopStates();
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
    // Stages from 1 up hold the alpha gradients being drawn; masks go above.
    const int first = 1 + Frame::AlphaGradient::StagesInUse();
    int usable = Game::Read<int>(dev, Offsets::OFF_GXDEV_STAGE_CAP) - first;
    if (usable > 8 - first)
        usable = 8 - first;
    if (usable < 1)
        return false;

    float br[4]; // base rect {top, left, bottom, right}, as the corner store takes it
    float btx[4], bty[4];
    if (!DrawnCorners(base, br, btx, bty))
        return false;
    const float midY = br[0] + br[2]; // the store's midline

    void *cgx[7];
    float mtx[7][16];
    int units = 0;

    // The path mask (SetMask) spans the base's own drawn corners — the exact
    // matrix a mask region covering the base would get. Per-frame residency
    // reference (force=1), exactly like the minimap's mask.
    if (pathMaskH != nullptr) {
        cgx[units] = Texture::DeviceTexture(pathMaskH);
        if (cgx[units] == nullptr)
            return false;
        if (!BuildCornerMatrix(btx, bty, midY, mtx[units]))
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
        cgx[units] = Texture::DeviceTexture(hTex);
        if (cgx[units] == nullptr)
            return false;
        float mr[4], mtx4[4], mty4[4];
        if (!DrawnCorners(masks[i], mr, mtx4, mty4) || !BuildCornerMatrix(mtx4, mty4, midY, mtx[units]))
            return false;
    }
    if (units == 0)
        return false;

    // Infallible tail. Track each push as it lands so a fault mid-sequence
    // never pops something that wasn't pushed.
    if (g_pendingRestore != 0)
        RestoreAfterDraw(); // stale leftover (shouldn't happen) — clear first
    Gx::PushStates();
    g_pendingRestore = 1u;
    // The path mask replaces the texture's alpha (D3D9 only: the preset is ours).
    const bool replaceAlpha = pathMaskH != nullptr && Gx::IsD3d();
    if (replaceAlpha)
        Gx::SetState(Offsets::GXRS_COMBINER0, Offsets::GXRS_COMBINE_COLOR_ONLY);
    for (int i = 0; i < units; ++i) {
        const int unit = first + i;
        Gx::SetState(Offsets::GXRS_TEXTURE0 + unit, cgx[i]);
        Gx::SetState(Offsets::GXRS_COMBINER0 + unit, replaceAlpha && i == 0 ? kPresetMaskAlpha
                                                                            : Offsets::GXRS_COMBINE_MODULATE);
        Gx::SetState(Offsets::GXRS_TEXGEN0 + unit, Offsets::GXRS_TEXGEN_OBJECT_POS);
        Gx::SetState(Offsets::GXRS_TEXXFORM0 + unit, Offsets::GXRS_TEXXFORM_MATRIX);
        Gx::PushTransform(unit, mtx[i]);
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
    // Fast path: nothing masked, no MaskTexture, or not a plain textured quad.
    if (count == 4 && uv0 != nullptr && uv1 == nullptr && pos != nullptr &&
        (!g_maskRegions.empty() || (g_enabled && (!g_regionMask.empty() || !g_baseMasks.empty())))) {
        // The region draw passes the region's corner array as `positions`, so
        // the owning region is positions - OFF_SIMPLETEXTURE_CORNERS. A false
        // match needs a non-region draw whose positions pointer numerically
        // equals a masked region's corner array — an exact-pointer collision,
        // effectively impossible. A sliced texture's pieces point at arrays
        // Texture::Slice keeps, which it maps back to the texture.
        void *region = Texture::Slice::Owner(pos);
        if (region == nullptr)
            region = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(pos) -
                                              Offsets::OFF_SIMPLETEXTURE_CORNERS);
        // A MaskTexture is never drawn.
        if (!g_maskRegions.empty() && g_maskRegions.count(region) != 0)
            return;
        // A path mask (SetMask) and positioned mask regions (AddMaskTexture)
        // compose — every mask MODULATEs into the same draw.
        void *pathMaskH = nullptr;
        if (g_enabled && !g_regionMask.empty()) {
            auto it = g_regionMask.find(region);
            if (it != g_regionMask.end())
                pathMaskH = it->second.handle;
        }
        void *const *masks = nullptr;
        int maskCount = 0;
        if (g_enabled && !g_baseMasks.empty()) {
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

// --- region construction: drop a stale mask on pooled reuse ------------------

void OnTextureConstructed(void *mem) {
    // A (re)constructed region owns no mask and is no longer a mask source.
    ClearPathMask(mem);
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

static const Texture::Construct::AutoSubscribe _constructSub{&OnTextureConstructed};

// --- Lua surface -------------------------------------------------------------

// texture:SetMask(file): a path, or a number read as one; "" or a file that does
// not load clears the mask. nil is the usage error, as 3.80.2's (0x140A69FE0).
int __fastcall Script_SetMask(void *L) {
    void *region = Game::Lua::ResolveTexture(L);
    if (region == nullptr)
        return 0;
    const int type = Game::Lua::Type(L, 2);
    if (type != Game::Lua::TYPE_STRING && type != Game::Lua::TYPE_NUMBER) {
        Game::Lua::Error(L, "Usage: self:SetMask(file)");
        return 0;
    }
    SetPath(region, Game::Lua::ToString(L, 2));
    return 0;
}

// The later-client XML attribute `<Texture file=".." mask="path"/>`, applied
// as texture:SetMask(path) when present and non-empty.
void OnTextureNode(void *object, const void *node, void * /*status*/) {
    if (const char *mask = Xml::Attr(node, "mask"); mask != nullptr && *mask != '\0')
        SetPath(object, mask);
}

const Xml::TypeLoad::AutoSubscribe _xmlSub{Xml::TypeLoad::TEXTURE, &OnTextureNode};


// The MaskTexture argument at `idx`, or nullptr.
void *MaskArg(void *L, int idx) {
    void *mask = Game::Lua::ResolveTexture(L, idx, /*raiseError=*/false);
    return IsMask(mask) ? mask : nullptr;
}

// frame:CreateMaskTexture([name, drawLayer, templateName, subLevel]) — the
// engine's own CreateTexture (it reads name, layer and template off this same
// stack and pushes the new region), then the MaskTexture vtable. subLevel is
// checked as 3.80.2 checks it (0x140A84590); a mask never draws, so it places
// nothing.
int __fastcall Script_CreateMaskTexture(void *L) {
    if (Game::Lua::Type(L, 5) > Game::Lua::TYPE_NIL) {
        const int subLevel = static_cast<int>(Game::Lua::ToNumber(L, 5));
        if (subLevel < -8 || subLevel > 7) {
            Game::Lua::Error(L, "Sublevel must be between -8 and 7");
            return 0;
        }
    }
    if (Game::Lua::GetTop(L) > 4)
        Game::Lua::SetTop(L, 4);
    const int rc = Game::Lua::CallScript(Offsets::FUN_SCRIPT_CREATE_TEXTURE, L);
    if (rc != 1)
        return rc; // engine raised an error, or pushed nothing
    void *region = Game::Lua::ResolveObject(L, Game::Lua::GetTop(L));
    if (region != nullptr)
        MakeMask(region);
    return 1; // the region object the engine pushed is our return value
}

// texture:AddMaskTexture(mask) — clip this texture to the mask. Adding the same
// mask twice is a no-op; a fourth raises 3.80.2's error.
int __fastcall Script_AddMaskTexture(void *L) {
    void *base = Game::Lua::ResolveTexture(L);
    if (base == nullptr)
        return 0;
    void *mask = MaskArg(L, 2);
    if (mask == nullptr) {
        Game::Lua::Error(L, "Usage: self:AddMaskTexture(mask)");
        return 0;
    }
    if (Add(base, mask) == AddResult::Full)
        Game::Lua::RaiseClientMethodError(L, Game::Lua::ObjectName(base), "AddMaskTexture",
                                          "Texture already has the maximum number of mask textures (3)");
    return 0;
}

// texture:RemoveMaskTexture(mask) — a mask that is not attached is no error.
int __fastcall Script_RemoveMaskTexture(void *L) {
    void *base = Game::Lua::ResolveTexture(L);
    if (base == nullptr)
        return 0;
    void *mask = MaskArg(L, 2);
    if (mask == nullptr) {
        Game::Lua::Error(L, "Usage: self:RemoveMaskTexture(mask)");
        return 0;
    }
    auto it = g_baseMasks.find(base);
    if (it == g_baseMasks.end())
        return 0;
    auto &v = it->second;
    v.erase(std::remove(v.begin(), v.end(), mask), v.end());
    if (v.empty())
        g_baseMasks.erase(it);
    return 0;
}

// texture:GetNumMaskTextures() -> count.
int __fastcall Script_GetNumMaskTextures(void *L) {
    void *base = Game::Lua::ResolveTexture(L);
    int n = 0;
    if (base != nullptr) {
        auto it = g_baseMasks.find(base);
        if (it != g_baseMasks.end())
            n = static_cast<int>(it->second.size());
    }
    Game::Lua::PushNumber(L, n);
    return 1;
}

#define GET_MASK_TEXTURE_USAGE "Usage: local mask = self:GetMaskTexture(index)"

// texture:GetMaskTexture(index) -> the index-th mask, or nil. `index` is
// required, a number from 0 to 2^32 - 1; it is lowered by 1 and truncated, so
// 0 is nil and 1.5 is the first (3.80.2's 0x140A66DE0).
int __fastcall Script_GetMaskTexture(void *L) {
    void *base = Game::Lua::ResolveTexture(L);
    if (base == nullptr)
        return 0;
    if (Game::Lua::Type(L, 2) <= Game::Lua::TYPE_NIL) {
        Game::Lua::Error(L, GET_MASK_TEXTURE_USAGE);
        return 0;
    }
    const double index = Game::Lua::IsNumber(L, 2) ? Game::Lua::ToNumber(L, 2) : -1.0;
    if (!(index >= 0.0 && index <= 4294967295.0)) {
        Game::Lua::Error(L, "outside of expected range 0 to 4294967295 - " GET_MASK_TEXTURE_USAGE);
        return 0;
    }
    const int i = static_cast<int>(index - 1.0);
    auto it = g_baseMasks.find(base);
    if (i >= 0 && it != g_baseMasks.end() && static_cast<size_t>(i) < it->second.size())
        Game::Lua::PushObject(L, it->second[i]);
    else
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

const Game::Doc::Field kSetMaskArgs[] = {
    Game::Doc::Req("file", "cstring",
                   "A texture whose alpha channel replaces the texture's own; an empty string, "
                   "or a file that does not load, removes the mask."),
};
const Game::Doc::Function kSetMask{"Clips the texture to the shape of a mask file.",
                                   kSetMaskArgs, {}};

const Game::Doc::Field kAddMaskTextureArgs[] = {
    Game::Doc::Req("mask", "SimpleMaskTexture"),
};
const Game::Doc::Function kAddMaskTexture{
    "Clips the texture to a mask texture's shape and position; up to 3 masks intersect.",
    kAddMaskTextureArgs, {}};

const Game::Doc::Field kRemoveMaskTextureArgs[] = {
    Game::Doc::Req("mask", "SimpleMaskTexture"),
};
const Game::Doc::Function kRemoveMaskTexture{"Removes a mask texture from the texture.",
                                             kRemoveMaskTextureArgs, {}};

const Game::Doc::Field kGetNumMaskTexturesRets[] = {
    Game::Doc::Req("count", "number", "Does not count a mask set with SetMask."),
};
const Game::Doc::Function kGetNumMaskTextures{
    "Returns the number of mask textures attached to the texture.", {}, kGetNumMaskTexturesRets};

const Game::Doc::Field kGetMaskTextureArgs[] = {
    Game::Doc::Req("index", "luaIndex", "1 is the first mask added."),
};
const Game::Doc::Field kGetMaskTextureRets[] = {
    Game::Doc::Opt("mask", "SimpleMaskTexture", nullptr, "Nil when no mask has the index."),
};
const Game::Doc::Function kGetMaskTexture{"Returns a mask texture attached to the texture.",
                                          kGetMaskTextureArgs, kGetMaskTextureRets};

const Game::Doc::Field kCreateMaskTextureArgs[] = {
    Game::Doc::Opt("name", "cstring"),
    Game::Doc::Opt("drawLayer", "DrawLayer"),
    Game::Doc::Opt("templateName", "cstring"),
    Game::Doc::Opt("subLevel", "number", "0", "From -8 to 7."),
};
const Game::Doc::Field kCreateMaskTextureRets[] = {
    Game::Doc::Req("maskTexture", "SimpleMaskTexture",
                   "A texture that is never drawn and clips the textures it is added to; "
                   "GetObjectType returns \"MaskTexture\"."),
};
const Game::Doc::Function kCreateMaskTexture{"Creates a mask texture on the frame.",
                                             kCreateMaskTextureArgs, kCreateMaskTextureRets};

const Game::Doc::Method g_methodDocs[] = {
    {"SetMask", &kSetMask},
    {"AddMaskTexture", &kAddMaskTexture},
    {"RemoveMaskTexture", &kRemoveMaskTexture},
    {"GetNumMaskTextures", &kGetNumMaskTextures},
    {"GetMaskTexture", &kGetMaskTexture},
};

const Game::Doc::Method g_frameMethodDocs[] = {
    {"CreateMaskTexture", &kCreateMaskTexture},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_TEXTURE_METHOD_REGISTRY), g_methods, g_methodDocs);
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_frameMethods,
        g_frameMethodDocs);
    Game::Lua::RegisterGlobalFunction("_classicapi_TextureMaskEnable", &Script_TextureMaskEnable,
                                      nullptr);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void PrepareForReload() {
    // /reload frees every region; forget the mask maps so a recycled address
    // can't be mistaken for a still-masked region before its ctor re-clears it.
    for (auto &entry : g_regionMask)
        Game::ReleaseHandle(entry.second.handle);
    g_regionMask.clear();
    g_baseMasks.clear();
    g_maskRegions.clear();
}

// The masked-draw path's own gate, minus the per-draw shape tests: if this is
// false, PrimStreams_h took the fast path on every draw and this module bound
// nothing on any texture stage.
bool AnyMaskActive() { return g_enabled && (!g_regionMask.empty() || !g_baseMasks.empty()); }

static const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};
static const Game::GlueBootAutoRegister _glueBootReg{&PrepareForReload};

} // namespace Texture::Mask
