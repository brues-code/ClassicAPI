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

// Inline texture escape (`|Tpath:height:width:...|t`) backport.
//
// Vanilla 1.12 has ZERO inline-texture support: the shared `|`-tokenizer
// (FUN_005c2810) has no `T`/`t` case, so `|T...|t` renders as literal text.
// This module teaches the text engine to render the icon inline.
//
// Two working pieces:
//   1. POSITIONING — co-hook the per-line glyph emitter (FUN_005ccbe0) and, for
//      a line containing `|T…|t`, render the plain text runs by DELEGATING to the
//      original emitter per segment (threading the pen through linkState[4]) while
//      recording an icon at the pen between runs. This avoids reimplementing the
//      emitter's intricate vertex math: the engine still lays out every glyph; we
//      only track where each icon goes.
//   2. RENDERING — the 4.3.4 CSimpleEmbeddedTexture model, ported faithfully.
//      A co-hook on CSimpleFontString::RebuildString (FUN_007724A0) maps each
//      fresh text node to its OWNING FONTSTRING (1.12 chat lines are real
//      CSimpleFontStrings — the ScrollingMessageFrame's display refresh
//      FUN_00788750 SetTexts/anchors/shows one per visible line). A WorldTick
//      publisher then walks the icon records and hands each to
//      `Text::InlineTexturePool::PlaceOwned`, which configures a pooled
//      engine-managed CSimpleTexture ANCHORED TO THE OWNING FONTSTRING at the
//      pen offset — exactly how 4.3.4's UpdateEmbeddedTextures anchors its
//      embedded textures. The engine draws the region every frame (residency)
//      and the anchor system moves it with its line on every scroll/shift
//      (zero per-frame work, no render transforms, no mid-render mutation).
//      See docs/InlineTextureResidency.md.
//
// Supports the full positional payload
// `|Tpath:height:width:offsetX:offsetY:texW:texH:left:right:top:bottom:r:g:b|t`
// (size, pen offset, sprite-sheet texcoord crop, and r:g:b vertex tint). MEASURE
// width is corrected at the fs-level choke: the tokenizer still reports an icon
// as ~zero width to every measure/wrap loop (the token contract has no width
// field), but a co-hook on the internal string-width getter
// (FUN_FONTSTRING_STRING_WIDTH) re-adds the same per-icon advances the emitter
// reserves — so GetStringWidth, the tooltip auto-size, and auto-width layout
// match the rendered width — and a co-hook on the shared wrap-stepper
// dispatcher (FUN_TEXT_WRAP_STEPPER) shrinks the wrap width by the line's icon
// advances so wrapped lines break at the visible edge instead of overflowing.
// Still icon-blind (documented, accepted): the substring measure
// (FUN_00772AE0) and hyperlink hit-testing. See docs/InlineTextureEscapes.md
// for the RE map.

#include "text/InlineTexture.h"

#include "Game.h"
#include "Offsets.h"
#include "text/InlineTexturePool.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace Text::InlineTexture {

namespace {

// True if `p` looks like a readable in-process pointer (heap/.data range), so we
// can probe engine structures without faulting on a bad/uninitialized field.
// UPPER BOUND IS 4GB-ish, NOT 2GB: this client is Large Address Aware
// (VanillaFixes), so heap allocations above 0x80000000 are valid and common
// once the heap grows. A 0x7FFF0000 cap silently rejected high nodes and
// fontstrings — and because the flush's layout walk BREAKS on an "unreadable"
// node, one high node truncated the walk and every node after it lost its
// icons (the per-row random missing-icon bug).
bool LooksReadable(const void *p) {
    auto a = reinterpret_cast<uintptr_t>(p);
    return a >= 0x00010000u && a < 0xFFFF0000u;
}

// --- textured-quad rendering primitive --------------------------------------
//
// Icons draw as raw GxU quads in the text engine's OWN render space (pen +
// node-origin), the same coordinate system and dynamic VB the glyphs use — so
// position is pixel-exact by construction, with no anchor/layout-space
// conversion. Residency (the reason quads alone flickered) is handled
// separately by Text::InlineTexturePool holders: one engine-managed
// CSimpleTexture per texture path, drawn every frame, keeps that texture hot in
// VRAM for these quads to bind.

// The UI text vertex the GxU path expects (stride 0x18 = 24 bytes), from the
// glyph vertex assembler FUN_005c8710: +0x00 x +0x04 y +0x08 z +0x0c colorBGRA
// +0x10 u +0x14 v.
struct Vertex {
    float x, y, z;
    uint32_t color;
    float u, v;
};
static_assert(sizeof(Vertex) == 0x18, "text vertex must be 24 bytes");

// Byte-identical to the 5-dword on-stack descriptor FUN_00770200 builds before
// FUN_00449d90. On a successful load the loader never touches it; only the
// load-failure log path reads it, so a faithful copy keeps the missing-texture
// path as safe as the engine's own call.
struct TexLoadDesc {
    void *vtbl;
    int32_t field4;
    void *self8;
    uint32_t fieldC;
    int32_t field10;
};

using TexFlagsInit_t = void *(__thiscall *)(void *self, uint32_t blend, int, int, int, int, int,
                                            uint32_t, int);
using TextureLoad_t = uint32_t(__fastcall *)(const char *path, void *desc, uint32_t flags, int,
                                             int);

std::unordered_map<std::string, void *> g_texCache;

// Resolve a texture PATH to a bindable CGxTexture handle (cached by path).
// Mirrors FUN_00770200's load: build flags via FUN_0058a980, then
// FUN_00449d90(path, &desc, flags, 0, 1). The engine keeps its own refcounted
// by-name cache, so the handle is stable for the session.
void *LoadTextureByPath(const char *path) {
    if (path == nullptr || path[0] == '\0')
        return nullptr;
    std::string key(path);
    auto it = g_texCache.find(key);
    if (it != g_texCache.end())
        return it->second;

    TexLoadDesc desc;
    desc.vtbl = reinterpret_cast<void *>(Offsets::PTR_TEXLOAD_DESC_VTBL);
    desc.field4 = 8;
    desc.self8 = &desc.self8;
    desc.fieldC = reinterpret_cast<uintptr_t>(&desc.self8) | 1u;
    desc.field10 = 0;
    uint32_t flags = 0;
    const uint32_t blend = *reinterpret_cast<uint32_t *>(Offsets::VAR_TEXTURE_BLEND_DEFAULT);
    reinterpret_cast<TexFlagsInit_t>(Offsets::FUN_GX_TEXFLAGS_INIT)(&flags, blend, 0, 0, 0, 0, 0, 1,
                                                                    0);
    void *handle = reinterpret_cast<void *>(
        reinterpret_cast<TextureLoad_t>(Offsets::FUN_TEXTURE_LOAD_BY_PATH)(path, &desc, flags, 0,
                                                                           1));
    g_texCache.emplace(std::move(key), handle);
    return handle;
}

using GxBind_t = void(__fastcall *)(int selector, void *tex);
using GxLockVB_t = int(__fastcall *)(int zero, int stride, int vertCount);
using GxVBData_t = void *(__fastcall *)(int handle);
using GxSubmit_t = void *(__fastcall *)(int *handle, int vertCount);
using GxUnlock_t = void(__fastcall *)(int handle, int zero);

constexpr int kRingVerts = 0x800; // the paint pass always locks this many; mirror it

// Draw one textured quad through the UI text VB primitive. Must run while the
// device is in the text-paint state (from the paint co-hook, after the original
// glyph flush). Corner order TL, TR, BL, BR matches the shared quad index
// buffer ({0,1,2, 2,1,3}) and the device cull winding.
void DrawTexturedQuad(void *tex, float x0, float y0, float x1, float y1, float u0, float v0,
                      float u1, float v1, uint32_t color, float z) {
    if (tex == nullptr)
        return;
    auto lockVB = reinterpret_cast<GxLockVB_t>(Offsets::FUN_GX_LOCK_DYNAMIC_VB);
    int handle = lockVB(0, Offsets::GX_TEXT_VERTEX_STRIDE, kRingVerts);
    auto vbData = reinterpret_cast<GxVBData_t>(Offsets::FUN_GX_VB_DATA_PTR);
    auto *v = reinterpret_cast<Vertex *>(vbData(handle));
    if (v != nullptr) {
        // Resolve the HTEXTURE to its bindable CGxTexture the way the engine's
        // render does: FUN_0044acf0(tex, force=1, 0) returns [tex+0x140] and
        // drives the streaming load. Binding the raw HTEXTURE is SetTexture(0).
        auto getRenderable = reinterpret_cast<void *(__fastcall *)(void *, int, void *)>(
            Offsets::FUN_TEXTURE_GET_RENDERABLE);
        void *cgxTex = getRenderable(tex, 1, nullptr);
        if (!LooksReadable(cgxTex)) {
            reinterpret_cast<GxUnlock_t>(Offsets::FUN_GX_UNLOCK_VB)(handle, 0);
            return;
        }
        reinterpret_cast<GxBind_t>(Offsets::FUN_GX_BIND_TEXTURE)(Offsets::GX_TEXTURE_SELECTOR,
                                                                 cgxTex);
        // The UI device backend is OpenGL (bottom-left texture origin, v=0 at the
        // bottom), so top corners map to the LARGER v — else the icon renders
        // vertically flipped (obvious on directional raid-target markers).
        v[0] = {x0, y0, z, color, u0, v1};
        v[1] = {x1, y0, z, color, u1, v1};
        v[2] = {x0, y1, z, color, u0, v0};
        v[3] = {x1, y1, z, color, u1, v0};
        reinterpret_cast<GxSubmit_t>(Offsets::FUN_GX_SUBMIT_VB)(&handle, 4);
    }
    reinterpret_cast<GxUnlock_t>(Offsets::FUN_GX_UNLOCK_VB)(handle, 0);
}

// --- inline-texture descriptor parse ---------------------------------------

// Parsed
// `|Tpath:height:width:offsetX:offsetY:texW:texH:left:right:top:bottom:r:g:b|t`
// payload. Trailing fields are optional; texcoords crop a sprite sheet (e.g. the
// raid-target icons) to one cell, and r:g:b (0-255) tint the icon.
struct IconDesc {
    std::string path;
    float height = 0.0f;
    float width = 0.0f;   // defaults to height when the width field is absent/0
    float offsetX = 0.0f; // pen-relative pixel shift
    float offsetY = 0.0f;
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f; // full texture by default
    // Vertex colour (modulates the texture). Packed 0xAARRGGBB — the same order
    // the engine builds for `|cAARRGGBB` text (verified in the tokenizer's colour
    // path), so full white = no tint. Set from the optional r:g:b (0-255) fields.
    uint32_t color = 0xFFFFFFFFu;
};

// Reads a numeric field starting at `s` (bounded by `end`), stopping at the next
// ':' or the end. Returns the parsed value; sets `next` past the field's
// terminating ':' (or to `end`). Tolerates leading spaces and decimals.
float ParseField(const char *s, const char *end, const char **next) {
    char buf[32];
    int n = 0;
    const char *p = s;
    while (p < end && *p != ':' && n < static_cast<int>(sizeof(buf) - 1)) {
        buf[n++] = *p;
        ++p;
    }
    buf[n] = '\0';
    // Skip to just past the delimiter for the caller's next field.
    while (p < end && *p != ':')
        ++p;
    if (p < end && *p == ':')
        ++p;
    *next = p;
    return static_cast<float>(atof(buf));
}

// Parses the payload between `|T` and `|t` (`payload`, length `len`) into `out`.
// Returns false if there's no path or no positive height (nothing to draw).
bool ParseIcon(const char *payload, size_t len, IconDesc &out) {
    const char *end = payload + len;
    const char *colon = payload;
    while (colon < end && *colon != ':')
        ++colon;
    if (colon == payload)
        return false; // empty path
    out.path.assign(payload, static_cast<size_t>(colon - payload));

    if (colon >= end)
        return false; // path with no height

    // Parse up to 13 numeric fields after the path: height, width, offsetX,
    // offsetY, texW, texH, left, right, top, bottom, r, g, b.
    float f[13] = {0};
    int nf = 0;
    const char *p = colon + 1;
    while (p < end && nf < 13) {
        const char *nx = p;
        f[nf++] = ParseField(p, end, &nx);
        p = nx;
    }
    if (nf < 1)
        return false; // need at least the height field
    // height/width of 0 means "auto" — resolved to the line's font height at emit
    // time (retail's `:0:0` convention, e.g. GetCoinTextureString coins).
    out.height = f[0];
    out.width = (nf >= 2 && f[1] > 0.0f) ? f[1] : out.height;
    out.offsetX = (nf >= 3) ? f[2] : 0.0f;
    out.offsetY = (nf >= 4) ? f[3] : 0.0f;
    // texW=f[4] texH=f[5] left=f[6] right=f[7] top=f[8] bottom=f[9] -> normalized
    // texcoords cropping the sheet to one cell.
    if (nf >= 10 && f[4] > 0.0f && f[5] > 0.0f) {
        out.u0 = f[6] / f[4];
        out.u1 = f[7] / f[4];
        out.v0 = f[8] / f[5];
        out.v1 = f[9] / f[5];
    }
    // r=f[10] g=f[11] b=f[12] (0-255) -> vertex tint. All three required (they're
    // the last positional fields); clamped to a byte and packed 0xFFrrggbb.
    if (nf >= 13) {
        auto clampByte = [](float v) -> uint32_t {
            if (v < 0.0f)
                v = 0.0f;
            if (v > 255.0f)
                v = 255.0f;
            return static_cast<uint32_t>(v + 0.5f);
        };
        out.color = 0xFF000000u | (clampByte(f[10]) << 16) | (clampByte(f[11]) << 8) |
                    clampByte(f[12]);
    }
    return true;
}

// --- per-node recorded icons -----------------------------------------------

// One icon to draw, in the render node's node-local coordinate space (the same
// space glyph verts live in — the paint pass translates it by the node origin).
struct IconRecord {
    std::string path;       // texture path (also the residency-holder key)
    void *tex;              // bindable HTEXTURE (from LoadTextureByPath)
    float x;                // node-local pen x at the icon
    float y;                // node-local pen reference (penXYZ[1]) — near the text top
    float fontH;            // font pixel height of the line, for vertical centering
    float w;
    float h;
    float z;                // pen z (penXYZ[2]) — quad depth
    float offsetX, offsetY; // pen-relative pixel shift
    float u0, v0, u1, v1;   // texture crop
    uint32_t color;         // vertex tint (0xAARRGGBB; white = untinted)
};

// Icons keyed by render node, VERSIONED BY BUILD: `builtSeq` is the builder
// invocation that produced the records. The flush only trusts records whose
// builtSeq matches the node's LATEST build (g_nodeBuilt) — a build that never
// ran the emitter (empty text) leaves records from a previous life at a reused
// address, and unversioned records ghosted: pfUI nameplate fontstrings churn
// node addresses constantly, inherited dead emote records, passed the
// authority check (genuinely their current node), and drew emote icons at the
// parked nameplate position at the bottom of the screen.
struct NodeIcons {
    uint32_t builtSeq = 0;
    std::vector<IconRecord> icons;
};
std::unordered_map<void *, NodeIcons> g_nodeIcons;

// node → owning CSimpleFontString, recorded by the RebuildString co-hook (the
// 4.3.4 ownership model — 1.12 chat lines ARE fontstrings). Consulted by the
// region render mode to anchor icon regions to their line. Stale fs pointers
// are guarded by LooksReadable at use (chat/bubble fontstrings are pooled and
// long-lived); the map clears on /reload.
std::unordered_map<void *, void *> g_nodeOwner;

// Flush visit stamps: which flush call last WALKED each owned node. Lets the
// Broken() diagnostic distinguish "node painted but queue dropped" from "node's
// layout is never painted at all" (the two remaining theories for a visible
// markup line with an empty want).
uint32_t g_flushSeq = 0;
std::unordered_map<void *, uint32_t> g_nodeSeen;

// Render mode: 0 = raw quads (pen space, pixel-true, needs residency holders),
// 1 = engine regions (4.3.4 model — resident by construction; the default), 2 =
// both (bring-up A/B: if the region placement is right, the two coincide).
int g_renderMode = 1;

// Cached pen-units-per-anchor-unit scale (K). Derived per flush from any icon
// node whose fontstring rect is resolved: the node origin is the fontstring's
// justification anchor point mapped by K (verified empirically: two probe
// fontstrings gave origin = rectJustifyRef × K with K ≈ 1470, both axes, no
// offset). 0 until first derivation → region placement waits for it.
float g_penPerAnchor = 0.0f;

// Region-mode calibration, PEN units added to the region copy's position only
// (quads are the ground truth and never shifted). x needs no constant — the +3
// once calibrated here turned out to be the missing lead pad, now applied at
// draw for both render paths. y keeps a 1px residual (the pen→anchor map's one
// true constant). Tunable live via _classicapi_InlineTexRegionCal(dx, dy).
float g_regionCalX = 0.0f;
float g_regionCalY = -1.0f;

// Draw-builder co-hook: exact build boundaries for first-line detection. The
// builder runs once per node build and calls the emitter per wrapped line;
// stamping the node + zeroing the emit counter here lets the emitter know
// "this is the first line of a fresh build" with certainty. (The previous
// heuristic — comparing the emit text pointer against node+text — failed on
// pfUI-processed lines and caused both the erased-live-records bug and the
// ghost-icons-on-reused-nodes bug.)
void *g_buildNode = nullptr;
uint32_t g_buildEmitSeq = 0;
// Monotonic builder-invocation counter + per-node latest build. Records carry
// the build they were made in; the flush rejects records older than the node's
// latest build (see NodeIcons).
uint32_t g_buildCounter = 0;
std::unordered_map<void *, uint32_t> g_nodeBuilt;
// Last emitter outcome per node (diagnostic): 1 feature-off delegate, 2 null
// args, 3 manual suppress, 4 focused-editbox pointer suppress, 5 content
// suppress, 6 editable suppress, 7 no-markup line, 8 recorded icons.
std::unordered_map<void *, uint8_t> g_nodeEmit;

using DrawBuilder_t = void(__fastcall *)(void *node);
DrawBuilder_t g_builderOriginal = nullptr;

void __fastcall DrawBuilder_h(void *node) {
    g_buildNode = node;
    g_buildEmitSeq = 0;
    g_nodeBuilt[node] = ++g_buildCounter;
    g_builderOriginal(node);
    g_buildNode = nullptr;
}

static const Game::HookAutoRegister _builderHook{Offsets::FUN_TEXT_DRAW_BUILDER,
                                                 reinterpret_cast<void *>(&DrawBuilder_h),
                                                 reinterpret_cast<void **>(&g_builderOriginal)};

// Node-free co-hook: THE lifetime fix. Every gxu text node dies through
// FUN_TEXT_NODE_FREE (single engine call site) before its address goes back to
// the free list for reuse. Erasing all per-node state here makes it a hard
// invariant that anything in our maps refers to a LIVE node — the entire class
// of stale-record/stale-owner bugs (inherited ghost icons on recycled
// nameplates, orphaned records inflating dropNoFs, reload leftovers) becomes
// structurally impossible rather than heuristically guarded.
using NodeFree_t = void(__fastcall *)(void *node);
NodeFree_t g_nodeFreeOriginal = nullptr;

void __fastcall NodeFree_h(void *node) {
    if (node != nullptr) {
        g_nodeIcons.erase(node);
        g_nodeOwner.erase(node);
        g_nodeSeen.erase(node);
        g_nodeBuilt.erase(node);
        g_nodeEmit.erase(node);
    }
    g_nodeFreeOriginal(node);
}

static const Game::HookAutoRegister _nodeFreeHook{Offsets::FUN_TEXT_NODE_FREE,
                                                  reinterpret_cast<void *>(&NodeFree_h),
                                                  reinterpret_cast<void **>(&g_nodeFreeOriginal)};

// RebuildString co-hook: after the engine (re)builds a fontstring's text block,
// record node → fontstring. `fs+0xF8` is an HTEXTBLOCK handle; the node lives
// at handle+8.
using RebuildString_t = void(__fastcall *)(void *fs);
RebuildString_t g_rebuildOriginal = nullptr;

void __fastcall RebuildString_h(void *fs) {
    g_rebuildOriginal(fs);
    if (!LooksReadable(fs))
        return;
    void *block = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(fs) +
                                             Offsets::OFF_FONTSTRING_TEXT_BLOCK);
    if (!LooksReadable(block))
        return;
    void *node = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(block) +
                                            Offsets::OFF_TEXTBLOCK_NODE);
    if (LooksReadable(node))
        g_nodeOwner[node] = fs;
}

static const Game::HookAutoRegister _rebuildHook{Offsets::FUN_FONTSTRING_REBUILD_STRING,
                                                 reinterpret_cast<void *>(&RebuildString_h),
                                                 reinterpret_cast<void **>(&g_rebuildOriginal)};

// Derives K for THIS node from its own fontstring: K = originX / justifyRefX
// (node+0x54: 0 = left, 1 = centre, 2 = right — the draw builder's encoding).
// K is PER-FONTSTRING, not global: pen space is scaled by the fs's effective
// scale chain, so a pfUI money display at a custom frame scale has a different
// K than chat (the deterministic 0.53× half-size icons were exactly this —
// a global chat-derived K applied to a differently-scaled fs). The node's own
// derivation is preferred; the global cache is a fallback for nodes whose
// justify ref sits too close to 0 to divide by (guarded), updated only within
// ±10% (a mid-layout stale-rect read must not poison it).
float DeriveK(const uint8_t *n, const uint8_t *f, float originX) {
    if (LooksReadable(f)) {
        const float left = *reinterpret_cast<const float *>(f + Offsets::OFF_REGION_RECT + 4);
        const float right = *reinterpret_cast<const float *>(f + Offsets::OFF_REGION_RECT + 12);
        const float insetX = *reinterpret_cast<const float *>(f + Offsets::OFF_FONTSTRING_INSET_X);
        if (left < right && std::fabs(insetX) < 1e-6f) {
            const int justify = *reinterpret_cast<const int *>(n + 0x54);
            const float ref =
                (justify == 1) ? (left + right) * 0.5f : ((justify == 2) ? right : left);
            if (std::fabs(ref) > 0.02f && originX > 1.0f) {
                const float k = originX / ref;
                if (k > 1.0f) {
                    if (g_penPerAnchor <= 1.0f ||
                        (k > g_penPerAnchor * 0.9f && k < g_penPerAnchor * 1.1f))
                        g_penPerAnchor = k;
                    return k; // this node's own K — always right for its fs
                }
            }
        }
    }
    return g_penPerAnchor;
}

// Runtime toggle (default ON — the feature is proven; `_classicapi_InlineTexEnable(false)`
// still turns it off). When off, the emitter/tokenizer co-hooks fast-path straight
// to the originals.
bool g_inlineEnabled = true;
// Manual suppression override (Lua _classicapi_InlineTexSuppress) — normally
// unused. Editbox exclusion is per-node via NodeEditable (below), not a global.
bool g_suppressInline = false;

// True while an EditBox has keyboard focus (the engine's cursor global is set).
// Suppression keys on the focused editbox's BUFFER, not this global (see
// TextInFocusedEditbox below); kept only for the manual-override edge.
inline bool InputFocused() {
    return *reinterpret_cast<void *const *>(Offsets::VAR_FOCUSED_EDITBOX) != nullptr;
}

// True if `t` points into [buf, buf+strlen(buf)] — a TIGHT, exact extent (bounded
// strlen, cap 0x4000). The editbox's buffers are distinct heap allocations, so an
// in-range pointer is unambiguously that buffer's, with no false-match cap. Guards
// every deref: a null/unreadable buffer or below-range pointer just returns false.
bool PtrInBuffer(const uint8_t *t, const uint8_t *buf) {
    if (!LooksReadable(buf) || t < buf)
        return false;
    size_t n = 0;
    while (n < 0x4000u && buf[n] != '\0')
        ++n;
    return t <= buf + n;
}

// The focused editbox's INPUT buffer ([fe+0x32C]/[fe+0x334]), or nullptr. This is
// the buffer the caret/width MEASURE path reads IN PLACE (FUN_0077da80 →
// FUN_00772ae0 → … → FUN_005c6940 loops the tokenizer), so the measure tokenizer's
// `text` points directly into it.
const uint8_t *FocusedEditboxInput() {
    const void *fe = *reinterpret_cast<const void *const *>(Offsets::VAR_FOCUSED_EDITBOX);
    if (!LooksReadable(fe))
        return nullptr;
    auto *f = reinterpret_cast<const uint8_t *>(fe);
    const uint8_t sel = f[Offsets::OFF_EDITBOX_BUFFER_SELECT];
    const uint8_t *inBuf = *reinterpret_cast<const uint8_t *const *>(
        f + ((sel & 8u) ? Offsets::OFF_EDITBOX_BUFFER_MASKED : Offsets::OFF_EDITBOX_BUFFER));
    return LooksReadable(inBuf) ? inBuf : nullptr;
}

// True if `text` points into the focused editbox's input buffer — the MEASURE path
// (caret/GetStringWidth), which must measure RAW `|T…|t` so the caret stays aligned
// with the raw glyphs. Per-editbox, so a display FontString measured while an
// editbox is focused still measures icons (an improvement over the old global).
// Cheap: one pointer-range check on pipe-leading tokens, null-short-circuited.
bool TextInFocusedEditbox(const void *text) {
    return PtrInBuffer(reinterpret_cast<const uint8_t *>(text), FocusedEditboxInput());
}

// True if `text`'s content equals the focused editbox's input text. The editbox
// RENDERS its content through a TRANSIENT copy — same bytes, different allocation,
// no pointer link to the editbox (verified in-game: the render buffer matched the
// input buffer's string exactly but sat in an unrelated heap block). The emitter
// gets that whole copy as one line, so a content compare identifies it. Bounded to
// 0x1000 bytes; only reached on a pipe-leading emit line while an editbox is focused.
bool EmitLineIsFocusedEditbox(const uint8_t *text) {
    const uint8_t *inBuf = FocusedEditboxInput();
    if (inBuf == nullptr || !LooksReadable(text))
        return false;
    // Compare render vs input with `||` collapsed to a single `|` on BOTH sides.
    // Different editboxes handle typed escapes differently: some render the text
    // as-is (render == input buffer, both single-pipe), some neutralize each `|`
    // to `||` in the render while the buffer stays single-pipe. Collapsing pairs
    // on both sides makes the compare robust to either, so the focused input's OWN
    // line is recognized (and shown raw) regardless of doubling — no flag gate
    // (which mis-fires on pfUI's editable chat display) needed. A chat history
    // line's content differs, so it never matches and keeps its icons.
    size_t r = 0, in = 0;
    for (int guard = 0; guard < 0x1000; ++guard) {
        uint8_t a = text[r];
        if (a == '|' && text[r + 1] == '|') {
            r += 2; // collapse `||` -> `|`
        } else {
            r += 1;
        }
        uint8_t b = inBuf[in];
        if (b == '|' && inBuf[in + 1] == '|') {
            in += 2; // collapse `||` -> `|`
        } else {
            in += 1;
        }
        if (a != b)
            return false;
        if (a == '\0')
            return true; // both reached the terminator together
    }
    return true;
}

// Text range currently being delegated to the ORIGINAL emitter from a suppressed
// editbox render. The re-entrant tokenizer stands down across this whole span (not
// just the first token) so the delegated raw layout doesn't eat `|T` as a
// zero-width token. Single-threaded engine → a plain pair is safe.
const uint8_t *g_reentryLo = nullptr;
const uint8_t *g_reentryHi = nullptr;
inline bool TextInReentry(const uint8_t *t) {
    return g_reentryLo != nullptr && t >= g_reentryLo && t < g_reentryHi;
}

// The tokenizer's stand-down environment, factored into ONE predicate so measure
// and render can never disagree: true when inline-texture interception is ACTIVE
// for `text` (spans are eaten as zero-width tokens and icons render). `editable`
// is the caller's editbox bit at its own representation level — gxu flags bit
// 0x40 at the tokenizer, fs+0x120 bit 0x1000 at the string-width co-hook
// (FUN_0044D670 translates the latter into the former, so both call shapes
// evaluate the same engine bit). The tokenizer's `text[0] == '|'` position check
// stays at its call site — it's about where the tokenizer stands in the text,
// not about whether the feature is active.
bool InlineInterceptActive(const uint8_t *text, bool editable) {
    return g_inlineEnabled && !g_suppressInline && !editable && text != nullptr &&
           !TextInReentry(text) && !TextInFocusedEditbox(text);
}

// A text node's flags (`[node+0x5c]`) bit 6 (0x40) distinguishes editable input
// text (set on the macro editbox: flags 0x4D) from display text (chat 0x20D,
// FontStrings 0x0D — bit 6 clear). It's a per-node property the emitter and
// tokenizer don't otherwise use, and it rides in the tokenizer's flags argument,
// so we can suppress inline rendering per node — covering editboxes the focus
// global misses (multi-line editors build once, un-focused). This mirrors 4.3.4's
// per-render texture-disable flag, adapted to 1.12's layout.
inline bool NodeEditable(const void *node) {
    return (*reinterpret_cast<const uint32_t *>(reinterpret_cast<const uint8_t *>(node) +
                                                Offsets::OFF_TEXT_NODE_FLAGS) &
            0x40u) != 0;
}

float g_vBias = 0.0f;     // extra node-local Y added to the icon centre (fine-tune)
float g_sizeScale = 1.0f; // multiplies the parsed icon size
// Icon vertical centre = penY + fontHeight * centerFrac. penY sits near the text
// top; ~0.6 centres the icon on the line across the fonts we render into (chat +
// pfUI's bubble), tuned in-game via _classicapi_InlineTexTune.
float g_centerFrac = 0.6f;
// Horizontal breathing room around every inline icon, as a fraction of the icon's
// width. Applied FULL on the lead (left) and HALF on the trail (right): the
// preceding glyph's right-side bearing extends past the reported pen and eats into
// the left gap visually, so a lead-heavy split makes the left and right gaps LOOK
// even. Without any pad an icon crowds the char before it. Tuned via
// _classicapi_InlineTexTune (4th arg). 0.18 ≈ 3px lead / 1.5px trail on an 18px icon.
float g_iconPadFrac = 0.18f;

// The 1.12 FontString text sanitizer DOUBLES any pipe that doesn't begin a
// recognized escape (so it renders as a literal `|`). Since 1.12 doesn't know
// `|T`, a caller's `|Tpath:h|t` arrives at the emitter as `||Tpath:h||t`
// (verified in-game: emitter text length +2, pipes doubled). So we accept both the
// doubled form (the real-world case) and a clean `|T` (should one ever reach us
// undoubled, e.g. after a future sanitizer hook). A literal user `||T…||t` is
// effectively nonexistent, and ordinary doubled pipes (`|| `) never match because
// the char after must be `T`.

// If an inline-texture escape starts at text[i], returns its opening-marker
// length: 3 for the doubled `||T`, 2 for a clean `|T`; 0 otherwise. The doubled
// form is checked first so the leading `|` of `||T` wins over reading the 2nd `|`
// as a clean `|T`.
int IconStartLen(const uint8_t *text, int len, int i) {
    if (i + 2 < len && text[i] == '|' && text[i + 1] == '|' && text[i + 2] == 'T')
        return 3;
    if (i + 1 < len && text[i] == '|' && text[i + 1] == 'T')
        return 2;
    return 0;
}

// True if [text,text+len) contains any inline-texture escape.
bool HasInlineTexture(const uint8_t *text, int len) {
    for (int i = 0; i + 1 < len; ++i)
        if (IconStartLen(text, len, i))
            return true;
    return false;
}

// Finds the matching close (`||t` for the doubled form, `|t` for clean) at or
// after `from`. Returns its offset and sets *closeLen, or npos.
size_t FindIconClose(const uint8_t *text, int len, size_t from, bool doubled, int *closeLen) {
    const size_t n = static_cast<size_t>(len);
    if (doubled) {
        for (size_t i = from; i + 2 < n; ++i)
            if (text[i] == '|' && text[i + 1] == '|' && text[i + 2] == 't') {
                *closeLen = 3;
                return i;
            }
    } else {
        for (size_t i = from; i + 1 < n; ++i)
            if (text[i] == '|' && text[i + 1] == 't') {
                *closeLen = 2;
                return i;
            }
    }
    return static_cast<size_t>(-1);
}

// Length of an inline-texture escape starting at `text[0]` (through its close
// marker), or 0 if none. Null-terminated variant for the tokenizer hook, which
// gets no length. Bounded so a malformed/unterminated span can't run away.
int InlineSpanLen(const uint8_t *text) {
    int mlen;
    if (text[0] == '|' && text[1] == '|' && text[2] == 'T')
        mlen = 3; // doubled ||T
    else if (text[0] == '|' && text[1] == 'T')
        mlen = 2; // clean |T
    else
        return 0;
    const bool doubled = (mlen == 3);
    for (int i = mlen; i < 2048 && text[i] != '\0'; ++i) {
        if (doubled) {
            if (text[i] == '|' && text[i + 1] == '|' && text[i + 2] == 't')
                return i + 3;
        } else {
            if (text[i] == '|' && text[i + 1] == 't')
                return i + 2;
        }
    }
    return 0; // unterminated → let the engine treat it as ordinary text
}

// THE per-icon pen advance the emitter reserves: icon width + full lead pad +
// half trail pad + any positive offsetX (a negative offsetX — deliberate
// leftward overlap — doesn't shrink the advance). Single source shared by the
// emitter's icon loop, its centre/right-justify pre-shift, and the string-width
// co-hook — the three MUST agree or measured and rendered width drift.
// `fontHPen` resolves the retail `:0` auto-size (icon dims default to the
// line's font height); pen units in, pen units out.
float IconAdvancePen(const IconDesc &d, float fontHPen) {
    const float baseH = (d.height > 0.0f) ? d.height : fontHPen;
    const float baseW = (d.width > 0.0f) ? d.width : baseH;
    const float w = baseW * g_sizeScale;
    const float offX = d.offsetX * g_sizeScale;
    return w + 1.5f * (w * g_iconPadFrac) + (offX > 0.0f ? offX : 0.0f);
}

// Sum of IconAdvancePen over every well-formed `|T…|t` (or sanitizer-doubled
// `||T…||t`) span in [text, text+len). Malformed/unterminated spans contribute
// nothing — mirroring the emitter, which renders them as plain text.
float SumIconAdvances(const uint8_t *text, int len, float fontHPen) {
    float sum = 0.0f;
    int i = 0;
    while (i < len) {
        const int ml = IconStartLen(text, len, i);
        if (ml == 0) {
            // Skip an escaped pipe as a pair so its 2nd `|` isn't re-read as a
            // clean `|T` next iteration (same walk the emitter does).
            if (text[i] == '|' && i + 1 < len && text[i + 1] == '|')
                i += 2;
            else
                ++i;
            continue;
        }
        int cl = 0;
        const size_t ce = FindIconClose(text, len, static_cast<size_t>(i) + ml, ml == 3, &cl);
        if (ce == static_cast<size_t>(-1))
            break; // unterminated → the rest is plain text
        IconDesc d;
        if (ParseIcon(reinterpret_cast<const char *>(text) + i + ml,
                      ce - (static_cast<size_t>(i) + ml), d))
            sum += IconAdvancePen(d, fontHPen);
        i = static_cast<int>(ce) + cl;
    }
    return sum;
}

// --- tokenizer co-hook (measure/wrap correction) ---------------------------

// FUN_005c2810 — the shared `|`-escape tokenizer. Returns a token-type code,
// writes bytes-consumed to *bytesConsumed and the payload char to *payloadOut.
using Tokenizer_t = uint32_t(__fastcall *)(uint8_t *text, int *bytesConsumed, uint32_t *colorOut,
                                           uint32_t flags, uint32_t *payloadOut);
Tokenizer_t g_tokenizerOriginal = nullptr;

uint32_t __fastcall Tokenizer_h(uint8_t *text, int *bytesConsumed, uint32_t *colorOut,
                                uint32_t flags, uint32_t *payloadOut) {
    // Intervene at a pipe when enabled, not manually suppressed, flags bit 0x40
    // (EDITABLE) clear, and the text is NOT the focused editbox's own text.
    //
    // Bit 0x40 (EDITABLE, set on the macro editor 0x4D) means "leave `|T` as
    // literal" — it keeps the macro editor's measure literal so its caret stays
    // aligned with the raw glyphs the emitter draws for it. Chat DISPLAY is
    // editable=0 (flags 0x205, ICON-NODE probe), so this does NOT touch it — its
    // icons still measure ~zero. Single-line inputs (chat / name box) lack bit 6
    // and are handled by the two focused-input signals:
    //   • MEASURE (caret/GetStringWidth loops this tokenizer over the input buffer
    //     in place) → TextInFocusedEditbox: `text` points into that buffer.
    //   • Re-entrant RENDER (the suppressed emitter delegates the raw line to the
    //     original, which re-enters here) → TextInReentry: `text` is inside the
    //     line span the emitter bracketed. Without it the span is eaten as a
    //     zero-width token and the editbox draws BLANK.
    if (text != nullptr && text[0] == '|' &&
        InlineInterceptActive(text, (flags & 0x40u) != 0)) {
        const int span = InlineSpanLen(text);
        if (span > 0) {
            // Consume the whole escape as one glyph-type token with a payload that
            // resolves to no glyph — so every measure/wrap caller advances past the
            // path text and counts it as near-zero width instead of ~40 literal
            // characters. The emitter detects icons itself and is unaffected (it
            // only ever delegates plain, `|T`-free segments).
            if (bytesConsumed)
                *bytesConsumed = span;
            if (payloadOut)
                *payloadOut = 0;
            return 6; // ordinary-glyph token type
        }
    }
    return g_tokenizerOriginal(text, bytesConsumed, colorOut, flags, payloadOut);
}

static const Game::HookAutoRegister _tokenizerHook{Offsets::FUN_TEXT_TOKENIZER,
                                                   reinterpret_cast<void *>(&Tokenizer_h),
                                                   reinterpret_cast<void **>(&g_tokenizerOriginal)};

// --- string-width co-hook (measure-width icon fix) --------------------------
//
// FUN_FONTSTRING_STRING_WIDTH = CSimpleFontString::GetStringWidthInternal (see
// Offsets.h for the full derivation). The tokenizer hook above makes every
// measure path count a `|T` span as ~zero while the emitter reserves the real
// advance — so GetStringWidth, the GameTooltip auto-size, and auto-width layout
// undercount by the icons' widths. Fix at the single fs-level choke: call the
// original, then, when the tokenizer WOULD have eaten the spans (the shared
// InlineInterceptActive predicate) and the fs isn't an editbox, add the same
// per-icon advances the emitter reserves (the shared IconAdvancePen math).
//
// UNITS: escape sizes ("|T…:16|t") are UI pixels; the internal getter returns
// ANCHOR units. Convert px→anchor with the same global factor the Script push
// chain uses: K = [VAR_UI_COORD_SCALE_DIV] × 1024 / [VAR_UI_COORD_SCALE_MUL]
// (anchor→px), i.e. sum_px / K. Do NOT divide by fs+0x7C — that's the layout
// UI SCALE (~0.68-1.0), and the original's own `out / fs+0x7C` applies to a
// value FUN_0044D670 already ran through the FUN_0041AD80 gxu→internal
// converter, not to raw pixels (a /0x7C here inflated a 16px icon to +44k px
// on first flight). The `:0` auto-size default gets fontH in px as
// internal × K, which also tracks a scaled fs correctly.
//
// Idempotence: the original may serve the cached fs+0xFC — the icon sum is
// re-derived and re-added on EVERY call, and the cache is NEVER written.
//
// Residuals (documented in docs/InlineTextureEscapes.md): the measure loop ends
// on the last glyph's INK width rather than its advance, so a trailing icon
// leaves a ~≤1px artifact; wrap-break (FUN_00772B60), substring measure
// (FUN_00772AE0), and hyperlink hit-test stay icon-blind. The focused chat
// editbox's own display fontstring (editable bit CLEAR — only multi-line
// editors carry it) can reach this through the caret positioner's line-boundary
// branch with a content-suppressed raw render; that branch is multi-line-only
// in practice, so no content compare is spent here.
using StringWidthInternal_t = float(__fastcall *)(void *fs);
// FUN_FONTSTRING_FONT_HEIGHT is __thiscall(fs, mode-on-stack) — dummy-EDX
// __fastcall matches the register/stack layout (established pattern).
using FsFontHeight_t = float(__fastcall *)(void *fs, void *edx, int mode);
StringWidthInternal_t g_stringWidthOriginal = nullptr;

float __fastcall StringWidth_h(void *fs) {
    const float base = g_stringWidthOriginal(fs);
    if (!LooksReadable(fs))
        return base;
    const auto *f = reinterpret_cast<const uint8_t *>(fs);
    // fs+0x120 bit 0x1000 → gxu editbox bit 0x40: an editbox measures the raw
    // markup it renders, so its width must stay unadjusted (caret alignment).
    const bool editable =
        (*reinterpret_cast<const uint32_t *>(f + Offsets::OFF_FONTSTRING_MEASURE_FLAGS) &
         0x1000u) != 0;
    const uint8_t *text =
        *reinterpret_cast<const uint8_t *const *>(f + Offsets::OFF_FONTSTRING_TEXT);
    if (!LooksReadable(text) || !InlineInterceptActive(text, editable))
        return base;
    int len = 0;
    while (len < 0x4000 && text[len] != '\0')
        ++len;
    if (!HasInlineTexture(text, len))
        return base;
    const float mul = *reinterpret_cast<const float *>(Offsets::VAR_UI_COORD_SCALE_MUL);
    const float div = *reinterpret_cast<const float *>(Offsets::VAR_UI_COORD_SCALE_DIV);
    if (!(mul > 0.0f) || !(div > 0.0f))
        return base;
    const float anchorToPx = div * Offsets::UI_COORD_SCALE_UNIT / mul; // ≈1468 (the push K)
    // Font height in UI pixels for the `:0` auto-size default (internal × K).
    const float fontHPx =
        reinterpret_cast<FsFontHeight_t>(Offsets::FUN_FONTSTRING_FONT_HEIGHT)(fs, nullptr, 1) *
        anchorToPx;
    return base + SumIconAdvances(text, len, fontHPx) / anchorToPx;
}

static const Game::HookAutoRegister _stringWidthHook{
    Offsets::FUN_FONTSTRING_STRING_WIDTH, reinterpret_cast<void *>(&StringWidth_h),
    reinterpret_cast<void **>(&g_stringWidthOriginal)};

// --- wrap-stepper co-hook (icon-aware line breaks) ---------------------------
//
// FUN_TEXT_WRAP_STEPPER lays out one wrapped line per call (see Offsets.h).
// It measures the line through the tokenizer, which eats each `|T` span as
// ~zero width — so the break lands as if the icons weren't there, and the
// emitter's real advances then overflow the right edge (chat lines with
// prefix icons ran past the frame). Fix: shrink the wrapWidth argument by the
// advances of the icons in the remaining text (same IconAdvancePen math the
// emitter reserves), then call the original. `text` always points at the
// REMAINING text, so a continuation line whose icons are already behind it
// gets sum = 0 — chat prefix icons come out exact. An icon that would land on
// a LATER wrapped line still shrinks the earlier calls' width, wrapping those
// lines slightly early — the safe direction (never overflow), cosmetic only.
//
// Because all four wrap consumers route through this one dispatcher, the
// render's breaks, GetStringHeight's line count, ellipsis truncation, and the
// break arrays all shift together — no cross-consumer drift.
//
// UNITS (bit us on first flight): each caller passes fontH/wrapWidth in its
// OWN space — the draw builder passes the node's fontSize (+0x1C) and wrap
// width (+0x3C) in internal text units, while the fs-level callers (height,
// fit, break arrays — the path CHAT wraps through) pass the much smaller
// anchor-converted space. Subtracting a raw pixel advance annihilated those
// small widths to the floor and shredded icon-bearing chat lines into
// 2-glyph fragments. The space-agnostic conversion uses the engine's own
// convention: every gxu caller passes fontH in the units FUN_TEXT_FONT_HEIGHT
// expects, and the measure loops use FUN_TEXT_FONT_HEIGHT(flag, fontH) as the
// PIXEL realization of that fontH (see FUN_005c6940's final scale). So
// px→caller-units is exactly (fontH / fontHPx): compute the icon sum in true
// pixels (same value the emitter reserves), then scale by that ratio.
using WrapStepper_t = void(__fastcall *)(void *font, uint8_t *text, float fontH,
                                         float wrapWidth, int *outBreak, float *outWidth,
                                         void *outNext, float indent, uint32_t flags,
                                         uint8_t *p10);
WrapStepper_t g_wrapStepperOriginal = nullptr;

// Diagnostics ring for the wrap hook — one record per icon-bearing stepper
// call, read back via _classicapi_InlineTexWrap(n). All widths are in the
// CALLER's unit space except the *Px fields.
constexpr uint32_t kWrapDiagCount = 8;
struct WrapDiag {
    float wrapWidth = 0.0f; // width the caller passed
    float fontH = 0.0f;     // fontH the caller passed (caller units)
    float fontHPx = 0.0f;   // FUN_TEXT_FONT_HEIGHT's pixel realization of it
    int len = 0;            // text length scanned (to '\n'/'\0', cap 2048)
    int passes = 0;         // probe passes run (≤8)
    float probeS[8] = {};   // per-pass shrink candidate probed
    float sumPx[8] = {};    // per-pass icon-advance sum before the probe break
    int lineLen[8] = {};    // per-pass line length used for the sum (chars)
    int pBreakA[8] = {};    // per-pass raw outBreak from the stepper
    int pNextD[8] = {};     // per-pass raw outNext - text (-1 when null/inval)
    float pWidthA[8] = {};  // per-pass raw outWidth from the stepper
    float finalSumPx = 0.0f;
    float shrunk = 0.0f;    // width handed to the original
    int floored = 0;        // hit the minWidth floor
};
WrapDiag g_wrapDiag[kWrapDiagCount];
uint32_t g_wrapDiagNext = 0;

void __fastcall WrapStepper_h(void *font, uint8_t *text, float fontH, float wrapWidth,
                              int *outBreak, float *outWidth, void *outNext, float indent,
                              uint32_t flags, uint8_t *p10) {
    // flags bit 0x80 routes to the no-wrap path (wrapWidth unused); bit 0x40 is
    // the editbox bit the tokenizer stands down on — same predicate, same gate.
    // Positive fontH/wrapWidth gates mirror the original's own validation: a
    // shrink below zero would trip its bail-to-zero-outputs path and blank the
    // line, so the floor keeps a couple of glyphs' worth of progress instead.
    if ((flags & 0x80u) == 0 && wrapWidth > 0.0f && fontH > 0.0f &&
        InlineInterceptActive(text, (flags & 0x40u) != 0)) {
        int len = 0;
        while (len < 2048 && text[len] != '\0' && text[len] != '\n')
            ++len;
        if (HasInlineTexture(text, len)) {
            // Pixel realization of this caller's fontH — the same helper the
            // measure loops and the emitter use. px → caller units is then
            // (fontH / fontHPx); see the units note above.
            const int fontFlag = static_cast<int>((flags >> 7) & 1u);
            const float fontHPx = reinterpret_cast<float(__fastcall *)(int, float)>(
                Offsets::FUN_TEXT_FONT_HEIGHT)(fontFlag, fontH);
            if (fontHPx > 0.0f) {
                const float toUnits = fontH / fontHPx;
                const float minWidth = fontH * 2.0f;
                // Only icons that actually LAND on this line may shrink its
                // width — subtracting every icon in the remaining text made an
                // 8-icon line wrap absurdly early (one word per line). The
                // break position depends on the shrink and vice versa, so run
                // the stepper as a sandboxed probe (local out-params + a COPY
                // of the p10 in/out state byte, so the caller's first-line
                // state isn't consumed) and iterate to a fixed point: probe
                // unshrunk first (latest possible break = upper bound on the
                // line's icons), re-count icons before the resulting break,
                // re-probe with that shrink. Converges when the icon set
                // stabilizes (exact); on boundary oscillation take the larger
                // of the last two sums (wraps at most one icon early — the
                // safe direction).
                //
                // The probe width MUST be floored like the final width: a ≤0
                // probe width trips the stepper's bail-to-zero-outputs path,
                // the zeroed break falls back to lineLen = len, the sum snaps
                // back to EVERY icon, and the loop "converges" on the naive
                // whole-text shrink (the v2 bug — multi-icon lines still
                // shredded while single-icon lines worked).
                WrapDiag &d = g_wrapDiag[g_wrapDiagNext % kWrapDiagCount];
                ++g_wrapDiagNext;
                d = WrapDiag{};
                d.wrapWidth = wrapWidth;
                d.fontH = fontH;
                d.fontHPx = fontHPx;
                d.len = len;
                // Minimal-feasible-shrink search. The stepper reports the
                // line's measured (icons-at-zero) width in outWidth, so each
                // probe yields a direct feasibility check: the line renders
                // inside the frame iff its icons fit in the shrink plus the
                // spare the break left — iconSum ≤ s + (probeW − outWidth).
                // Search for the SMALLEST feasible s (fullest lines):
                //   • s = 0 feasible → no shrink at all (line fits its icons
                //     in the natural slack — the common short-message case).
                //   • infeasible with no feasible found yet → escalate to the
                //     measured deficit (iconSum − slack; strictly increasing).
                //   • once a [lo = infeasible, best = feasible] bracket
                //     exists → bisect it, feasible probes lowering best,
                //     infeasible ones raising lo, until the bracket is under
                //     a quarter-glyph. Giving up on the first stale
                //     escalation candidate (an earlier version) left the
                //     bracket unsearched and cost the Marks line a whole
                //     mark+word pair per line.
                // Bounded at 8 probes; if none lands feasible, the last
                // escalation target is used (an upper-bound shrink — wraps
                // early rather than overflowing).
                float lo = -1.0f;   // largest known-infeasible s
                float best = -1.0f; // smallest known-feasible s
                float s = 0.0f;
                for (int pass = 0; pass < 8; ++pass) {
                    int pBreak = 0;
                    float pWidth = 0.0f;
                    uint8_t *pNext = nullptr;
                    uint8_t p10copy = (p10 != nullptr) ? *p10 : 0;
                    float probeW = wrapWidth - s;
                    if (probeW < minWidth)
                        probeW = minWidth;
                    g_wrapStepperOriginal(font, text, fontH, probeW, &pBreak, &pWidth,
                                          static_cast<void *>(&pNext), indent, flags,
                                          (p10 != nullptr) ? &p10copy : nullptr);
                    // outBreak and outNext−text are both BYTE counts (verified
                    // via the diag); outBreak == len means the whole text fit.
                    int lineLen = len;
                    if (pBreak > 0 && pBreak < len)
                        lineLen = pBreak;
                    else if (pNext > text && pNext - text < len)
                        lineLen = static_cast<int>(pNext - text);
                    const float sumPx = SumIconAdvances(text, lineLen, fontHPx);
                    const float iconUnits = sumPx * toUnits;
                    float slack = probeW - pWidth;
                    if (slack < 0.0f)
                        slack = 0.0f;
                    d.probeS[pass] = s;
                    d.sumPx[pass] = sumPx;
                    d.lineLen[pass] = lineLen;
                    d.pBreakA[pass] = pBreak;
                    d.pNextD[pass] = (pNext != nullptr && pNext >= text)
                                         ? static_cast<int>(pNext - text)
                                         : -1;
                    d.pWidthA[pass] = pWidth;
                    d.passes = pass + 1;
                    // AUTO-WIDTH bail: the whole text fits its budget with at
                    // most ~a glyph of slack — the signature of a
                    // single-anchor, no-width fontstring whose wrap budget is
                    // its own text-only measure plus a ~1-glyph engine margin
                    // (measured 1.07×fontH on pfUI's addon-list labels; the
                    // half-glyph first cut missed it). There is no real frame
                    // edge to protect there: the box is synthetic, the icons
                    // draw past it harmlessly, and any shrink FABRICATES a
                    // wrap on a line the engine would never have wrapped.
                    // Real-width content stays covered: an overflowing line
                    // breaks (lineLen < len), and a fitting line in a real
                    // frame carries frame-sized slack (the Marks harness line
                    // measures ~3.5×fontH) — only a text that lands within
                    // 1.5 glyphs of exactly filling its frame gives up its
                    // shrink, and its overflow is bounded by its icon sum
                    // (the pre-hook status quo).
                    if (pass == 0 && lineLen == len && slack <= fontH * 1.5f)
                        break; // s == 0, best unset → final shrink = 0
                    // Half-glyph feasibility tolerance. An AUTO-WIDTH
                    // fontstring's wrap width IS the icon-inclusive string
                    // width (the GetStringWidth hook feeds the effective-width
                    // vmethod), so its line fits with EXACTLY zero slack —
                    // iconSum == slack up to cross-computation drift (the two
                    // hooks derive the icon sum through different unit chains,
                    // plus OUTLINE extras and the trailing-glyph ink/advance
                    // quirk). Without tolerance that drift reads "fits
                    // exactly" as "1px over", shrinks a zero-slack line, and
                    // forces a wrap that had no business existing (the
                    // TwitchEmotes addon-list label). Genuine icon overflow is
                    // >= a full icon (~1.27 fontH), so half a glyph separates
                    // drift from real overflow; the cost is that a real
                    // overflow may render up to half a glyph past the edge.
                    if (iconUnits <= s + slack + fontH * 0.5f) {
                        if (best < 0.0f || s < best)
                            best = s;
                        if (s <= 0.0f)
                            break; // can't beat zero shrink
                    } else {
                        if (s > lo)
                            lo = s;
                        if (best < 0.0f) {
                            // No feasible found yet — escalate by the
                            // measured deficit (strictly increasing).
                            float next = iconUnits - slack;
                            if (next <= s)
                                next = s + fontH;
                            s = next;
                            continue;
                        }
                    }
                    // A [lo, best] bracket exists — bisect until it's tighter
                    // than a quarter-glyph.
                    if (lo < 0.0f || best < 0.0f)
                        break; // feasible with no infeasible below → best = s
                    if (best - lo < fontH * 0.25f)
                        break;
                    s = (lo + best) * 0.5f;
                }
                const float finalShrink = (best >= 0.0f) ? best : s;
                float shrunk = wrapWidth - finalShrink;
                if (shrunk < minWidth) {
                    shrunk = minWidth;
                    d.floored = 1;
                }
                d.finalSumPx = (toUnits > 0.0f) ? (finalShrink / toUnits) : 0.0f;
                d.shrunk = shrunk;
                wrapWidth = shrunk;
            }
        }
    }
    g_wrapStepperOriginal(font, text, fontH, wrapWidth, outBreak, outWidth, outNext, indent,
                          flags, p10);
}

static const Game::HookAutoRegister _wrapStepperHook{
    Offsets::FUN_TEXT_WRAP_STEPPER, reinterpret_cast<void *>(&WrapStepper_h),
    reinterpret_cast<void **>(&g_wrapStepperOriginal)};

// --- emitter co-hook (positioning) -----------------------------------------

// FUN_005ccbe0 — the per-line glyph emitter. __thiscall(node, text, len,
// colorState, penXYZ, pageMask, linkState); declared __fastcall with a dummy edx
// (the established pattern for co-hooking __thiscall engine methods).
using Emitter_t = void(__fastcall *)(void *node, void *edx, uint8_t *text, int len,
                                     uint32_t *colorState, float *penXYZ, uint32_t *pageMask,
                                     int *linkState);
Emitter_t g_emitterOriginal = nullptr;

void __fastcall Emitter_h(void *node, void *edx, uint8_t *text, int len, uint32_t *colorState,
                          float *penXYZ, uint32_t *pageMask, int *linkState) {
    // Fast path — feature off: delegate verbatim, touching nothing.
    if (!g_inlineEnabled) {
        g_emitterOriginal(node, edx, text, len, colorState, penXYZ, pageMask, linkState);
        return;
    }

    // Suppressed — render the node's text verbatim and drop any icons previously
    // recorded for it, so editable input shows raw markup. Covers the FOCUSED
    // editbox's own text (this render line's CONTENT equals the editbox input —
    // the editbox renders through a transient copy with no pointer link), the
    // pointer path (`text` inside the input buffer, rare), any editable node (flags
    // bit 6, e.g. the un-focused macro editor), and the manual override. Per-
    // editbox, not global: a chat-history line's content differs, so its icons keep
    // rendering while an editbox is focused.
    const bool sup_manual = g_suppressInline;
    const bool sup_ptr = TextInFocusedEditbox(text);
    const bool sup_content = !sup_ptr && EmitLineIsFocusedEditbox(text);
    // Suppress on the editable bit (bit 6). Verified via the ICON-NODE probe: chat
    // DISPLAY is editable=0 (flags 0x205), the macro editor is editable=1 (0x4D),
    // the focused chat edit box is editable=0 but content-matched (0x20D). So the
    // editable bit cleanly catches the un-focused macro editor (which content-match
    // can't, since it's not the focused editbox) without touching chat display.
    // Content-match still handles the focused chat/name box.
    const bool sup_editable = node != nullptr && NodeEditable(node);
    if (sup_manual || sup_ptr || sup_content || sup_editable) {
        if (node != nullptr) {
            g_nodeIcons.erase(node);
            g_nodeEmit[node] = sup_manual ? 3 : (sup_ptr ? 4 : (sup_content ? 5 : 6));
        }
        // Bracket the delegated raw layout so the re-entrant tokenizer stands down
        // across the whole line (else it eats `|T` as a zero-width token → BLANK).
        g_reentryLo = text;
        g_reentryHi = (text != nullptr && len > 0) ? text + len : text;
        g_emitterOriginal(node, edx, text, len, colorState, penXYZ, pageMask, linkState);
        g_reentryLo = nullptr;
        g_reentryHi = nullptr;
        return;
    }

    if (node == nullptr || text == nullptr || len <= 0) {
        g_emitterOriginal(node, edx, text, len, colorState, penXYZ, pageMask, linkState);
        return;
    }

    // The draw builder walks a node's wrapped lines by calling the emitter once
    // per line on the SAME node. First-line detection comes from the builder
    // co-hook's build stamp (exact), NOT from comparing text pointers against
    // node+text — that heuristic silently failed on pfUI-processed lines,
    // leaving reused nodes' inherited records alive (ghost icons). Clear the
    // icon list once per build so wrapped lines ACCUMULATE their icons instead
    // of each wiping the previous.
    // Build-stamp detection when this emit is inside a tracked builder run
    // (exact); the old text-pointer heuristic as fallback for any emitter
    // caller that doesn't route through FUN_TEXT_DRAW_BUILDER — `false` there
    // would mean never-erase, which is how reused nodes kept dead records.
    const bool firstLine =
        (node == g_buildNode)
            ? (g_buildEmitSeq++ == 0)
            : (text == *reinterpret_cast<uint8_t **>(reinterpret_cast<uint8_t *>(node) +
                                                     Offsets::OFF_TEXT_NODE_TEXT));
    if (firstLine) {
        g_nodeIcons.erase(node);
        g_nodeEmit.erase(node); // fresh build → fresh outcome
    }

    if (!HasInlineTexture(text, len)) {
        // No inline texture on this line — render it normally. Note: firstLine
        // already erased the node's stale records above, so a REUSED node
        // address whose new text has no markup is cleaned HERE, by the emitter.
        // This is the ghost-icon protection; the flush must NOT re-derive it by
        // scanning node text (that scan read a stale/preprocessed pointer on
        // pfUI chat lines and erased LIVE records every frame — the persistent
        // iconless LFG lines).
        if (g_nodeEmit[node] != 8) // don't demote a build that recorded on an earlier line
            g_nodeEmit[node] = 7;
        g_emitterOriginal(node, edx, text, len, colorState, penXYZ, pageMask, linkState);
        return;
    }

    // This line owns inline textures — append them, rendering the plain runs by
    // delegating to the original per segment.
    NodeIcons &nodeEntry = g_nodeIcons[node];
    g_nodeEmit[node] = 8; // this build recorded icons
    nodeEntry.builtSeq = g_buildCounter; // records belong to the current build
    std::vector<IconRecord> &icons = nodeEntry.icons;

    // Bit 3 of the node flags gates the emitter's per-call batch-clear. When set
    // (the standalone-FontString case), each original call would wipe the page
    // batches, so segmenting would lose all but the last run. Handle it by doing
    // the batch-clear ONCE per build (a len-0 original call with bit 3 still set,
    // only on the first wrapped line), then clearing bit 3 so our per-segment calls
    // APPEND. Restore the flags before returning.
    uint32_t *const flagsPtr = reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(node) +
                                                            Offsets::OFF_TEXT_NODE_FLAGS);
    const uint32_t savedFlags = *flagsPtr;
    const bool batchClearMode = (savedFlags & 8u) != 0;
    if (batchClearMode) {
        if (firstLine)
            g_emitterOriginal(node, edx, text, 0, colorState, penXYZ, pageMask, linkState);
        *flagsPtr = savedFlags & ~8u;

        // Clearing bit 3 switches the engine from the FontString's UNIFORM colour
        // (RGB from SetTextColor + opacity applied wholesale) to PER-GLYPH colour
        // taken from colorState = [node+0x2c]. That field's RGB is correct, but its
        // ALPHA byte is a stale default (observed 0x07 ≈ 3%) rather than the
        // FontString's real opacity — so the segmented glyphs draw ~transparent,
        // and a FontString with a dark outline (pfUI's reskinned bubble) shows only
        // the outline: "black text". Force the glyph alpha opaque, keeping the RGB
        // (so a coloured bubble stays its colour). `|c`-driven text already carries
        // alpha 0xFF, so this is a no-op for chat lines.
        colorState[0] |= 0xFF000000u;
    }

    // Font pixel height of this line — used to centre icons vertically (penY sits
    // near the text top). Mirrors the emitter's own call: ecx = (nodeFlags>>7)&1,
    // stack = the node's font size [node+0x1c].
    const int fontFlag = static_cast<int>((savedFlags >> 7) & 1u);
    const float fontSize =
        *reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(node) +
                                   Offsets::OFF_TEXT_NODE_FONT_SIZE);
    const float fontH = reinterpret_cast<float(__fastcall *)(int, float)>(
        Offsets::FUN_TEXT_FONT_HEIGHT)(fontFlag, fontSize);

    // The original emitter never writes penXYZ[0]; we mutate it to thread the pen
    // across segments, so snapshot and restore it. The draw builder does NOT reset
    // penXYZ[0] between left-justified lines, so leaving it mutated would cascade-
    // shift every following line.
    const float startX = penXYZ[0];
    const float penY = penXYZ[1];
    const float penZ = penXYZ[2];
    float penX = penXYZ[0];

    // Centre/right-justify correction. The engine positioned the pen (penXYZ[0])
    // using this line's MEASURED width, which counts inline icons as ~zero (the
    // tokenizer measure gap). The rendered line is wider by the icons' reserved
    // width, so a centred line overflows its box to the right (empty space on the
    // left) and a right-aligned line overflows past the right edge. Pre-sum this
    // line's icon widths and shift the pen left by half that (centre) or all of it
    // (right) so the whole text+icons block is justified as a unit. Left-justify
    // (chat, justify 0) needs no shift. node+0x54: 1 = centre, 2 = right (verified
    // in the draw builder FUN_005cdc20's justify branch).
    const int justify =
        *reinterpret_cast<const int *>(reinterpret_cast<const uint8_t *>(node) + 0x54);
    if (justify == 1 || justify == 2) {
        // Shared helper so the pre-shift matches the real advances exactly
        // (including the positive-offsetX term an earlier inline copy omitted).
        const float iconW = SumIconAdvances(text, len, fontH);
        penX -= (justify == 1) ? iconW * 0.5f : iconW;
    }

    // Draws a plain run [start,start+n) via the original emitter, threading the
    // pen: the original starts at penXYZ[0] and leaves the final node-local pen x
    // (truncated to int) in linkState[4]. n == 0 is a safe no-op that just finalizes
    // linkState (used to keep the pen/link state consistent between and after icons).
    auto drawRun = [&](size_t start, int n) {
        penXYZ[0] = penX;
        g_emitterOriginal(node, edx, text + start, n, colorState, penXYZ, pageMask, linkState);
        // linkState[4] holds the final pen x, stored by the engine with `FSTP dword`
        // — i.e. a FLOAT bit pattern, not an int. Read it as a float (an int-cast
        // reinterprets the bits and yields garbage).
        penX = *reinterpret_cast<float *>(&linkState[4]);
    };

    size_t runStart = 0;
    int i = 0;
    while (i < len) {
        if (text[i] != '|') {
            ++i;
            continue;
        }
        int mlen = IconStartLen(text, len, i);
        if (mlen == 0) {
            // Not an icon. Skip an escaped pipe as a pair so its 2nd `|` isn't
            // re-read as a clean `|T` next iteration; otherwise advance one.
            if (i + 1 < len && text[i + 1] == '|')
                i += 2;
            else
                ++i;
            continue;
        }
        const bool doubled = (mlen == 3);
        int closeLen = 0;
        size_t close = FindIconClose(text, len, static_cast<size_t>(i) + mlen, doubled, &closeLen);
        if (close == static_cast<size_t>(-1)) {
            // Unterminated on this line — treat the rest as plain text.
            break;
        }
        // Render the plain run before the icon (finalizes pen at the icon).
        drawRun(runStart, i - static_cast<int>(runStart));

        IconDesc d;
        const char *payload = reinterpret_cast<const char *>(text) + i + mlen;
        size_t payloadLen = close - (static_cast<size_t>(i) + mlen);
        if (ParseIcon(payload, payloadLen, d)) {
            // height/width of 0 => size to the line's font (retail :0:0).
            const float baseH = (d.height > 0.0f) ? d.height : fontH;
            const float baseW = (d.width > 0.0f) ? d.width : baseH;
            const float w = baseW * g_sizeScale;
            const float h = baseH * g_sizeScale;
            IconRecord r;
            r.path = d.path;
            r.tex = LoadTextureByPath(d.path.c_str());
            r.x = penX;
            r.y = penY;
            r.fontH = fontH;
            r.w = w;
            r.h = h;
            r.z = penZ;
            r.offsetX = d.offsetX * g_sizeScale;
            r.offsetY = d.offsetY * g_sizeScale;
            r.u0 = d.u0;
            r.v0 = d.v0;
            r.u1 = d.u1;
            r.v1 = d.v1;
            r.color = d.color;
            icons.push_back(std::move(r));
            // Reserve the icon width + a LEAD pad (full) and a TRAIL pad (half) so
            // it never jams against adjacent text. Lead-heavy on purpose: the
            // preceding glyph's right-side bearing eats into the left gap visually,
            // so a symmetric layout pad looks tighter on the left / looser on the
            // right — a half trail balances that. The lead pad is applied to the
            // draw position in FlushLayout. Shared with the justify pre-shift and
            // the string-width co-hook — never inline this math.
            penX += IconAdvancePen(d, fontH);
        }
        penXYZ[0] = penX;
        i = static_cast<int>(close) + closeLen; // skip past the closing marker
        runStart = static_cast<size_t>(i);
    }

    // Trailing plain run (also finalizes pen/link state for the builder).
    drawRun(runStart, len - static_cast<int>(runStart));

    // Restore the pen origin to match the engine's own post-call invariant, and the
    // node flags (bit 3) we cleared for the segmented append.
    penXYZ[0] = startX;
    if (batchClearMode)
        *flagsPtr = savedFlags;
}

static const Game::HookAutoRegister _emitterHook{Offsets::FUN_TEXT_EMITTER,
                                                 reinterpret_cast<void *>(&Emitter_h),
                                                 reinterpret_cast<void **>(&g_emitterOriginal)};

// --- paint co-hook: draw recorded icons as quads ---------------------------

uint32_t g_faultCode = 0;
uintptr_t g_faultAddr = 0;

// Flush-gate telemetry (cumulative): how often an icon-bearing node's region
// queue was issued vs dropped, and by which gate. Read via InlineTexStats.
uint32_t g_statQueued = 0;   // QueuePlacements(fs, places) issued
uint32_t g_statDropNoFs = 0; // owner missing / authority failed
uint32_t g_statDropRect = 0; // fs rect unresolved this paint
uint32_t g_statDropK = 0;    // pen→anchor scale not yet derived
uint32_t g_statNudged = 0;   // stuck-blockless fs redirtied for engine rebuild
uint32_t g_statDropEditable = 0; // editable node queued {} over an owner fs

int CaptureFault(EXCEPTION_POINTERS *ep, uint32_t *code, uintptr_t *addr) {
    *code = ep->ExceptionRecord->ExceptionCode;
    *addr = reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}

using Paint_t = void(__fastcall *)(void *layout);
Paint_t g_paintOriginal = nullptr;

// Draws every recorded icon for the layout's live render nodes as a raw quad,
// translating from node-local coords by the node's screen origin (+0x70/+0x74) —
// the SAME transform the paint pass applies to glyph batches, so position is
// pixel-exact. Runs after the original paint (device in the text-paint state).
// Also asks the pool to hold each texture resident (a managed CSimpleTexture
// per path, engine-drawn) so the raw quad never flickers / blanks on a 2nd client.
void FlushLayout(void *layout) {
    if (!LooksReadable(layout))
        return;
    ++g_flushSeq;
    auto *L = reinterpret_cast<uint8_t *>(layout);
    const int linkOff = *reinterpret_cast<int *>(L + Offsets::OFF_TEXT_LAYOUT_NODE_LINK);
    void *node = *reinterpret_cast<void **>(L + Offsets::OFF_TEXT_LAYOUT_NODE_HEAD);

    for (int guard = 0; node != nullptr && (reinterpret_cast<uintptr_t>(node) & 1) == 0 &&
                        guard < 4096;
         ++guard) {
        if (!LooksReadable(node))
            break;
        auto *n = reinterpret_cast<uint8_t *>(node);
        void *const next = *reinterpret_cast<void **>(n + linkOff + 4);

        // Resolve the owning fontstring up front (region modes): a node with NO
        // icons must still clear its fontstring's regions — the fs may have just
        // been re-SetText'd from icon text to plain text (chat line slot reuse).
        // A node only speaks for its fontstring while it IS the fontstring's
        // CURRENT text node (fs+0xF8 → handle+8): around a rebuild, the old and
        // new node can both be walked in one paint, and letting the stale one
        // queue (especially a clear) made icons vanish nondeterministically.
        void *fs = nullptr;
        if (g_renderMode != 0) {
            auto ow = g_nodeOwner.find(node);
            if (ow != g_nodeOwner.end() && LooksReadable(ow->second)) {
                auto *of = reinterpret_cast<uint8_t *>(ow->second);
                void *block =
                    *reinterpret_cast<void **>(of + Offsets::OFF_FONTSTRING_TEXT_BLOCK);
                g_nodeSeen[node] = g_flushSeq;
                if (LooksReadable(block) &&
                    *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(block) +
                                               Offsets::OFF_TEXTBLOCK_NODE) == node) {
                    fs = ow->second;
                } else if (block == nullptr) {
                    // Stuck-blockless fs: RebuildString released the block (a
                    // SetText landed on an unresolved rect) and never rebuilt;
                    // this zombie node keeps painting the old text. Re-set the
                    // fs's rebuild-dirty bit so the engine rebuilds next update
                    // and the icon pipeline resumes. Byte write only — safe
                    // from the paint tail (consumed by the fs's own update).
                    uint8_t *flags = of + Offsets::OFF_FONTSTRING_DIRTY_FLAGS;
                    if ((*flags & 1u) == 0) {
                        *flags |= 1u;
                        ++g_statNudged;
                    }
                }
            }
        }

        auto it = g_nodeIcons.find(node);
        // Ghost guard, scanned against the FONTSTRING's text (fs+0xF0 — proven
        // trustworthy by the Broken() dumps, unlike the node's text pointer,
        // which lied on pfUI chat lines and made the old node-text scan erase
        // LIVE records). A node with records whose owning fs currently shows
        // text WITHOUT any |T (e.g. a recycled nameplate name that inherited a
        // dead emote node's address) is stale: erase and clear. False matches
        // can only false-KEEP (safe), never false-erase.
        // (NOTE: do NOT build-version records against the builder counter — the
        // builder runs every paint but only emits when dirty, so a version
        // check erases good records on every clean paint: the all-icons-gone
        // regression.)
        if (it != g_nodeIcons.end() && !it->second.icons.empty() && fs != nullptr) {
            const char *ftext = *reinterpret_cast<const char *const *>(
                reinterpret_cast<uint8_t *>(fs) + 0xF0);
            bool fsHasMarkup = false;
            if (LooksReadable(ftext)) {
                for (int k = 1; k < 2048 && ftext[k] != '\0'; ++k)
                    if (ftext[k] == 'T' && ftext[k - 1] == '|') {
                        fsHasMarkup = true;
                        break;
                    }
            }
            if (!fsHasMarkup) {
                g_nodeIcons.erase(it);
                it = g_nodeIcons.end();
            }
        }
        if (it == g_nodeIcons.end() || it->second.icons.empty()) {
            if (fs != nullptr)
                Text::InlineTexturePool::QueuePlacements(fs, {});
            node = next;
            continue;
        }

        // NOTE: no node-text stale scan here. Records are authoritative from the
        // emitter alone: a reused node address is always rebuilt → dirty → the
        // emitter runs before any flush walks it, and its firstLine erase (or
        // suppression erase) cleans stale records. The old flush-side scan of
        // node+text for |T read a stale/preprocessed pointer on pfUI chat lines
        // and ERASED LIVE RECORDS every frame — the persistent iconless LFG
        // lines (FsDump chain bucket 3).

        // Never draw over editable text (flags bit 6) — safety net for records
        // made before the emitter's editable-suppress applied.
        if (!NodeEditable(node)) {
            const float ox = *reinterpret_cast<float *>(n + Offsets::OFF_TEXT_NODE_ORIGIN_X);
            const float oy = *reinterpret_cast<float *>(n + Offsets::OFF_TEXT_NODE_ORIGIN_Y);
            const bool wantQuads = g_renderMode != 1;
            const bool wantRegions = g_renderMode != 0;
            float K = 0.0f;
            // The fs rect, read HERE in the same flush as the icon coords — a
            // coherent snapshot. Placements are stored FS-RELATIVE: an
            // apply-time rect read raced the chat relayout (SetText invalidates
            // the rect briefly), and a placement applied against a mid-relayout
            // rect parked the icon off the line — then the dedup (unchanged
            // absolute want) froze it there. Relative offsets are also
            // position-invariant, so scrolling no longer re-places anything.
            float fsLeft = 0.0f, fsBottom = 0.0f;
            bool fsRectValid = false;
            if (wantRegions) {
                K = DeriveK(n, reinterpret_cast<uint8_t *>(fs), ox);
                if (fs != nullptr) {
                    const float *rc = reinterpret_cast<const float *>(
                        reinterpret_cast<uint8_t *>(fs) + Offsets::OFF_REGION_RECT);
                    fsBottom = (rc[0] < rc[2]) ? rc[0] : rc[2];
                    fsLeft = (rc[1] < rc[3]) ? rc[1] : rc[3];
                    fsRectValid = rc[1] != rc[3]; // unresolved rect reads 0-width
                }
            }
            std::vector<Text::InlineTexturePool::Placement> places;
            for (const IconRecord &r : it->second.icons) {
                // Screen left = pen + the FULL lead pad. The emitter reserves
                // w + 1.5×pad in the advance (lead 1×, trail 0.5×) — drawing at
                // the raw pen put all of that gap AFTER the icon, which is why
                // a string-final icon (money-string copper) looked jammed
                // against its digits and grew per-coin offset hacks.
                const float cx = r.x + ox + r.offsetX + r.w * g_iconPadFrac;
                // Centre on the line: penY sits near the text top, so add a
                // fraction of the font height (plus the fine-tune bias). offsetY
                // shifts up (WoW convention), so subtract it.
                const float cy = r.y + r.fontH * g_centerFrac + oy + g_vBias - r.offsetY;
                if (wantQuads) {
                    DrawTexturedQuad(r.tex, cx, cy - r.h * 0.5f, cx + r.w, cy + r.h * 0.5f, r.u0,
                                     r.v0, r.u1, r.v1, r.color, r.z);
                    Text::InlineTexturePool::Hold(r.path.c_str());
                }
                if (wantRegions && K > 1.0f && fs != nullptr && fsRectValid) {
                    const float rx = cx + g_regionCalX;
                    const float ry = cy + g_regionCalY;
                    Text::InlineTexturePool::Placement p;
                    p.path = r.path;
                    p.x0 = rx / K - fsLeft;
                    p.y0 = (ry - r.h * 0.5f) / K - fsBottom;
                    p.x1 = (rx + r.w) / K - fsLeft;
                    p.y1 = (ry + r.h * 0.5f) / K - fsBottom;
                    p.color = r.color;
                    p.u0 = r.u0;
                    p.v0 = r.v0;
                    p.u1 = r.u1;
                    p.v1 = r.v1;
                    places.push_back(std::move(p));
                }
            }
            // The queue gate must match the per-icon build gate EXACTLY —
            // including K. If any input wasn't ready this paint (unresolved fs
            // rect, K not yet derived), SKIP the queue and retry next paint: an
            // empty queue here would HIDE the line's icons and the dedup would
            // freeze it hidden forever (identical empty re-queues never dirty).
            // That was the scroll-landing bug's second head: lines painted
            // before the session's first K derivation queued {} and stayed
            // iconless until their text changed.
            if (wantRegions) {
                if (fs == nullptr)
                    ++g_statDropNoFs;
                else if (!fsRectValid)
                    ++g_statDropRect;
                else if (!(K > 1.0f))
                    ++g_statDropK;
                else {
                    ++g_statQueued;
                    Text::InlineTexturePool::QueuePlacements(fs, std::move(places));
                }
            }
        } else if (fs != nullptr) {
            ++g_statDropEditable;
            Text::InlineTexturePool::QueuePlacements(fs, {});
        }
        node = next;
    }
}

void SafeFlush(void *layout) {
    __try {
        FlushLayout(layout);
    } __except (CaptureFault(GetExceptionInformation(), &g_faultCode, &g_faultAddr)) {
        g_inlineEnabled = false;
    }
}

void __fastcall Paint_h(void *layout) {
    g_paintOriginal(layout);
    if (g_inlineEnabled)
        SafeFlush(layout);
}

static const Game::HookAutoRegister _paintHook{Offsets::FUN_TEXT_PAINT,
                                               reinterpret_cast<void *>(&Paint_h),
                                               reinterpret_cast<void **>(&g_paintOriginal)};

// --- Lua control surface ---------------------------------------------------

double Arg(void *L, int idx, double fallback) {
    return Game::Lua::IsNumber(L, idx) ? Game::Lua::ToNumber(L, idx) : fallback;
}

// _classicapi_InlineTexEnable([on]) -> enabled
int __fastcall Script_InlineTexEnable(void *L) {
    if (Game::Lua::GetTop(L) == 0)
        g_inlineEnabled = true;
    else
        g_inlineEnabled = Game::Lua::ToBoolean(L, 1) != 0;
    Game::Lua::PushBool(L, g_inlineEnabled);
    return 1;
}

// _classicapi_InlineTexSuppress([on]) -> suppressed
int __fastcall Script_InlineTexSuppress(void *L) {
    if (Game::Lua::GetTop(L) == 0)
        g_suppressInline = true;
    else
        g_suppressInline = Game::Lua::ToBoolean(L, 1) != 0;
    Game::Lua::PushBool(L, g_suppressInline);
    return 1;
}

// _classicapi_InlineTexTune(vBias, sizeScale, centerFrac, iconPadFrac) -> the
// same four. Live geometry calibration; omitted args keep their value.
int __fastcall Script_InlineTexTune(void *L) {
    g_vBias = static_cast<float>(Arg(L, 1, g_vBias));
    g_sizeScale = static_cast<float>(Arg(L, 2, g_sizeScale));
    g_centerFrac = static_cast<float>(Arg(L, 3, g_centerFrac));
    g_iconPadFrac = static_cast<float>(Arg(L, 4, g_iconPadFrac));
    Game::Lua::PushNumber(L, g_vBias);
    Game::Lua::PushNumber(L, g_sizeScale);
    Game::Lua::PushNumber(L, g_centerFrac);
    Game::Lua::PushNumber(L, g_iconPadFrac);
    return 4;
}

// _classicapi_InlineTexStats() -> enabled, suppressed, trackedNodes, faultCode,
// queued, dropNoFs, dropRect, dropK, faultAddr (flush-gate telemetry cumulative;
// faultAddr = the faulting instruction VA when the SEH latch tripped, 0 if never).
int __fastcall Script_InlineTexStats(void *L) {
    Game::Lua::PushBool(L, g_inlineEnabled);
    Game::Lua::PushBool(L, g_suppressInline);
    Game::Lua::PushNumber(L, static_cast<double>(g_nodeIcons.size()));
    Game::Lua::PushNumber(L, static_cast<double>(g_faultCode));
    Game::Lua::PushNumber(L, static_cast<double>(g_statQueued));
    Game::Lua::PushNumber(L, static_cast<double>(g_statDropNoFs));
    Game::Lua::PushNumber(L, static_cast<double>(g_statDropRect));
    Game::Lua::PushNumber(L, static_cast<double>(g_statDropK));
    Game::Lua::PushNumber(L, static_cast<double>(g_faultAddr));
    Game::Lua::PushNumber(L, static_cast<double>(g_statNudged));
    Game::Lua::PushNumber(L, static_cast<double>(g_statDropEditable));
    return 11;
}

// _classicapi_InlineTexMode([n]) -> mode. 0 = quads (pen-space, needs holders),
// 1 = regions (engine-drawn, 4.3.4 model), 2 = both (bring-up A/B overlay).
int __fastcall Script_InlineTexMode(void *L) {
    if (Game::Lua::IsNumber(L, 1)) {
        const int m = static_cast<int>(Game::Lua::ToNumber(L, 1));
        if (m >= 0 && m <= 2) {
            if (m == 0 && g_renderMode != 0)
                Text::InlineTexturePool::HideAll(); // no flush walk will clear them
            g_renderMode = m;
        }
    }
    Game::Lua::PushNumber(L, static_cast<double>(g_renderMode));
    return 1;
}

// _classicapi_InlineTexRegionCal([dx][, dy]) -> dx, dy. Pixel (pen-unit) nudge
// applied to region-mode icons only; use in mode 2 to align the region copy
// onto the quad copy (the pixel-perfect reference).
int __fastcall Script_InlineTexRegionCal(void *L) {
    g_regionCalX = static_cast<float>(Arg(L, 1, g_regionCalX));
    g_regionCalY = static_cast<float>(Arg(L, 2, g_regionCalY));
    Game::Lua::PushNumber(L, g_regionCalX);
    Game::Lua::PushNumber(L, g_regionCalY);
    return 2;
}

// _classicapi_InlineTexWrap([n]) -> wrapWidth, fontH, fontHPx, len, passes,
// finalSumPx, shrunkWidth, floored, passSummary. n = 0 (default) is the most
// recent icon-bearing wrap-stepper call, 1 the one before, … up to 7.
// wrapWidth/fontH/shrunk are in the CALLER's unit space; fontHPx and the
// sums are pixels. passSummary is one string ("p1 s=… sum=…px len=… brk=…
// nxt=… w=… | p2 …") with every probe's shrink candidate, icon sum, line
// length, raw outBreak/outNext−text, and raw outWidth — compacted so /dump's
// ~30-value print cap never truncates the record.
int __fastcall Script_InlineTexWrap(void *L) {
    uint32_t n = 0;
    if (Game::Lua::IsNumber(L, 1))
        n = static_cast<uint32_t>(Game::Lua::ToNumber(L, 1));
    if (n >= kWrapDiagCount || n >= g_wrapDiagNext)
        return 0;
    const WrapDiag &d =
        g_wrapDiag[(g_wrapDiagNext - 1 - n) % kWrapDiagCount];
    if (d.passes == 0)
        return 0;
    Game::Lua::PushNumber(L, d.wrapWidth);
    Game::Lua::PushNumber(L, d.fontH);
    Game::Lua::PushNumber(L, d.fontHPx);
    Game::Lua::PushNumber(L, static_cast<double>(d.len));
    Game::Lua::PushNumber(L, static_cast<double>(d.passes));
    Game::Lua::PushNumber(L, d.finalSumPx);
    Game::Lua::PushNumber(L, d.shrunk);
    Game::Lua::PushNumber(L, static_cast<double>(d.floored));
    // All passes compacted into ONE string so a /dump never truncates (its
    // print caps around 30 values).
    char buf[512];
    int off = 0;
    for (int i = 0; i < d.passes && off < static_cast<int>(sizeof(buf)) - 1; ++i) {
        off += snprintf(buf + off, sizeof(buf) - static_cast<size_t>(off),
                        "%sp%d s=%.4f sum=%.1fpx len=%d brk=%d nxt=%d w=%.4f",
                        (i != 0) ? " | " : "", i + 1, d.probeS[i], d.sumPx[i], d.lineLen[i],
                        d.pBreakA[i], d.pNextD[i], d.pWidthA[i]);
        if (off < 0)
            break;
    }
    Game::Lua::PushString(L, buf);
    return 9;
}

// _classicapi_InlineTexProbeFS("FontStringName") ->
//   s, rect[0..3] (raw +0x64..+0x70: {yA, left, yB, right}), insetX, insetY,
//   originX, originY, nodeFontH
// Verification probe for the pen↔anchor scale bridge (RebuildString 0x7724A0,
// fs+0x7C): expected relations are origin ≈ (rectCorner + inset×s)/s (or ×s,
// depending on which side of the bridge the node stores) and nodeFontH ≈
// fontPx×s. Node fields are nil when the fontstring has no built text block.
int __fastcall Script_InlineTexProbeFS(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return 0;
    void *fs = nullptr;
    {
        const int top = Game::Lua::GetTop(L);
        Game::Lua::PushString(L, Game::Lua::ToString(L, 1));
        Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
        fs = Game::Lua::ResolveObject(L, -1);
        Game::Lua::SetTop(L, top);
    }
    if (!LooksReadable(fs))
        return 0;
    auto *f = reinterpret_cast<uint8_t *>(fs);
    Game::Lua::PushNumber(L, *reinterpret_cast<float *>(f + Offsets::OFF_LAYOUT_SCALE));
    for (int i = 0; i < 4; ++i)
        Game::Lua::PushNumber(L,
                              *reinterpret_cast<float *>(f + Offsets::OFF_REGION_RECT + i * 4));
    Game::Lua::PushNumber(L, *reinterpret_cast<float *>(f + Offsets::OFF_FONTSTRING_INSET_X));
    Game::Lua::PushNumber(L, *reinterpret_cast<float *>(f + Offsets::OFF_FONTSTRING_INSET_Y));

    void *node = nullptr;
    void *block = *reinterpret_cast<void **>(f + Offsets::OFF_FONTSTRING_TEXT_BLOCK);
    if (LooksReadable(block))
        node = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(block) +
                                          Offsets::OFF_TEXTBLOCK_NODE);
    if (LooksReadable(node)) {
        auto *n = reinterpret_cast<uint8_t *>(node);
        Game::Lua::PushNumber(L, *reinterpret_cast<float *>(n + Offsets::OFF_TEXT_NODE_ORIGIN_X));
        Game::Lua::PushNumber(L, *reinterpret_cast<float *>(n + Offsets::OFF_TEXT_NODE_ORIGIN_Y));
        Game::Lua::PushNumber(L,
                              *reinterpret_cast<float *>(n + Offsets::OFF_TEXT_NODE_FONT_SIZE));
    } else {
        Game::Lua::PushNil(L);
        Game::Lua::PushNil(L);
        Game::Lua::PushNil(L);
    }
    // The engine's Script_SetPoint px→internal conversion globals, to correlate
    // against the measured K (if K == div×1024/mul, pen units are SetPoint px).
    Game::Lua::PushNumber(L, *reinterpret_cast<float *>(Offsets::VAR_UI_COORD_SCALE_MUL));
    Game::Lua::PushNumber(L, *reinterpret_cast<float *>(Offsets::VAR_UI_COORD_SCALE_DIV));
    // Live K cache (0 until an icon node has derived it).
    Game::Lua::PushNumber(L, g_penPerAnchor);
    return 13;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexEnable", &Script_InlineTexEnable);
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexSuppress", &Script_InlineTexSuppress);
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexTune", &Script_InlineTexTune);
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexStats", &Script_InlineTexStats);
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexProbeFS", &Script_InlineTexProbeFS);
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexMode", &Script_InlineTexMode);
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexRegionCal", &Script_InlineTexRegionCal);
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexWrap", &Script_InlineTexWrap);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

int DebugChainState(void *fs) {
    if (!LooksReadable(fs))
        return 0;
    void *block = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(fs) +
                                             Offsets::OFF_FONTSTRING_TEXT_BLOCK);
    if (block == nullptr) {
        // Blockless: rebuild pending (dirty bit set → engine will rebuild) vs
        // STUCK (bit clear → nothing will ever rebuild; the flush nudges these).
        const uint8_t flags = *(reinterpret_cast<uint8_t *>(fs) +
                                Offsets::OFF_FONTSTRING_DIRTY_FLAGS);
        return (flags & 1u) ? 5 : 6;
    }
    if (!LooksReadable(block))
        return 0;
    void *node = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(block) +
                                            Offsets::OFF_TEXTBLOCK_NODE);
    if (!LooksReadable(node))
        return 0;
    auto ow = g_nodeOwner.find(node);
    if (ow == g_nodeOwner.end())
        return 1;
    if (ow->second != fs)
        return 2;
    auto it = g_nodeIcons.find(node);
    if (it == g_nodeIcons.end() || it->second.icons.empty())
        return 3;
    return 4;
}

int DebugNodeSeenAge(void *fs) {
    if (!LooksReadable(fs))
        return -1;
    void *block = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(fs) +
                                             Offsets::OFF_FONTSTRING_TEXT_BLOCK);
    if (!LooksReadable(block))
        return -1;
    void *node = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(block) +
                                            Offsets::OFF_TEXTBLOCK_NODE);
    if (!LooksReadable(node))
        return -1;
    auto it = g_nodeSeen.find(node);
    if (it == g_nodeSeen.end())
        return -1;
    return static_cast<int>(g_flushSeq - it->second);
}

namespace {
// fs → current node (fs+0xF8 → handle+8), nullptr when the chain is unreadable.
void *DebugCurrentNode(void *fs) {
    if (!LooksReadable(fs))
        return nullptr;
    void *block = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(fs) +
                                             Offsets::OFF_FONTSTRING_TEXT_BLOCK);
    if (!LooksReadable(block))
        return nullptr;
    void *node = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(block) +
                                            Offsets::OFF_TEXTBLOCK_NODE);
    return LooksReadable(node) ? node : nullptr;
}
} // namespace

int DebugNodeBuiltKnown(void *fs) {
    void *node = DebugCurrentNode(fs);
    if (node == nullptr)
        return -1;
    return g_nodeBuilt.find(node) != g_nodeBuilt.end() ? 1 : 0;
}

int DebugNodeEmitOutcome(void *fs) {
    void *node = DebugCurrentNode(fs);
    if (node == nullptr)
        return -1;
    auto it = g_nodeEmit.find(node);
    return it == g_nodeEmit.end() ? -1 : static_cast<int>(it->second);
}

unsigned int DebugNodeFlags(void *fs) {
    if (!LooksReadable(fs))
        return 0xFFFFFFFFu;
    void *block = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(fs) +
                                             Offsets::OFF_FONTSTRING_TEXT_BLOCK);
    if (!LooksReadable(block))
        return 0xFFFFFFFFu;
    void *node = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(block) +
                                            Offsets::OFF_TEXTBLOCK_NODE);
    if (!LooksReadable(node))
        return 0xFFFFFFFFu;
    return *reinterpret_cast<const uint32_t *>(reinterpret_cast<const uint8_t *>(node) +
                                               Offsets::OFF_TEXT_NODE_FLAGS);
}

void PrepareForReload() {
    // /reload frees every gxu text node — g_nodeIcons/g_nodeOwner hold stale
    // node pointers. Forget them (records rebuild as the reloaded UI re-emits
    // its text). The texture-handle cache stays valid (engine keeps its by-name
    // cache); K survives (resolution/uiScale don't change across /reload).
    g_nodeIcons.clear();
    g_nodeOwner.clear();
    g_nodeSeen.clear();
    g_nodeBuilt.clear();
    g_nodeEmit.clear();
}

} // namespace Text::InlineTexture
