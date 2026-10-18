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

// Inline texture escape (`|Tpath:height:width:...|t`) backport, plus its atlas
// sibling (`|A:atlasName:height:width[:offsetX:offsetY]|a`).
//
// Vanilla 1.12 has ZERO inline-texture support: the shared `|`-tokenizer
// (FUN_005c2810) has no `T`/`t` case, so `|T...|t` renders as literal text.
// This module teaches the text engine to render the icon inline.
//
// The two markers share EVERYTHING except the payload parser. An atlas is a
// texture path plus a normalized sub-rect, which is exactly what IconDesc already
// carries for a `|T` span with texcoords, so `ParseAtlasIcon` resolves the name
// through Texture::Atlas and fills the same struct — every scanner, measure, wrap,
// record and placement stage below is marker-agnostic from that point on. Adding
// a THIRD marker means touching IconStartLen / FindIconClose / InlineSpanLen, the
// ghost guard in FlushLayout, and — mandatory — the chat anti-spoof in
// chat/IconFilter.cpp, which is what stops a player rendering art by typing it.
//
// Two working pieces:
//   1. POSITIONING — co-hook the per-line glyph emitter (FUN_005ccbe0) and, for
//      a line containing `|T…|t`, render the plain text runs by DELEGATING to the
//      original emitter per segment (threading the pen through linkState[4]) while
//      recording an icon at the pen between runs. This avoids reimplementing the
//      emitter's intricate vertex math: the engine still lays out every glyph; we
//      only track where each icon goes.
//   2. RENDERING — the 4.3.4 CSimpleEmbeddedTexture model, ported faithfully
//      (the ONLY render path; a raw-GxU-quad mode existed during bring-up and
//      was removed). A co-hook on CSimpleFontString::RebuildString
//      (FUN_007724A0) maps each fresh text node to its OWNING FONTSTRING (1.12
//      chat lines are real CSimpleFontStrings — the ScrollingMessageFrame's
//      display refresh FUN_00788750 SetTexts/anchors/shows one per visible
//      line). The paint-tail flush walks the icon records and queues each to
//      `Text::InlineTexturePool`, which configures a pooled engine-managed
//      CSimpleTexture ANCHORED TO THE OWNING FONTSTRING at the pen offset —
//      exactly how 4.3.4's UpdateEmbeddedTextures anchors its embedded
//      textures. The engine draws the region every frame (residency) and the
//      anchor system moves it with its line on every scroll/shift (zero
//      per-frame work, no render transforms, no mid-render mutation). See
//      docs/InlineTextureResidency.md.
//
// Supports the full positional payload
// `|Tpath:height:width:offsetX:offsetY:texW:texH:left:right:top:bottom:r:g:b|t`
// (size, pen offset, sprite-sheet texcoord crop, and r:g:b vertex tint). Size,
// spacing and vertical alignment follow 4.3.4's rules exactly — see the "icon
// geometry" block and docs/InlineTextureEscapes.md "Icon geometry". MEASURE
// is corrected at the fs-level chokes: the tokenizer still reports an icon as
// ~zero to every measure/wrap loop (the token contract has no size fields), but
// a co-hook on the internal string-width getter (FUN_FONTSTRING_STRING_WIDTH)
// re-adds the same per-icon advances the emitter reserves — so GetStringWidth,
// the tooltip auto-size, and auto-width layout match the rendered width — a
// co-hook on the internal string-height getter (FUN_FONTSTRING_STRING_HEIGHT)
// adds each wrapped line's icon overflow past the font height, summed — while
// the emitter moves those lines down and the origin finalize justifies the
// taller block, 4.3.4's layout (chat stacking + auto-height rects) — and a
// co-hook on the shared wrap-stepper dispatcher (FUN_TEXT_WRAP_STEPPER)
// shrinks the wrap width by the line's icon advances so wrapped lines break at
// the visible edge instead of overflowing. Still icon-blind (documented,
// accepted): the substring measure (FUN_00772AE0) and hyperlink hit-testing.
// See docs/InlineTextureEscapes.md for the RE map.

#include "text/InlineTexture.h"

#include "Game.h"
#include "Offsets.h"
#include "text/InlineTexturePool.h"
#include "text/PtrProbe.h"
#include "texture/Atlas.h"
#include "texture/Transform.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace Text::InlineTexture {

namespace {

// LAA-aware pointer sanity probe: Text::LooksReadable (text/PtrProbe.h).

// --- inline-texture descriptor parse ---------------------------------------

// Parsed
// `|Tpath:height:width:offsetX:offsetY:texW:texH:left:right:top:bottom:r:g:b|t`
// payload. Trailing fields are optional; texcoords crop a sprite sheet (e.g. the
// raid-target icons) to one cell, and r:g:b (0-255) tint the icon.
// Sizes stay as WRITTEN here; ResolveIconSize turns them into pixels against
// the line's font, which is only known at the emitter/measure site.
struct IconDesc {
    std::string path;
    float height = 0.0f;     // <= 0 → the line's font height
    float width = 0.0f;      // see ResolveIconSize for the three cases
    bool widthGiven = false; // the width field was present (even as 0)
    float offsetX = 0.0f;    // screen pixels, right; never scaled
    float offsetY = 0.0f;    // screen pixels, up; never scaled
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
    out.height = f[0];
    out.widthGiven = (nf >= 2);
    out.width = out.widthGiven ? f[1] : 0.0f;
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

// Parses the payload between `|A` and `|a` — `atlasName:height:width[:offsetX:offsetY]`
// — into the SAME IconDesc a `|T` span produces, so every stage after this point
// (measure, wrap, record, placement, texcoord) is shared between the two markers.
// An atlas is exactly "a path plus a normalized sub-rect", which is what IconDesc
// already carries. Returns false when the name is unbound, and the span then
// renders as ordinary text.
bool ParseAtlasIcon(const char *payload, size_t len, IconDesc &out) {
    const char *end = payload + len;
    // The marker is `|A` and the retail payload is `:name:h:w…`, so the name is
    // the first field AFTER a leading colon — unlike `|T`, whose path starts
    // immediately. Consuming that colon here is what makes the two forms differ.
    const char *nameStart = payload;
    if (nameStart < end && *nameStart == ':')
        ++nameStart;
    const char *colon = nameStart;
    while (colon < end && *colon != ':')
        ++colon;
    if (colon == nameStart)
        return false; // no name
    const std::string name(nameStart, static_cast<size_t>(colon - nameStart));

    const Texture::Atlas::Info *info = Texture::Atlas::Find(name.c_str());
    if (info == nullptr) {
        Texture::Atlas::RecordMiss(name.c_str());
        return false;
    }
    out.path = info->file;
    out.u0 = info->left;
    out.u1 = info->right;
    out.v0 = info->top;
    out.v1 = info->bottom;

    float f[4] = {0};
    int nf = 0;
    const char *p = (colon < end) ? colon + 1 : end;
    while (p < end && nf < 4) {
        const char *nx = p;
        f[nf++] = ParseField(p, end, &nx);
        p = nx;
    }
    // An omitted or 0 height means "the atlas's own pixel size" — resolved HERE,
    // not deferred to the line font height the `|T` path falls back to. The
    // result is an explicit size, so ResolveIconSize scales it exactly like a
    // written `|T` size.
    const float nativeH = info->height;
    const float nativeW = info->width;
    out.height = (nf >= 1 && f[0] > 0.0f) ? f[0] : nativeH;
    if (out.height <= 0.0f)
        return false; // no usable size anywhere → nothing to draw
    out.widthGiven = true;
    if (nf >= 2 && f[1] > 0.0f)
        out.width = f[1];
    else if (nativeH > 0.0f && nativeW > 0.0f)
        out.width = out.height * (nativeW / nativeH); // keep the atlas's aspect
    else
        out.width = out.height;
    out.offsetX = (nf >= 3) ? f[2] : 0.0f;
    out.offsetY = (nf >= 4) ? f[3] : 0.0f;
    return true;
}

// Parses a span's payload with the parser its marker kind calls for.
bool ParseIconSpan(const char *payload, size_t len, char kind, IconDesc &out) {
    return (kind == 'A') ? ParseAtlasIcon(payload, len, out) : ParseIcon(payload, len, out);
}

// --- per-node recorded icons -----------------------------------------------

// One icon to draw, in the render node's node-local coordinate space (the same
// space glyph verts live in — the paint pass translates it by the node origin).
// Y is UP, as in the engine's own text layout (the pen descends per line).
struct IconRecord {
    std::string path;       // texture path
    float left;             // node-local x of the icon's left edge
    float bottom;           // node-local y of the icon's bottom edge
    float w;
    float h;
    float offsetX, offsetY; // screen-pixel shift (right, up)
    float u0, v0, u1, v1;   // texture crop
    uint32_t color;         // vertex tint (0xAARRGGBB; white = untinted)
};

// Icons keyed by render node. Stale-record safety is structural, not
// versioned: the node-free co-hook (below) erases a node's records the moment
// its address can be reused, and the emitter's firstLine erase resets them on
// every rebuild. (An explicit per-build version check was tried and REJECTED —
// the builder runs every paint but only emits when dirty, so a version
// mismatch on a clean paint erased live records: the all-icons-gone
// regression.)
std::unordered_map<void *, std::vector<IconRecord>> g_nodeIcons;

// node → owning CSimpleFontString, recorded by the RebuildString co-hook (the
// 4.3.4 ownership model — 1.12 chat lines ARE fontstrings). Consulted by the
// flush to anchor icon regions to their line. Stale fs pointers are guarded by
// LooksReadable at use (chat/bubble fontstrings are pooled and long-lived);
// the map clears on /reload.
std::unordered_map<void *, void *> g_nodeOwner;

// node → the node's base colour (node+0x2c) at the time its segmented
// per-glyph colours were baked. A segmented (bit-3, icon-bearing) node never
// re-bakes on a colour change — the engine's colour setter skips the
// invalidate for bit-3 nodes because UNIFORM bit-3 text recolours live at
// paint — so a later SetTextColor RGB change (the glue AddonList's gold↔gray
// toggle) froze the glyphs at their bake-time colour while plain titles
// swapped fine. The flush compares this stamp against the live node colour
// and re-bakes on an RGB change via the engine's own invalidate; alpha-only
// changes take the cheap in-place alpha mirror instead (fades animate per
// frame — a re-bake per frame would be absurd, and alpha is safe to patch in
// place because every baked glyph carries the node alpha).
std::unordered_map<void *, uint32_t> g_nodeBakedColor;

// Draw-builder co-hook: exact build boundaries for first-line detection. The
// builder runs once per node build and calls the emitter per wrapped line;
// stamping the node + zeroing the emit counter here lets the emitter know
// "this is the first line of a fresh build" with certainty. (The previous
// heuristic — comparing the emit text pointer against node+text — failed on
// pfUI-processed lines and caused both the erased-live-records bug and the
// ghost-icons-on-reused-nodes bug.)
// Runtime toggle (default ON — the feature is proven; `_classicapi_InlineTexEnable(false)`
// still turns it off, and the SEH latch trips it on a flush OR rotation fault).
// When off, the emitter/tokenizer co-hooks fast-path straight to the originals
// and the fontstring-rotation apply is skipped.
bool g_inlineEnabled = true;

void *g_buildNode = nullptr;
uint32_t g_buildEmitSeq = 0;

// The CSimpleFontString that currently owns `node` (node is its live text node:
// fs+0xF8 → block, block+8 == node), or nullptr. Read-only — the validation
// variant with side effects (re-dirtying a stuck-blockless fs) lives inline in
// FlushLayout; this one just answers "whose verts are these?" for the rotation
// hook.
void *OwningFontStringOf(void *node) {
    auto ow = g_nodeOwner.find(node);
    if (ow == g_nodeOwner.end() || !LooksReadable(ow->second))
        return nullptr;
    void *block = Game::Read<void *>(ow->second, Offsets::OFF_FONTSTRING_TEXT_BLOCK);
    if (!LooksReadable(block))
        return nullptr;
    void *cur = Game::Read<void *>(block, Offsets::OFF_TEXTBLOCK_NODE);
    return (cur == node) ? ow->second : nullptr;
}

// A fontstring's layout scale (fs+0x7C), 1 when unset. Feeds 3.3.5's extra
// size factor (see ResolveIconSize).
float FsLayoutScale(void *fs) {
    const float scale = Game::Read<float>(fs, Offsets::OFF_LAYOUT_SCALE);
    return (scale > 0.0f) ? scale : 1.0f;
}

// The owning fontstring's layout scale, or 1 when the node's owner isn't known.
float NodeLayoutScale(void *node) {
    void *fs = OwningFontStringOf(node);
    return (fs != nullptr) ? FsLayoutScale(fs) : 1.0f;
}

// The fontstring whose fs-level layout is running. The fs-level wrap callers —
// the height getter, the display-text truncation (run by the rebuild) and the
// break array — hand gxu a font handle, never the fontstring, so the hooks on
// those three set this around the original for the wrap stepper to read.
// 4.3.4 gets the same value to every wrap the same way: each caller passes
// m_layoutScale as CalcWrapPoint's `scale`.
void *g_scopeFs = nullptr;

struct FsScope {
    void *prev;
    explicit FsScope(void *fs) : prev(g_scopeFs) { g_scopeFs = fs; }
    ~FsScope() { g_scopeFs = prev; }
    FsScope(const FsScope &) = delete;
    FsScope &operator=(const FsScope &) = delete;
};

// The layout scale for a wrap-stepper call: the node being built when the draw
// builder is the caller, else the fontstring of the running fs-level layout.
// Every line break therefore sizes icons exactly as the render does.
float WrapLayoutScale() {
    if (g_buildNode != nullptr) {
        if (void *fs = OwningFontStringOf(g_buildNode))
            return FsLayoutScale(fs);
    }
    return (g_scopeFs != nullptr) ? FsLayoutScale(g_scopeFs) : 1.0f;
}

// Per node: the summed overflow of its tall-icon lines this build, in pen px —
// 4.3.4's CGxString::m_extraLineHeight. The emitter adds each line's overflow
// as it moves the lines down; the origin finalize adds the total to the text
// height it justifies. Reset at each real build, erased with the node.
std::unordered_map<void *, float> g_nodeExtra;

// Set by the string-height co-hook around the engine's wrapped-height pass:
// the wrap stepper adds each wrapped line's icon overflow here, in font
// heights — GxuFontGetWrappedTextHeight's extraHeight sum.
float *g_heightExtra = nullptr;

using DrawBuilder_t = void(__fastcall *)(void *node);
DrawBuilder_t g_builderOriginal = nullptr;

void __fastcall DrawBuilder_h(void *node) {
    // The builder lays the node out only when its built-line count is 0 (else
    // it returns at once); only then does its extra height start over.
    if (node != nullptr && Game::Read<int>(node, Offsets::OFF_TEXT_NODE_BUILT_LINES) == 0)
        g_nodeExtra.erase(node);
    g_buildNode = node;
    g_buildEmitSeq = 0;
    g_builderOriginal(node);
    g_buildNode = nullptr;
    // A fresh vertex bake happened iff the emitter emitted ≥1 line this build
    // (clean paints re-run the builder but re-emit nothing). Only then are the
    // node's glyph verts axis-aligned — the sole point it is safe to apply a
    // fontstring rotation, since rotating already-rotated verts on a clean paint
    // would make the text spin. Guarded: a bad vert read disables the inline
    // feature (SEH latch) rather than faulting the client.
    if (g_inlineEnabled && g_buildEmitSeq > 0) {
        void *fs = OwningFontStringOf(node);
        if (fs != nullptr) {
            __try {
                Texture::Transform::RotateFontStringNode(node, fs);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                g_inlineEnabled = false;
            }
        }
    }
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
        g_nodeBakedColor.erase(node);
        g_nodeExtra.erase(node);
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
    {
        // The rebuild runs the display-text truncation, an fs-level wrap.
        FsScope scope(fs);
        g_rebuildOriginal(fs);
    }
    if (!LooksReadable(fs))
        return;
    void *block = Game::Read<void *>(fs, Offsets::OFF_FONTSTRING_TEXT_BLOCK);
    if (!LooksReadable(block))
        return;
    void *node = Game::Read<void *>(block, Offsets::OFF_TEXTBLOCK_NODE);
    if (LooksReadable(node))
        g_nodeOwner[node] = fs;
}

static const Game::HookAutoRegister _rebuildHook{Offsets::FUN_FONTSTRING_REBUILD_STRING,
                                                 reinterpret_cast<void *>(&RebuildString_h),
                                                 reinterpret_cast<void **>(&g_rebuildOriginal)};

// Origin-finalize co-hook: tall-icon lines count in the vertical justify.
// FUN_TEXT_NODE_FINALIZE_ORIGIN places the text block in its rect by a text
// height of lines × fontH + spacing — 4.3.4's InitializeViewTranslation, less
// the m_extraLineHeight term 4.3.4 adds. Middle justify centres by half the
// text height and bottom justify adds all of it, both on top of the node's
// position y, so raising that position by half / all of the node's extra
// height for the call gives 4.3.4's result through the engine's own math and
// rounding. Top justify ignores the text height, as in 4.3.4. Runs after every
// build and on every reposition (FUN_005ccb10), so it reads the node's stored
// extra rather than fixing the origin once.
using FinalizeOrigin_t = void(__fastcall *)(void *node);
FinalizeOrigin_t g_finalizeOriginOriginal = nullptr;

void __fastcall FinalizeOrigin_h(void *node) {
    auto it = g_nodeExtra.find(node);
    if (it == g_nodeExtra.end() || !(it->second > 0.0f)) {
        g_finalizeOriginOriginal(node);
        return;
    }
    const int lines = Game::Read<int>(node, Offsets::OFF_TEXT_NODE_BUILT_LINES);
    const uint32_t flags = Game::Read<uint32_t>(node, Offsets::OFF_TEXT_NODE_FLAGS);
    const int justifyV = Game::Read<int>(node, Offsets::OFF_TEXT_NODE_JUSTIFY_V);
    const int rasterY = Game::Read<int>(Offsets::VAR_TEXT_RASTER_Y);
    // The same gates the original takes before it justifies.
    if (lines == 0 || (flags & 0x80u) != 0 || rasterY <= 0 || (justifyV != 1 && justifyV != 2)) {
        g_finalizeOriginOriginal(node);
        return;
    }
    float &posY = Game::Ref<float>(node, Offsets::OFF_TEXT_NODE_POSITION_Y);
    const float saved = posY;
    const float extra = it->second / static_cast<float>(rasterY); // pen px → node units
    posY = saved + ((justifyV == 1) ? extra * 0.5f : extra);
    g_finalizeOriginOriginal(node);
    posY = saved;
}

static const Game::HookAutoRegister _finalizeOriginHook{
    Offsets::FUN_TEXT_NODE_FINALIZE_ORIGIN, reinterpret_cast<void *>(&FinalizeOrigin_h),
    reinterpret_cast<void **>(&g_finalizeOriginOriginal)};

// Pen-units-per-anchor-unit scale (K), per axis — the ENGINE'S OWN conversion,
// read live from the same four globals the text pipeline uses. Verified by
// decompiling the whole chain this session:
//   FUN_0041ade0: block/node positions are stored NORMALIZED — fs-local anchor
//     x ÷ [VAR_COORD_DDC_WIDTH_SCALE] (= screen width in anchor units), y ÷
//     [VAR_COORD_DDC_HEIGHT_SCALE];
//   FUN_005cdf70 (origin finalize): node origin (+0x70/+0x74) =
//     round(normalized × [VAR_TEXT_RASTER_X/Y]) — render-target PIXELS, with
//     the justify shift and vertical align folded in.
// So K = raster ÷ anchorExtent per axis. Cross-checked against a live probe
// on a right-justified fs: rasterX/anchorW = 3025 matched the measured
// origin÷rect-right quotient to four digits.
//
// History: two earlier forms both failed. The empirical derivation
// (K = nodeOriginX / rectJustifyRefX) couldn't self-derive for chat (rect
// edge near x = 0 fails the divide-guard) and rode a global cache seeded by
// whichever fs derived first — right at the layout it was calibrated on,
// wrong elsewhere. A closed form from the SetPoint px factor
// ((div·1024/mul) × fsScale) divided by the wrong global pair and multiplied
// by a scale that doesn't belong (node positions are fed fs-LOCAL anchor
// units, so the fs chain never enters). Returns 0 while the globals aren't
// live yet (boot) — the caller's K > 1 gates treat that as "not ready".
struct PenScale {
    float x = 0.0f, y = 0.0f;
};
PenScale PenPerAnchor() {
    const float aw = Game::Read<float>(Offsets::VAR_COORD_DDC_WIDTH_SCALE);
    const float ah = Game::Read<float>(Offsets::VAR_COORD_DDC_HEIGHT_SCALE);
    const int rx = Game::Read<int>(Offsets::VAR_TEXT_RASTER_X);
    const int ry = Game::Read<int>(Offsets::VAR_TEXT_RASTER_Y);
    PenScale k;
    if (aw > 0.0f && rx > 0)
        k.x = static_cast<float>(rx) / aw;
    if (ah > 0.0f && ry > 0)
        k.y = static_cast<float>(ry) / ah;
    return k;
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
    const void *fe = Game::Read<const void *>(Offsets::VAR_FOCUSED_EDITBOX);
    if (!LooksReadable(fe))
        return nullptr;
    const uint8_t sel = Game::Read<uint8_t>(fe, Offsets::OFF_EDITBOX_BUFFER_SELECT);
    const uint8_t *inBuf = Game::Read<const uint8_t *>(
        fe, (sel & 8u) ? Offsets::OFF_EDITBOX_BUFFER_MASKED : Offsets::OFF_EDITBOX_BUFFER);
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
// is the caller's editbox-PROXY bit at its own representation level — gxu flags
// bit 0x40 at the tokenizer, fs+0x120 bit 0x1000 at the string-width co-hook
// (FUN_0044D670 translates the latter into the former, so both call shapes
// evaluate the same engine bit). That bit is actually NON-SPACE-WRAP (what
// FontString:SetNonSpaceWrap toggles), used here as an editbox proxy — see
// NodeEditable. The tokenizer's `text[0] == '|'` position check stays at its call
// site — it's about where the tokenizer stands in the text, not about whether the
// feature is active.
bool InlineInterceptActive(const uint8_t *text, bool editable) {
    return g_inlineEnabled && !editable && text != nullptr && !TextInReentry(text) &&
           !TextInFocusedEditbox(text);
}

// A text node's flags (`[node+0x5c]`) bit 6 (0x40) is the NON-SPACE-WRAP bit (the
// one FontString:SetNonSpaceWrap sets; fs+0x120 bit 0x1000 → this via
// FUN_0044D420). We use it as an editbox PROXY: multi-line editors enable
// non-space-wrap (macro editbox node flags 0x4D, bit 6 set) while chat/display
// text does not (chat 0x20D, FontStrings 0x0D — bit 6 clear), so suppressing
// inline rendering when it is set covers editboxes the focused-buffer pointer test
// misses (multi-line editors build once, un-focused). Caveat: the correlation is
// not a guarantee — a DISPLAY fontstring with SetNonSpaceWrap(true) + inline |T
// icons would be wrongly suppressed here (rare, never observed; the focused-buffer
// pointer test remains the primary editbox guard). Mirrors 4.3.4's per-render
// texture-disable flag, adapted to 1.12's layout.
inline bool NodeEditable(const void *node) {
    return (Game::Read<uint32_t>(node, Offsets::OFF_TEXT_NODE_FLAGS) & 0x40u) != 0;
}

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
// `outKind` (optional) receives the marker letter — 'T' for a path span, 'A' for
// an atlas span (`|A:name:h:w|a`), whose name resolves through Texture::Atlas.
// Both markers share every scanner and every downstream stage; only the payload
// parser differs.
int IconStartLen(const uint8_t *text, int len, int i, char *outKind = nullptr) {
    char kind = 0;
    int mlen = 0;
    if (i + 2 < len && text[i] == '|' && text[i + 1] == '|' &&
        (text[i + 2] == 'T' || text[i + 2] == 'A')) {
        kind = static_cast<char>(text[i + 2]);
        mlen = 3;
    } else if (i + 1 < len && text[i] == '|' &&
               (text[i + 1] == 'T' || text[i + 1] == 'A')) {
        kind = static_cast<char>(text[i + 1]);
        mlen = 2;
    }
    if (mlen != 0 && outKind != nullptr)
        *outKind = kind;
    return mlen;
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
size_t FindIconClose(const uint8_t *text, int len, size_t from, bool doubled, int *closeLen,
                     char kind = 'T') {
    const size_t n = static_cast<size_t>(len);
    const uint8_t closer = (kind == 'A') ? 'a' : 't'; // the marker's own closer
    if (doubled) {
        for (size_t i = from; i + 2 < n; ++i)
            if (text[i] == '|' && text[i + 1] == '|' && text[i + 2] == closer) {
                *closeLen = 3;
                return i;
            }
    } else {
        for (size_t i = from; i + 1 < n; ++i)
            if (text[i] == '|' && text[i + 1] == closer) {
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
    if (text[0] == '|' && text[1] == '|' && (text[2] == 'T' || text[2] == 'A'))
        mlen = 3; // doubled ||T / ||A
    else if (text[0] == '|' && (text[1] == 'T' || text[1] == 'A'))
        mlen = 2; // clean |T / |A
    else
        return 0;
    const bool doubled = (mlen == 3);
    const uint8_t closer = (text[mlen - 1] == 'A') ? 'a' : 't';
    for (int i = mlen; i < 2048 && text[i] != '\0'; ++i) {
        if (doubled) {
            if (text[i] == '|' && text[i + 1] == '|' && text[i + 2] == closer)
                return i + 3;
        } else {
            if (text[i] == '|' && text[i + 1] == closer)
                return i + 2;
        }
    }
    return 0; // unterminated → let the engine treat it as ordinary text
}

// --- icon geometry: 4.3.4's ParseEmbeddedTexture + InitializeTextLine -------
//
// Every size and spacing rule below is the 4.3.4 client's (verified against
// the decompiled source in the 15595 tree and, for the size formula, against
// the apwow.exe call list of ParseEmbeddedTexture 0x00617BA0: it calls
// CoordinateGetAspectCompensation and NDCToDDCWidth). 3.3.5 has the same rules.

// ParseEmbeddedTexture's reference font: 14 UI units.
constexpr float kReferenceFontUnits = 14.0f;

// What a WRITTEN size multiplies by, in the same unit as `fontPx`:
//   pixelScale = windowPx / (aspectComp × 1024) × fontHeight / NDCToDDCWidth(14 / (aspectComp × 1024))
// which reduces to fontPx / (14 × ddcWidthScale). So a written size is
// relative to a 14-unit font and follows the text's size, the UI scale and
// the resolution — `:16` is not 16 pixels. 1.12 holds ddcWidthScale in the
// same Coordinate static as 4.3.4 (VAR_COORD_DDC_WIDTH_SCALE).
float WrittenSizeScale(float fontPx) {
    const float ddcW = Game::Read<float>(Offsets::VAR_COORD_DDC_WIDTH_SCALE);
    if (!(ddcW > 0.0f))
        return 0.0f;
    return fontPx / (kReferenceFontUnits * ddcW);
}

struct IconSize {
    float w = 0.0f;
    float h = 0.0f;
};

// ParseEmbeddedTexture's size cases, in the unit of `fontPx` — 3.3.5's
// version, which multiplies by the fontstring's layout scale `L`
// (CGxString::m_scale = CSimpleFontString::m_layoutScale, passed by
// TextBlockCreate) on top of a font height that already includes it. 4.3.4
// dropped that extra factor; 3.3.5's was kept on a side-by-side look in game
// (below a UI scale of 1 its coins sit closer to digit height). `L` = 1
// reproduces 4.3.4.
//   height > 0 → height × L × scale; else L × the font height.
//   width absent → the height.
//   width × L > 0 → that × scale, or, when the height came from the font,
//                   that × the height (a ratio: `:0:2` is twice as wide).
//   otherwise  → L × the font height (`:20:0` is font-wide, not 20 wide).
IconSize ResolveIconSize(const IconDesc &d, float fontPx, float layoutScale) {
    const float scale = WrittenSizeScale(fontPx);
    const float L = (layoutScale > 0.0f) ? layoutScale : 1.0f;
    IconSize s;
    const bool heightFromFont = !(d.height > 0.0f) || !(scale > 0.0f);
    s.h = heightFromFont ? L * fontPx : d.height * L * scale;
    const float width = d.width * L;
    if (!d.widthGiven)
        s.w = s.h;
    else if (width > 0.0f)
        s.w = heightFromFont ? width * s.h : width * scale;
    else
        s.w = L * fontPx;
    return s;
}

// The gap 4.3.4 leaves between a glyph and a following icon: the face's
// outline ink, 4 × scale for a thick outline and 2 × scale for a thin one
// (InitializeTextLine case CODE_TEXTURESTART), where scale is the face's
// raster scale (font pixel height ÷ native height). 1.12 stores the same two
// constants and the same face flags (face+0x180 bit 3 / bit 0) — its emitter
// grows the line height by them. No gap when no glyph precedes the icon.
float OutlinePadPen(const void *fontFace, float fontPx) {
    if (fontFace == nullptr)
        return 0.0f;
    const uint32_t flags = Game::Read<uint32_t>(fontFace, Offsets::OFF_FONTFACE_FLAGS);
    float ink = 0.0f;
    if ((flags & 8u) != 0)
        ink = Game::Read<float>(Offsets::FLOAT_OUTLINE_EXTRA_THICK);
    else if ((flags & 1u) != 0)
        ink = Game::Read<float>(Offsets::FLOAT_OUTLINE_EXTRA_THIN);
    if (ink == 0.0f)
        return 0.0f;
    using FontNativeHeight_t = int(__fastcall *)(const void *font);
    const int native =
        reinterpret_cast<FontNativeHeight_t>(Offsets::FUN_TEXT_FONT_NATIVE_HEIGHT)(fontFace);
    return (native > 0) ? ink * (fontPx / static_cast<float>(native)) : ink;
}

// Length of a non-drawing text code at text[i] (`|c` + 8 hex, `|r`, `|H…|h`,
// `|h`), or 0 when text[i] starts something that draws. A doubled `||` is a
// drawn pipe. Used to tell whether an icon follows a glyph.
int NonDrawingCodeLen(const uint8_t *text, int len, int i) {
    if (text[i] != '|' || i + 1 >= len)
        return 0;
    const uint8_t c = text[i + 1];
    if (c == 'c' || c == 'C')
        return (i + 10 <= len) ? 10 : 0;
    if (c == 'r' || c == 'R' || c == 'h')
        return 2;
    if (c == 'H') {
        for (int j = i + 2; j + 1 < len; ++j)
            if (text[j] == '|' && text[j + 1] == 'h')
                return j + 2 - i;
    }
    return 0;
}

// Calls `fn(desc, precededByGlyph)` for every well-formed `|T…|t` / `|A…|a`
// span (or its sanitizer-doubled form) in [text, text+len). precededByGlyph is
// whether anything drawn sits between the span and the previous icon or the
// text start — 4.3.4 adds the outline gap only then. Malformed and
// unterminated spans are skipped, mirroring the emitter, which renders them as
// plain text.
template <typename Fn>
void ForEachIconSpan(const uint8_t *text, int len, Fn fn) {
    bool glyphSince = false;
    int i = 0;
    while (i < len) {
        char kind = 'T';
        const int ml = IconStartLen(text, len, i, &kind);
        if (ml == 0) {
            const int code = NonDrawingCodeLen(text, len, i);
            if (code > 0) {
                i += code;
                continue;
            }
            glyphSince = true;
            // An escaped pipe is one drawn pipe; skip it as a pair so its 2nd
            // `|` isn't re-read as a clean `|T`.
            if (text[i] == '|' && i + 1 < len && text[i + 1] == '|')
                i += 2;
            else
                ++i;
            continue;
        }
        int cl = 0;
        const size_t ce =
            FindIconClose(text, len, static_cast<size_t>(i) + ml, ml == 3, &cl, kind);
        if (ce == static_cast<size_t>(-1))
            return; // unterminated → the rest is plain text
        IconDesc d;
        if (ParseIconSpan(reinterpret_cast<const char *>(text) + i + ml,
                          ce - (static_cast<size_t>(i) + ml), kind, d)) {
            fn(d, glyphSince);
            glyphSince = false;
        }
        i = static_cast<int>(ce) + cl;
    }
}

// The width the icons in [text, text+len) add to a MEASURE: each icon's width,
// plus the outline gap when a glyph precedes it. The engine measures an icon
// as its width alone (4.3.4 InternalGetTextExtent / CalcWrapPoint) but lays
// it out after the outline gap; counting the gap keeps measure and render in
// step, which the tooltip and right-justified layouts depend on. offsetX is
// left out: 4.3.4 applies it to the drawn texture only, never to the layout.
float SumIconAdvances(const uint8_t *text, int len, float fontPx, float outlinePad,
                      float layoutScale) {
    float sum = 0.0f;
    ForEachIconSpan(text, len, [&](const IconDesc &d, bool afterGlyph) {
        sum += ResolveIconSize(d, fontPx, layoutScale).w + (afterGlyph ? outlinePad : 0.0f);
    });
    return sum;
}

// How far the tallest icon in [text, text+len) reaches past the font height, in
// the unit of `fontPx` — 0 when every icon fits. Called per WRAPPED LINE: it is
// 4.3.4's `lineHeightUsed − fontPixelHeight` (CalcWrapPoint's height out-param
// is the tallest icon that fits on the line), which InitializeText uses to move
// the line and everything after it down, and GxuFontGetWrappedTextHeight sums
// over the lines.
float IconOverflowPx(const uint8_t *text, int len, float fontPx, float layoutScale) {
    float maxH = 0.0f;
    ForEachIconSpan(text, len, [&](const IconDesc &d, bool) {
        const float h = ResolveIconSize(d, fontPx, layoutScale).h;
        if (h > maxH)
            maxH = h;
    });
    return (maxH > fontPx) ? (maxH - fontPx) : 0.0f;
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
    // (the non-space-wrap editbox proxy — see NodeEditable) clear, and the text is
    // NOT the focused editbox's own text.
    //
    // Bit 0x40 (non-space-wrap; set on the macro editor 0x4D) is our proxy for
    // "leave `|T` as literal" — it keeps the macro editor's measure literal so its
    // caret stays aligned with the raw glyphs the emitter draws for it. Chat
    // DISPLAY has it clear (flags 0x205, ICON-NODE probe), so this does NOT touch
    // it — its icons still measure ~zero. Single-line inputs (chat / name box)
    // lack bit 6 and are handled by the two focused-input signals:
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
// InlineInterceptActive predicate) and the fs isn't an editbox, add the
// icons' measured widths (the shared SumIconAdvances math).
//
// UNITS: the icon sum is computed in true PEN px — the numbers the emitter
// lays out (sizes and outline gap; see ResolveFsPenFont) — and bridged into the getter's return space via the font height known in
// both spaces: `sumPen × (fontHInt / fontHPen)`. Do NOT divide by fs+0x7C —
// that's the layout UI SCALE (~0.68-1.0), and the original's own
// `out / fs+0x7C` applies to a value FUN_0044D670 already ran through the
// gxu→internal converter, not to raw pixels (a /0x7C here inflated a 16px
// icon to +44k px on first flight). An earlier bridge used the SetPoint push
// factor (div×1024/mul) as the intermediate — the units cancelled the same
// way for `:0` icons, but it couldn't express the pen-integral truncation or
// the outline ink, leaving ≤1px + 2-4px per-icon divergences from the render.
//
// Idempotence: the original may serve the cached fs+0xFC — the icon sum is
// re-derived and re-added on EVERY call, and the cache is NEVER written.
//
// Residuals (documented in docs/InlineTextureEscapes.md): an icon after a
// glyph is laid out from that glyph's ink edge but measured from its advance,
// so the two differ by the glyph's right bearing (≤1px, the engine's own
// discrepancy too); the hyperlink hit-test past a tall icon OUTSIDE the
// link stays icon-blind. (Wrap breaks route through the stepper hook, and the
// substring measure has its own co-hook below.) The focused chat editbox's own
// display fontstring (editable bit CLEAR — only multi-line editors carry it)
// can reach this through the caret positioner's line-boundary branch with a
// content-suppressed raw render; that branch is multi-line-only in practice,
// so no content compare is spent here.
using StringWidthInternal_t = float(__fastcall *)(void *fs);
// FUN_FONTSTRING_FONT_HEIGHT is __thiscall(fs, mode-on-stack) — dummy-EDX
// __fastcall matches the register/stack layout (established pattern).
using FsFontHeight_t = float(__fastcall *)(void *fs, void *edx, int mode);
StringWidthInternal_t g_stringWidthOriginal = nullptr;

// The RENDER's exact font numbers for an fs, for the measure hooks. fontHInt
// is the fs height-getter's raw value — the same unit space the hooked
// internal getters RETURN (they divide their measure by fs+0x7C exactly as the
// rebuild multiplied it in), so `penValue × (fontHInt / fontHPen)` converts
// any pen-px quantity into the hook's return space with the font height as
// the exact unit bridge. fontHPen replicates the node's pen font height:
// nodeFontSize = internal × fs+0x7C ÷ [0x832A48] (the rebuild FUN_007724a0
// feeds internal × 0x7C to FUN_0044d420, which normalizes it via
// FUN_0041ae50), floored at [0x801628]/rasterY = 2 pen px (the node ctor,
// FUN_005cd6d0), realized by the emitter's own helper (flag 0 = the snap
// path: round(size × rasterY)).
// fs-built nodes are always in snap mode — none of the fs-flag → node-flag
// mappings in FUN_0044d420 produce bit 7 — so snap-truncated advances are
// exact here. The face for the outline ink resolves via
// [[fs+0xE0]+0x20] → node+0x44 (see OFF_FONTSTRING_FONT_HANDLE).
struct FsPenFont {
    float fontHInt = 0.0f; // getter units — the hook-return space
    float fontHPen = 0.0f; // render pen px (rounded + min-clamped like the node)
    float outlinePad = 0.0f; // OutlinePadPen of the fs's face, pen px
    float layoutScale = 1.0f; // fs+0x7C, for ResolveIconSize
};
bool ResolveFsPenFont(void *fs, FsPenFont &out) {
    out.fontHInt =
        reinterpret_cast<FsFontHeight_t>(Offsets::FUN_FONTSTRING_FONT_HEIGHT)(fs, nullptr, 1);
    const float scale = Game::Read<float>(fs, Offsets::OFF_LAYOUT_SCALE);
    const float anchorH = Game::Read<float>(Offsets::VAR_COORD_DDC_HEIGHT_SCALE);
    const int rasterY = Game::Read<int>(Offsets::VAR_TEXT_RASTER_Y);
    if (!(out.fontHInt > 0.0f) || !(scale > 0.0f) || !(anchorH > 0.0f) || rasterY <= 0)
        return false;
    // The ÷anchorH hop is LOAD-BEARING: the block creator FUN_0044d420 runs
    // the rebuild's fontH (internal × fs+0x7C) through the y-axis
    // anchor→normalized converter FUN_0041ae50 (÷[0x832A48]) before it
    // reaches node+0x18. Skipping it undersized fontHPen ~40%: phantom
    // tall-icon overflow grew every icon-bearing chat line, and the
    // fontHInt/fontHPen bridge inflated fixed-size/ink width terms (the
    // money-tooltip right-anchor gap).
    float sizeNorm = out.fontHInt * scale / anchorH;
    const float minNorm = Game::Read<float>(Offsets::FLOAT_OUTLINE_EXTRA_THIN) /
                          static_cast<float>(rasterY);
    if (sizeNorm < minNorm)
        sizeNorm = minNorm;
    out.fontHPen = reinterpret_cast<float(__fastcall *)(int, float)>(
        Offsets::FUN_TEXT_FONT_HEIGHT)(0, sizeNorm);
    if (!(out.fontHPen > 0.0f))
        return false;
    const void *handle = Game::Read<const void *>(fs, Offsets::OFF_FONTSTRING_FONT_HANDLE);
    const void *face =
        LooksReadable(handle) ? Game::Read<const void *>(handle, Offsets::OFF_FONT_HANDLE_FACE)
                              : nullptr;
    out.outlinePad = LooksReadable(face) ? OutlinePadPen(face, out.fontHPen) : 0.0f;
    out.layoutScale = scale;
    return true;
}

float __fastcall StringWidth_h(void *fs) {
    const float base = g_stringWidthOriginal(fs);
    if (!LooksReadable(fs))
        return base;
    // fs+0x120 bit 0x1000 (non-space-wrap; → gxu bit 0x40) as an editbox proxy —
    // see NodeEditable. A multi-line editor measures the raw markup it renders, so
    // its width must stay unadjusted (caret alignment).
    const bool editable =
        (Game::Read<uint32_t>(fs, Offsets::OFF_FONTSTRING_MEASURE_FLAGS) & 0x1000u) != 0;
    const uint8_t *text = Game::Read<const uint8_t *>(fs, Offsets::OFF_FONTSTRING_TEXT);
    if (!LooksReadable(text) || !InlineInterceptActive(text, editable))
        return base;
    int len = 0;
    while (len < 0x4000 && text[len] != '\0')
        ++len;
    if (!HasInlineTexture(text, len))
        return base;
    FsPenFont pf;
    if (!ResolveFsPenFont(fs, pf))
        return base;
    // The icons' widths in pen px, outline gap included, bridged into the
    // return space by the font height known in both spaces.
    const float sumPen = SumIconAdvances(text, len, pf.fontHPen, pf.outlinePad, pf.layoutScale);
    return base + sumPen * (pf.fontHInt / pf.fontHPen);
}

static const Game::HookAutoRegister _stringWidthHook{
    Offsets::FUN_FONTSTRING_STRING_WIDTH, reinterpret_cast<void *>(&StringWidth_h),
    reinterpret_cast<void **>(&g_stringWidthOriginal)};

// --- string-height co-hook (line growth for tall icons) ---------------------
//
// FUN_FONTSTRING_STRING_HEIGHT = GetStringHeightInternal (cache fs+0x100,
// lines×fontH + (lines−1)×spacing — icon-blind). Its three binary consumers
// (verified via xrefs):
//   • FUN_00772A60 — the anchor's effective-height vmethod: explicit height if
//     set, else THIS. Chat line fontstrings get only a SetWidth from the
//     ScrollingMessageFrame refresh, so their rect height resolves here.
//   • FUN_00788750 — the SMF display refresh: budgets vertical space per line
//     with THIS and anchors each line to the previous line's rect.
//   • FUN_0077D4D0 — the multi-line EDITBOX self-size (excluded below; editbox
//     text renders raw markup, so its height must stay literal).
// Adding the icons' overflow therefore grows the line's rect AND the chat
// stack's spacing in one place — a 24px emote in 15px chat takes real vertical
// room instead of overlapping its neighbours.
//
// The amount is 4.3.4's GxuFontGetWrappedTextHeight extraHeight: for each
// wrapped line, how far its tallest icon reaches past the font height, summed
// over the lines. The original's own wrap pass calls the wrap stepper once per
// line, and the stepper co-hook adds each line's overflow (g_heightExtra), so
// the lines are exactly the engine's breaks. The render moves each such line
// and the lines after it down by the same amounts (the emitter co-hook) and
// justifies the taller block (the origin-finalize co-hook), as 4.3.4 does.
//
// The original serves fs+0x100 when it is set and runs no wrap pass, so the
// sum from the last real pass is kept per fontstring and reused while the
// cache holds. Whenever the engine invalidates its cache the next call
// recomputes both, so the sum is never staler than the engine's own height. The fs+0x100 cache
// itself is NEVER written here — it keeps the engine's icon-free value and the
// sum is added per call. The sum is in font heights, bridged by the fs font
// height in the getter's units (never /fs+0x7C). Editbox text measures raw: the
// stepper's InlineInterceptActive gate never adds for it.
using StringHeightInternal_t = float(__fastcall *)(void *fs);
StringHeightInternal_t g_stringHeightOriginal = nullptr;

// Per fontstring: the summed line overflow (font heights) from the engine's
// last real wrapped-height pass, reused while it serves its cached height.
std::unordered_map<void *, float> g_fsHeightExtra;

float __fastcall StringHeight_h(void *fs) {
    if (!LooksReadable(fs))
        return g_stringHeightOriginal(fs);
    // A zero cache means the original runs its wrapped-height pass, calling the
    // wrap stepper once per line; the stepper adds each line's overflow here.
    const bool fresh = Game::Read<float>(fs, Offsets::OFF_FONTSTRING_HEIGHT_CACHE) == 0.0f;
    float extra = 0.0f;
    float *const prevExtra = g_heightExtra;
    g_heightExtra = &extra;
    float base;
    {
        FsScope scope(fs);
        base = g_stringHeightOriginal(fs);
    }
    g_heightExtra = prevExtra;
    if (fresh) {
        if (extra > 0.0f)
            g_fsHeightExtra[fs] = extra;
        else
            g_fsHeightExtra.erase(fs);
    } else {
        auto it = g_fsHeightExtra.find(fs);
        extra = (it != g_fsHeightExtra.end()) ? it->second : 0.0f;
    }
    if (!(extra > 0.0f))
        return base;
    FsPenFont pf;
    if (!ResolveFsPenFont(fs, pf))
        return base;
    return base + extra * pf.fontHInt;
}

static const Game::HookAutoRegister _stringHeightHook{
    Offsets::FUN_FONTSTRING_STRING_HEIGHT, reinterpret_cast<void *>(&StringHeight_h),
    reinterpret_cast<void **>(&g_stringHeightOriginal)};

// --- substring-measure co-hook (wrapped tooltip segments count icons) --------
//
// FUN_FONTSTRING_MEASURE_SUBSTRING measures an ARBITRARY string in the fs's
// font (len 0 = strlen; same measure-core + out/fs+0x7C shape as the width
// getter, no cache). Its icon-relevant consumer is the GameTooltip auto-size
// (FUN_00530640): when a wrap-enabled line's icon-inclusive width exceeds the
// tooltip, it computes the break positions (FUN_00772B60 — icon-aware through
// the wrap-stepper hook) and measures EACH WRAPPED SEGMENT through this,
// taking the max as the tooltip width — so an icon-bearing segment undersized
// its tooltip. Same adjustment as the width hook: the segment's icon advances
// in true pen px, bridged by the font-height ratio. The editbox
// caret/selection callers (FUN_0077DA80 / FUN_0077DE70 / FUN_0077D0D0) must
// keep measuring raw markup: the multi-line editors carry the fs editable bit,
// and the focused single-line inputs measure their input buffer IN PLACE,
// which InlineInterceptActive's focused-buffer pointer test stands down on —
// the same predicate stack as everywhere else. A segment can never start or
// end inside a |T span (the tokenizer hook makes breaks treat a span as one
// token); an unterminated span from a hostile caller falls out as plain text
// (FindIconClose miss), contributing nothing.
using MeasureSubstring_t = float(__fastcall *)(void *fs, void *edx, const uint8_t *text, int len);
MeasureSubstring_t g_measureSubstringOriginal = nullptr;

float __fastcall MeasureSubstring_h(void *fs, void *edx, const uint8_t *text, int len) {
    const float base = g_measureSubstringOriginal(fs, edx, text, len);
    if (!LooksReadable(fs) || !LooksReadable(text))
        return base;
    const bool editable =
        (Game::Read<uint32_t>(fs, Offsets::OFF_FONTSTRING_MEASURE_FLAGS) & 0x1000u) != 0;
    if (!InlineInterceptActive(text, editable))
        return base;
    int n = len;
    if (n <= 0) { // len 0 = whole string, mirroring the original's strlen
        n = 0;
        while (n < 0x4000 && text[n] != '\0')
            ++n;
    } else if (n > 0x4000) {
        n = 0x4000;
    }
    if (!HasInlineTexture(text, n))
        return base;
    FsPenFont pf;
    if (!ResolveFsPenFont(fs, pf))
        return base;
    const float sumPen = SumIconAdvances(text, n, pf.fontHPen, pf.outlinePad, pf.layoutScale);
    return base + sumPen * (pf.fontHInt / pf.fontHPen);
}

static const Game::HookAutoRegister _measureSubstringHook{
    Offsets::FUN_FONTSTRING_MEASURE_SUBSTRING, reinterpret_cast<void *>(&MeasureSubstring_h),
    reinterpret_cast<void **>(&g_measureSubstringOriginal)};

// --- hyperlink-rect co-hook (tall icons stay hoverable) ----------------------
//
// FUN_TEXT_LINK_RECT_ADD appends a link's hit rect with the TEXT band's y
// extent (fontH tall — see Offsets.h). A tall inline icon inside a link
// (|H…|h with a |T…:32|t emote) centres on the line and pokes past that band,
// so only its text-high middle slice was hoverable.
//
// The consumer geometry (FUN_00788c00 → FUN_007a3350 + in-game experiments):
// the SMF rebuilds a HIT REGION per record, anchored to the LINE FONTSTRING's
// TOP-LEFT with the record's y band as offset/height. The band's y values are
// Y-UP (numeric max = screen top; extending the max visibly grew the region
// into the line ABOVE) and node-local, like the glyphs. The emitter has
// already moved the line to make room for its tall icons and the origin
// finalize justifies the taller block (4.3.4's layout), so the band sits on
// the text; it only has to grow toward where the icon reaches past it, which
// is the emitter's vertical alignment: below for top justify, above for
// bottom, half each way for middle. (An earlier version pushed only the
// bottom down by the full overflow — that compensated for text centred in a
// grown rect, the offset the finalize co-hook now removes.) 4.3.4 itself keeps
// the band at the text line; covering the icon is ClassicAPI's addition so a
// tall icon inside a link hovers over its whole height. escStart/escLen span
// the whole |H…|h escape, so any |T inside is found; the InlineInterceptActive
// gate keeps raw-rendered contexts (suppressed editbox delegation) literal.
using LinkRectAdd_t = void(__fastcall *)(void *node, void *edx, float yA, float xLeft, float yB,
                                         float xRight, const char *linkStart, uint32_t linkLen,
                                         const uint8_t *escStart, uint32_t escLen);
LinkRectAdd_t g_linkRectAddOriginal = nullptr;

void __fastcall LinkRectAdd_h(void *node, void *edx, float yA, float xLeft, float yB, float xRight,
                              const char *linkStart, uint32_t linkLen, const uint8_t *escStart,
                              uint32_t escLen) {
    if (g_inlineEnabled && LooksReadable(node) && LooksReadable(escStart) && escLen > 0 &&
        escLen < 0x4000 && InlineInterceptActive(escStart, false)) {
        const int len = static_cast<int>(escLen);
        if (HasInlineTexture(escStart, len)) {
            // The line's font pixel height, the emitter's own recipe (ecx =
            // (nodeFlags>>7)&1, stack = node fontSize).
            const uint32_t nodeFlags = Game::Read<uint32_t>(node, Offsets::OFF_TEXT_NODE_FLAGS);
            const float fontSize = Game::Read<float>(node, Offsets::OFF_TEXT_NODE_FONT_SIZE);
            const float fontH = reinterpret_cast<float(__fastcall *)(int, float)>(
                Offsets::FUN_TEXT_FONT_HEIGHT)(static_cast<int>((nodeFlags >> 7) & 1u), fontSize);
            const float overflow = IconOverflowPx(escStart, len, fontH, NodeLayoutScale(node));
            if (overflow > 0.0f) {
                // y-up band: the numeric min edge is the screen bottom.
                const int justifyV = Game::Read<int>(node, Offsets::OFF_TEXT_NODE_JUSTIFY_V);
                const float below =
                    (justifyV == 0) ? overflow : (justifyV == 2) ? 0.0f : overflow * 0.5f;
                const float above = overflow - below;
                float &lo = (yA <= yB) ? yA : yB;
                float &hi = (yA <= yB) ? yB : yA;
                lo -= below;
                hi += above;
            }
        }
    }
    g_linkRectAddOriginal(node, edx, yA, xLeft, yB, xRight, linkStart, linkLen, escStart, escLen);
}

static const Game::HookAutoRegister _linkRectAddHook{Offsets::FUN_TEXT_LINK_RECT_ADD,
                                                     reinterpret_cast<void *>(&LinkRectAdd_h),
                                                     reinterpret_cast<void **>(&g_linkRectAddOriginal)};

// --- wrap-stepper co-hook (icon-aware line breaks) ---------------------------
//
// FUN_TEXT_WRAP_STEPPER lays out one wrapped line per call (see Offsets.h).
// It measures the line through the tokenizer, which eats each `|T` span as
// ~zero width — so the break lands as if the icons weren't there, and the
// emitter's real advances then overflow the right edge (chat lines with
// prefix icons ran past the frame). Fix: shrink the wrapWidth argument by the
// measured widths of the icons in the remaining text (the SumIconAdvances
// math the width hook uses), then call the original. `text` always points at the
// REMAINING text, so a continuation line whose icons are already behind it
// gets sum = 0 — chat prefix icons come out exact. An icon that would land on
// a LATER wrapped line still shrinks the earlier calls' width, wrapping those
// lines slightly early — the safe direction (never overflow), cosmetic only.
//
// Because all four wrap consumers route through this one dispatcher, the
// render's breaks, GetStringHeight's line count, ellipsis truncation, and the
// break arrays all shift together — no cross-consumer drift.
//
// UNITS (bit us TWICE): the stepper's inputs are gxu-NORMALIZED, per axis —
// fontH is normalized-Y (× rasterY = pen px, FUN_005c6fa0) and
// wrapWidth/outWidth are normalized-X (× rasterX = pen px). Both caller
// flavors land there: the draw builder passes node+0x1C/+0x3C (normalized at
// block creation via FUN_0041ae50/ae40), and the fs-level callers (height,
// truncation, break arrays) x-normalize their widths
// inside FUN_0044d670 before gxu. First flight subtracted raw pixels and
// annihilated the small normalized widths (2-glyph chat fragments); the
// second used fontH/fontHPx — a Y-axis factor on an X-axis width —
// overshrinking by the render aspect (×2.37 at 2560×1080; early-wrapped
// icon chat lines). The correct pen-x → width-units factor is 1/rasterX.
using WrapStepper_t = void(__fastcall *)(void *font, uint8_t *text, float fontH,
                                         float wrapWidth, int *outBreak, float *outWidth,
                                         void *outNext, float indent, uint32_t flags,
                                         uint8_t *p10);
WrapStepper_t g_wrapStepperOriginal = nullptr;

// Inside the string-height pass (g_heightExtra set): add this wrapped line's
// icon overflow, in font heights. The line is [text, break) as the stepper
// just reported it, so only icons that landed on it count — CalcWrapPoint's
// height out-param, which GxuFontGetWrappedTextHeight sums.
void AddLineOverflow(const uint8_t *text, float fontH, uint32_t flags, const int *outBreak,
                     void *outNext) {
    if (!(fontH > 0.0f) || !InlineInterceptActive(text, (flags & 0x40u) != 0))
        return;
    int len = 0;
    while (len < 2048 && text[len] != '\0' && text[len] != '\n')
        ++len;
    if (!HasInlineTexture(text, len))
        return;
    int lineLen = len;
    const int brk = (outBreak != nullptr) ? *outBreak : 0;
    const uint8_t *next =
        (outNext != nullptr) ? *static_cast<const uint8_t *const *>(outNext) : nullptr;
    if (brk > 0 && brk < len)
        lineLen = brk;
    else if (next > text && next - text < len)
        lineLen = static_cast<int>(next - text);
    const float fontHPx = reinterpret_cast<float(__fastcall *)(int, float)>(
        Offsets::FUN_TEXT_FONT_HEIGHT)(static_cast<int>((flags >> 7) & 1u), fontH);
    if (!(fontHPx > 0.0f))
        return;
    const float overflow = IconOverflowPx(text, lineLen, fontHPx, WrapLayoutScale());
    if (overflow > 0.0f)
        *g_heightExtra += overflow / fontHPx;
}

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
            // measure loops and the emitter use.
            const int fontFlag = static_cast<int>((flags >> 7) & 1u);
            const float fontHPx = reinterpret_cast<float(__fastcall *)(int, float)>(
                Offsets::FUN_TEXT_FONT_HEIGHT)(fontFlag, fontH);
            const int rasterX = Game::Read<int>(Offsets::VAR_TEXT_RASTER_X);
            if (fontHPx > 0.0f && rasterX > 0) {
                // Pen-x px → the stepper's WIDTH units. fontH is normalized-Y
                // (× rasterY = pen) but wrapWidth/outWidth are normalized-X
                // (× rasterX = pen) — both caller flavors: the draw builder
                // passes node+0x3C (x-normalized at block creation via
                // FUN_0041ae40) and the fs-level callers x-normalize their
                // widths through FUN_0044d670's own FUN_0041ae40 call before
                // gxu. The first version scaled by fontH/fontHPx — a Y-AXIS
                // conversion applied to an x-axis width — overshrinking the
                // budget by rasterX/rasterY (the render aspect: ×1.33 at 4:3,
                // ×2.37 at 2560×1080), so icon-bearing chat lines wrapped a
                // word or two early, worse per icon and per aspect. The
                // glyph-relative tolerances below use fontHPx × this factor so
                // "half a glyph" means an actual glyph width in width units.
                const float penToUnits = 1.0f / static_cast<float>(rasterX);
                const float glyphU = fontHPx * penToUnits; // one glyph width, width units
                const float minWidth = glyphU * 2.0f;
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
                    // during bring-up); outBreak == len means the whole text fit.
                    int lineLen = len;
                    if (pBreak > 0 && pBreak < len)
                        lineLen = pBreak;
                    else if (pNext > text && pNext - text < len)
                        lineLen = static_cast<int>(pNext - text);
                    const float sumPx = SumIconAdvances(text, lineLen, fontHPx,
                                                        OutlinePadPen(font, fontHPx),
                                                        WrapLayoutScale());
                    const float iconUnits = sumPx * penToUnits;
                    float slack = probeW - pWidth;
                    if (slack < 0.0f)
                        slack = 0.0f;
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
                    if (iconUnits <= s + slack + glyphU * 0.5f) {
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
                                next = s + glyphU;
                            s = next;
                            continue;
                        }
                    }
                    // A [lo, best] bracket exists — bisect until it's tighter
                    // than a quarter-glyph.
                    if (lo < 0.0f || best < 0.0f)
                        break; // feasible with no infeasible below → best = s
                    if (best - lo < glyphU * 0.25f)
                        break;
                    s = (lo + best) * 0.5f;
                }
                const float finalShrink = (best >= 0.0f) ? best : s;
                float shrunk = wrapWidth - finalShrink;
                if (shrunk < minWidth)
                    shrunk = minWidth;
                wrapWidth = shrunk;
            }
        }
    }
    g_wrapStepperOriginal(font, text, fontH, wrapWidth, outBreak, outWidth, outNext, indent,
                          flags, p10);
    if (g_heightExtra != nullptr && text != nullptr)
        AddLineOverflow(text, fontH, flags, outBreak, outNext);
}

static const Game::HookAutoRegister _wrapStepperHook{
    Offsets::FUN_TEXT_WRAP_STEPPER, reinterpret_cast<void *>(&WrapStepper_h),
    reinterpret_cast<void **>(&g_wrapStepperOriginal)};

// Break-array co-hook: the third fs-level wrap caller (the tooltip auto-size's
// segment breaks, FontString:GetNumLines / GetWrappedWidth). It passes gxu the
// font handle only, so the fontstring's layout scale reaches the stepper
// through the scope — see g_scopeFs.
using BreakArray_t = uint32_t(__fastcall *)(void *fs, void *edx, const char *text, float width,
                                            int *outBreaks, int cap);
BreakArray_t g_breakArrayOriginal = nullptr;

uint32_t __fastcall BreakArray_h(void *fs, void *edx, const char *text, float width,
                                 int *outBreaks, int cap) {
    FsScope scope(fs);
    return g_breakArrayOriginal(fs, edx, text, width, outBreaks, cap);
}

static const Game::HookAutoRegister _breakArrayHook{
    Offsets::FUN_FONTSTRING_BREAK_ARRAY, reinterpret_cast<void *>(&BreakArray_h),
    reinterpret_cast<void **>(&g_breakArrayOriginal)};

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
    // pointer path (`text` inside the input buffer, rare), and any editable node
    // (flags bit 6, e.g. the un-focused macro editor). Per-editbox, not global: a
    // chat-history line's content differs, so its icons keep rendering while an
    // editbox is focused.
    const bool sup_ptr = TextInFocusedEditbox(text);
    const bool sup_content = !sup_ptr && EmitLineIsFocusedEditbox(text);
    // Suppress on the non-space-wrap proxy bit (bit 6; see NodeEditable). Verified
    // via the ICON-NODE probe: chat DISPLAY has it clear (flags 0x205), the macro
    // editor has it set (0x4D), the focused chat edit box has it clear but is
    // content-matched (0x20D). So the bit cleanly catches the un-focused macro
    // editor (which content-match can't, since it's not the focused editbox)
    // without touching chat display. Content-match still handles the focused chat/
    // name box. (Caveat: a display fs with SetNonSpaceWrap(true) would also match
    // here — rare, never observed; see NodeEditable.)
    const bool sup_editable = node != nullptr && NodeEditable(node);
    if (sup_ptr || sup_content || sup_editable) {
        if (node != nullptr)
            g_nodeIcons.erase(node);
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
            : (text == Game::Read<uint8_t *>(node, Offsets::OFF_TEXT_NODE_TEXT));
    if (firstLine)
        g_nodeIcons.erase(node);

    if (!HasInlineTexture(text, len)) {
        // No inline texture on this line — render it normally. Note: firstLine
        // already erased the node's stale records above, so a REUSED node
        // address whose new text has no markup is cleaned HERE, by the emitter.
        // This is the ghost-icon protection; the flush must NOT re-derive it by
        // scanning node text (that scan read a stale/preprocessed pointer on
        // pfUI chat lines and erased LIVE records every frame — the persistent
        // iconless LFG lines).
        g_emitterOriginal(node, edx, text, len, colorState, penXYZ, pageMask, linkState);
        return;
    }

    // This line owns inline textures — append them, rendering the plain runs by
    // delegating to the original per segment.
    std::vector<IconRecord> &icons = g_nodeIcons[node];

    // Stamp the base colour this bake is built from — the flush re-bakes the
    // node if the live node colour's RGB later diverges (see g_nodeBakedColor).
    g_nodeBakedColor[node] = Game::Read<uint32_t>(node, Offsets::OFF_TEXT_NODE_COLOR);

    // Bit 3 of the node flags gates the emitter's per-call batch-clear. When set
    // (the standalone-FontString case), each original call would wipe the page
    // batches, so segmenting would lose all but the last run. Handle it by doing
    // the batch-clear ONCE per build (a len-0 original call with bit 3 still set,
    // only on the first wrapped line), then clearing bit 3 so our per-segment calls
    // APPEND. Restore the flags before returning.
    uint32_t *const flagsPtr = Game::Ptr<uint32_t>(node, Offsets::OFF_TEXT_NODE_FLAGS);
    const uint32_t savedFlags = *flagsPtr;
    const bool batchClearMode = (savedFlags & 8u) != 0;
    if (batchClearMode) {
        if (firstLine)
            g_emitterOriginal(node, edx, text, 0, colorState, penXYZ, pageMask, linkState);
        *flagsPtr = savedFlags & ~8u;

        // Clearing bit 3 switches the engine from the FontString's UNIFORM colour
        // (node+0x2c, consulted live at every paint) to PER-GLYPH colour baked
        // from colorState — a copy of [node+0x2c] the draw builder took at build
        // start. Its alpha byte is the FOLDED live opacity (SetTextColor alpha ×
        // frame-chain effective alpha; the engine re-folds it into fs colors[0]
        // and node+0x2c on every alpha change — FUN_0077fac0), so it is correct
        // AT BUILD TIME and needs no fixup here. But baked per-glyph alphas
        // never refresh: the colour-change path (FUN_005ccb40) skips the
        // rebuild-invalidate for bit-3 nodes, so a later fade (parent SetAlpha)
        // updates +0x2c while the glyphs keep their build-time alpha. The
        // flush's baked-alpha mirror (FlushLayout) re-syncs them per frame.
        // (An earlier build forced alpha 0xFF here — a misread of a pfUI bubble
        // that BUILT while faded out: 0x07 was the live folded alpha, not a
        // stale default. The force inverted the bug: segmented text stuck fully
        // opaque while its icons and iconless siblings faded.)
    }

    // Font pixel height of this line. Mirrors the emitter's own call: ecx =
    // (nodeFlags>>7)&1, stack = the node's font size [node+0x1c].
    const int fontFlag = static_cast<int>((savedFlags >> 7) & 1u);
    const float fontSize = Game::Read<float>(node, Offsets::OFF_TEXT_NODE_FONT_SIZE);
    const float fontH = reinterpret_cast<float(__fastcall *)(int, float)>(
        Offsets::FUN_TEXT_FONT_HEIGHT)(fontFlag, fontSize);

    // Font face + native→pen scale, mirroring the emitter's own
    // `local_10 = fontH / (float)nativeHeight` (glyph-record metrics are in
    // native-font units).
    void *const fontFace = Game::Read<void *>(node, Offsets::OFF_TEXT_NODE_FONT_FACE);
    using FontNativeHeight_t = int(__fastcall *)(void *font);
    const int fontNativeH =
        (fontFace != nullptr)
            ? reinterpret_cast<FontNativeHeight_t>(Offsets::FUN_TEXT_FONT_NATIVE_HEIGHT)(fontFace)
            : 0;
    const float penScale = (fontNativeH > 0) ? fontH / static_cast<float>(fontNativeH) : 0.0f;
    const float outlinePad = OutlinePadPen(fontFace, fontH);
    const float layoutScale = NodeLayoutScale(node);
    // The node's pixel-snap mode (bit 7 clear): the emitter rounds every glyph
    // step and floors every glyph width in this mode.
    const bool snapNode = (savedFlags & 0x80u) == 0;

    // The original emitter never writes penXYZ[0]; we mutate it to thread the pen
    // across segments, so snapshot and restore it. The draw builder does NOT reset
    // penXYZ[0] between left-justified lines, so leaving it mutated would cascade-
    // shift every following line.
    const float startX = penXYZ[0];
    float penX = penXYZ[0];

    // The line's vertical band, y-up: the pen y is its baseline and the draw
    // builder hands each line's top in the link state ({state, top, left,
    // baseline, right} — FUN_005cdc20 passes &local_78 with local_74[0] =
    // baseline + font height + line spacing). These are exactly 4.3.4's
    // m_baseLineHeight / m_lineHeight for the line.
    //
    // A line whose tallest icon reaches past the font height moves down by
    // that overflow, and every later line with it — 4.3.4's InitializeText:
    // all of it before the line for bottom justify, all after for top, and
    // floor(overflow / 2) before and after for middle. The builder keeps the
    // pen y and the band in locals it hands over by pointer and lowers per
    // line (penXYZ[1] = local_48, linkState[1]/[3] = local_74[0]/[2]), so
    // moving them here carries into the lines that follow. The node's extra
    // height (m_extraLineHeight) feeds the origin finalize. Only inside a
    // tracked build — that is where the builder owns those locals.
    const int justifyV = Game::Read<int>(node, Offsets::OFF_TEXT_NODE_JUSTIFY_V);
    const float lineOverflow =
        (node == g_buildNode) ? IconOverflowPx(text, len, fontH, layoutScale) : 0.0f;
    float lineShift = lineOverflow;
    if (justifyV == 1)
        lineShift = std::floor(lineShift * 0.5f);
    auto shiftLine = [&]() {
        penXYZ[1] -= lineShift;
        *reinterpret_cast<float *>(&linkState[1]) -= lineShift;
        *reinterpret_cast<float *>(&linkState[3]) -= lineShift;
    };
    if (lineOverflow > 0.0f) {
        g_nodeExtra[node] += lineOverflow;
        if (justifyV != 0)
            shiftLine();
    }
    const float baseline = penXYZ[1];
    float lineTop = *reinterpret_cast<const float *>(&linkState[1]);
    if (!(lineTop > baseline))
        lineTop = baseline + fontH;

    // Centre/right-justify correction. The engine positioned the pen (penXYZ[0])
    // using this line's MEASURED width, which counts inline icons as ~zero (the
    // tokenizer measure gap). Pre-sum this line's icon widths and shift the pen
    // left by half that (centre) or all of it (right) so the whole text+icons
    // block is justified as a unit. Left-justify (chat, justify 0) needs no
    // shift. node+0x54: 1 = centre, 2 = right (verified in the draw builder
    // FUN_005cdc20's justify branch).
    const int justify = Game::Read<int>(node, Offsets::OFF_TEXT_NODE_JUSTIFY);
    if (justify == 1 || justify == 2) {
        float shift = SumIconAdvances(text, len, fontH, outlinePad, layoutScale);
        if (justify == 1)
            shift *= 0.5f;
        if (snapNode)
            shift = std::floor(shift + 0.5f);
        penX -= shift;
    }

    // Draws a plain run [start,start+n) via the original emitter and returns
    // where an icon that follows it starts.
    //
    // 4.3.4 starts an icon at the previous glyph's INK edge — its bearing plus
    // its cell width, never less than its own x — plus the outline gap
    // (InitializeTextLine case CODE_TEXTURESTART). With no glyph in the run
    // the icon starts at the run's pen, with no gap.
    //
    // 1.12's emitter does not report where its last glyph sits: its pen is
    // LAZY (each glyph lands at pen + pairAdvance(prev, cur), so the final
    // write in linkState[4] carries a stand-in step, not the glyph's end). So
    // the run's glyph placements are replayed with the emitter's own math —
    // the original tokenizer (runs never contain icon spans), the same
    // pair-advance variant, the same snap rounding (`FADD 0.5; __ftol`), the
    // same skip for glyphs the face can't produce — to find the last glyph's
    // x, and its quad is measured with the helpers the emitter draws it with.
    using GlyphPairAdvance_t = float(__thiscall *)(void *font, uint32_t prevCh, uint32_t curCh);
    using GlyphBaseAdvance_t = float(__thiscall *)(void *font, uint32_t ch);
    using GlyphDesc_t = uint8_t *(__thiscall *)(void *font, uint32_t ch);
    using GlyphBearing_t = float(__thiscall *)(void *font, uint8_t *desc, char scaled,
                                               float fontSize);
    auto drawRun = [&](size_t start, int n) -> float {
        const float runPen = penX;
        penXYZ[0] = penX;
        g_emitterOriginal(node, edx, text + start, n, colorState, penXYZ, pageMask, linkState);
        if (n <= 0 || penScale <= 0.0f || fontFace == nullptr)
            return runPen;
        const auto glyphDesc = reinterpret_cast<GlyphDesc_t>(Offsets::FUN_TEXT_GLYPH_DESC);
        const uintptr_t pairFn = ((savedFlags & 0x10u) != 0)
                                     ? Offsets::FUN_TEXT_GLYPH_PAIR_ADVANCE_ALT
                                     : Offsets::FUN_TEXT_GLYPH_PAIR_ADVANCE;
        float x = runPen;
        uint32_t prevCh = 0; // the emitter's local_8: last glyph DRAWN
        uint8_t *lastDesc = nullptr;
        float lastX = 0.0f;
        uint32_t payload = 0; // persists across tokens, like the emitter's local_c
        const uint8_t *p = text + start;
        int rem = n;
        while (rem > 0 && *p != 0) {
            int consumed = 0;
            uint32_t colorOut = 0;
            const uint32_t tok = g_tokenizerOriginal(const_cast<uint8_t *>(p), &consumed,
                                                     &colorOut, savedFlags, &payload);
            if (consumed <= 0 || consumed > rem)
                break;
            p += consumed;
            rem -= consumed;
            // Emitter switch: 0 colour / 1 reset / 2 break / 4 |H / 5 |h; every
            // other token (6 glyph, 3 literal ||) draws.
            if (tok == 0 || tok == 1 || tok == 2 || tok == 4 || tok == 5)
                continue;
            uint8_t *desc = glyphDesc(fontFace, payload);
            if (desc == nullptr)
                continue; // the emitter draws nothing and keeps its previous glyph
            float step = 0.0f;
            if (prevCh != 0) {
                step = reinterpret_cast<GlyphPairAdvance_t>(pairFn)(fontFace, prevCh, payload) *
                       penScale;
                if (snapNode)
                    step = static_cast<float>(static_cast<int>(step + 0.5f));
            }
            x += step;
            lastX = x;
            lastDesc = desc;
            prevCh = payload;
        }
        if (lastDesc == nullptr)
            return runPen;
        const float bearing = reinterpret_cast<GlyphBearing_t>(Offsets::FUN_TEXT_GLYPH_BEARING)(
            fontFace, lastDesc, static_cast<char>(fontFlag), fontSize);
        float cell = static_cast<float>(
                         Game::Read<uint32_t>(lastDesc, Offsets::OFF_GLYPH_DESC_CELL_WIDTH)) *
                     penScale;
        if (snapNode)
            cell = std::floor(cell);
        float ink = bearing + cell;
        if (ink < 0.0f)
            ink = 0.0f;
        // Never past the glyph's own advance. Every measure — the wrap stepper
        // and the width hooks — counts the glyph by its advance and the icon
        // by its width, so an ink edge beyond the advance (a glyph cell wider
        // than its step) pushed each following icon, and the rest of the line,
        // past the measured width: wrapped chat lines overran the frame edge.
        float advance = reinterpret_cast<GlyphBaseAdvance_t>(
                            Offsets::FUN_TEXT_GLYPH_BASE_ADVANCE)(fontFace, prevCh) *
                        penScale;
        if (snapNode)
            advance = std::floor(advance + 0.5f);
        if (ink > advance)
            ink = advance;
        return lastX + ink + outlinePad;
    };

    size_t runStart = 0;
    int i = 0;
    while (i < len) {
        if (text[i] != '|') {
            ++i;
            continue;
        }
        char kind = 'T';
        int mlen = IconStartLen(text, len, i, &kind);
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
        size_t close =
            FindIconClose(text, len, static_cast<size_t>(i) + mlen, doubled, &closeLen, kind);
        if (close == static_cast<size_t>(-1)) {
            // Unterminated on this line — treat the rest as plain text.
            break;
        }
        // Render the plain run before the icon; it reports where the icon starts.
        const float left = drawRun(runStart, i - static_cast<int>(runStart));

        IconDesc d;
        const char *payload = reinterpret_cast<const char *>(text) + i + mlen;
        size_t payloadLen = close - (static_cast<size_t>(i) + mlen);
        if (ParseIconSpan(payload, payloadLen, kind, d)) {
            const IconSize sz = ResolveIconSize(d, fontH, layoutScale);
            // Vertical alignment by the node's justify-V, 4.3.4's three cases
            // (y-up): top → the icon's top on the line top; bottom → its
            // bottom on the baseline; middle → centred on the font height
            // below the line top. The line has already moved down to make room
            // (lineShift), so the overflow lands in space no other line uses.
            float bottom;
            if (justifyV == 0)
                bottom = lineTop - sz.h;
            else if (justifyV == 2)
                bottom = baseline;
            else
                bottom = lineTop + (sz.h - fontH) * 0.5f - sz.h;
            IconRecord r;
            r.path = d.path;
            r.left = left;
            r.bottom = bottom;
            r.w = sz.w;
            r.h = sz.h;
            r.offsetX = d.offsetX;
            r.offsetY = d.offsetY;
            r.u0 = d.u0;
            r.v0 = d.v0;
            r.u1 = d.u1;
            r.v1 = d.v1;
            r.color = d.color;
            icons.push_back(std::move(r));
            // The next glyph starts at the icon's right edge, rounded — 4.3.4
            // `pos.x = floor(right + 0.5)`, with no kerning against the icon.
            penX = std::floor(left + sz.w + 0.5f);
        } else {
            penX = left;
        }
        penXYZ[0] = penX;
        i = static_cast<int>(close) + closeLen; // skip past the closing marker
        runStart = static_cast<size_t>(i);
    }

    // Trailing plain run (also finalizes pen/link state for the builder).
    drawRun(runStart, len - static_cast<int>(runStart));

    // The rest of the tall line's move (see lineShift): after it for top and
    // middle justify.
    if (lineOverflow > 0.0f && justifyV != 2)
        shiftLine();

    // Restore the pen origin to match the engine's own post-call invariant, and the
    // node flags (bit 3) we cleared for the segmented append.
    penXYZ[0] = startX;
    if (batchClearMode)
        *flagsPtr = savedFlags;
}

static const Game::HookAutoRegister _emitterHook{Offsets::FUN_TEXT_EMITTER,
                                                 reinterpret_cast<void *>(&Emitter_h),
                                                 reinterpret_cast<void **>(&g_emitterOriginal)};

// --- paint co-hook: queue recorded icons as region placements ---------------

using Paint_t = void(__fastcall *)(void *layout);
Paint_t g_paintOriginal = nullptr;

// Builds the region placements for `node`'s recorded icons, expressed RELATIVE
// to the owning fontstring `fs`'s rect min corner (the pool anchors them there,
// so the engine moves them with the line). Fills `out` and returns true ONLY
// when everything the placement needs is ready this instant — K live (raster
// globals written) and the fs rect resolved. Returns false otherwise, and the
// caller must NOT queue an empty set (that would hide the icons and the dedup
// would freeze them hidden — the scroll-landing bug's second head). Shared by
// the paint flush and the SMF-refresh early apply so both produce
// bit-identical placements (the pool's dedup depends on that). The per-icon
// geometry is what the emitter recorded — see its icon loop.
bool ComputePlacementsForNode(void *node, void *fs,
                              std::vector<Text::InlineTexturePool::Placement> &out) {
    if (!LooksReadable(node) || !LooksReadable(fs))
        return false;
    auto itIcons = g_nodeIcons.find(node);
    if (itIcons == g_nodeIcons.end() || itIcons->second.empty())
        return false;
    const PenScale K = PenPerAnchor();
    if (!(K.x > 1.0f) || !(K.y > 1.0f))
        return false;
    auto *n = reinterpret_cast<uint8_t *>(node);
    const float ox = Game::Read<float>(n, Offsets::OFF_TEXT_NODE_ORIGIN_X);
    const float oy = Game::Read<float>(n, Offsets::OFF_TEXT_NODE_ORIGIN_Y);
    // Same pixel-snap mode the emitter honours (bit-7 clear) — gates the
    // drawn-rect snap in the placement loop below.
    const bool snapNode = (Game::Read<uint32_t>(n, Offsets::OFF_TEXT_NODE_FLAGS) & 0x80u) == 0;
    const float *rc = Game::Ptr<const float>(fs, Offsets::OFF_REGION_RECT);
    const float fsBottom = (rc[0] < rc[2]) ? rc[0] : rc[2];
    const float fsLeft = (rc[1] < rc[3]) ? rc[1] : rc[3];
    if (rc[1] == rc[3])
        return false; // unresolved rect reads 0-width
    for (const IconRecord &r : itIcons->second) {
        // The emitter recorded the icon's left and bottom edges node-local
        // (y-up); the offsets move only the drawn texture, right and up, as
        // 4.3.4's InitEmbeddedTexture adds them to its SetPoint offsets.
        float rx = r.left + ox + r.offsetX;
        float y0 = r.bottom + oy + r.offsetY;
        float w = r.w, h = r.h;
        if (snapNode) {
            // Land the drawn rect on WHOLE render-target pixels, like the
            // engine's own glyphs (crisp texel sampling). Size snaps
            // independently of position so the icon keeps its rounded size.
            rx = std::floor(rx + 0.5f);
            y0 = std::floor(y0 + 0.5f);
            w = std::floor(w + 0.5f);
            h = std::floor(h + 0.5f);
            if (w < 1.0f)
                w = 1.0f;
            if (h < 1.0f)
                h = 1.0f;
        }
        Text::InlineTexturePool::Placement p;
        p.path = r.path;
        p.x0 = rx / K.x - fsLeft;
        p.y0 = y0 / K.y - fsBottom;
        p.x1 = (rx + w) / K.x - fsLeft;
        p.y1 = (y0 + h) / K.y - fsBottom;
        p.color = r.color;
        p.u0 = r.u0;
        p.v0 = r.v0;
        p.u1 = r.u1;
        p.v1 = r.v1;
        out.push_back(std::move(p));
    }
    return true;
}

// Walks the layout's live render nodes and, for each recorded icon, computes
// its screen geometry (node-local pen coords translated by the node's screen
// origin +0x70/+0x74 — the SAME transform the paint pass applies to glyph
// batches) and queues it to Text::InlineTexturePool as a placement RELATIVE TO
// THE OWNING FONTSTRING. Runs after the original paint so icon coords and the
// fs rect are read in one coherent snapshot; the pool applies placements on
// the next FrameTick (regions are never mutated mid-render).
void FlushLayout(void *layout) {
    if (!LooksReadable(layout))
        return;
    const int linkOff = Game::Read<int>(layout, Offsets::OFF_TEXT_LAYOUT_NODE_LINK);
    void *node = Game::Read<void *>(layout, Offsets::OFF_TEXT_LAYOUT_NODE_HEAD);

    for (int guard = 0; node != nullptr && (reinterpret_cast<uintptr_t>(node) & 1) == 0 &&
                        guard < 4096;
         ++guard) {
        if (!LooksReadable(node))
            break;
        auto *n = reinterpret_cast<uint8_t *>(node);
        void *const next = Game::Read<void *>(n, linkOff + 4);

        // Resolve the owning fontstring up front: a node with NO icons must
        // still clear its fontstring's regions — the fs may have just been
        // re-SetText'd from icon text to plain text (chat line slot reuse).
        // A node only speaks for its fontstring while it IS the fontstring's
        // CURRENT text node (fs+0xF8 → handle+8): around a rebuild, the old and
        // new node can both be walked in one paint, and letting the stale one
        // queue (especially a clear) made icons vanish nondeterministically.
        void *fs = nullptr;
        {
            auto ow = g_nodeOwner.find(node);
            if (ow != g_nodeOwner.end() && LooksReadable(ow->second)) {
                void *block = Game::Read<void *>(ow->second, Offsets::OFF_FONTSTRING_TEXT_BLOCK);
                if (LooksReadable(block) &&
                    Game::Read<void *>(block, Offsets::OFF_TEXTBLOCK_NODE) == node) {
                    fs = ow->second;
                } else if (block == nullptr) {
                    // Stuck-blockless fs: RebuildString released the block (a
                    // SetText landed on an unresolved rect) and never rebuilt;
                    // this zombie node keeps painting the old text. Re-set the
                    // fs's rebuild-dirty bit so the engine rebuilds next update
                    // and the icon pipeline resumes. Byte write only — safe
                    // from the paint tail (consumed by the fs's own update).
                    Game::Ref<uint8_t>(ow->second, Offsets::OFF_FONTSTRING_DIRTY_FLAGS) |= 1u;
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
        if (it != g_nodeIcons.end() && !it->second.empty() && fs != nullptr) {
            const char *ftext = Game::Read<const char *>(fs, Offsets::OFF_FONTSTRING_TEXT);
            bool fsHasMarkup = false;
            if (LooksReadable(ftext)) {
                for (int k = 1; k < 2048 && ftext[k] != '\0'; ++k)
                    if ((ftext[k] == 'T' || ftext[k] == 'A') && ftext[k - 1] == '|') {
                        fsHasMarkup = true;
                        break;
                    }
            }
            if (!fsHasMarkup) {
                g_nodeIcons.erase(it);
                it = g_nodeIcons.end();
            }
        }
        if (it == g_nodeIcons.end() || it->second.empty()) {
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
            // Baked-alpha mirror. Segmented lines carry PER-GLYPH colours (the
            // emitter hook clears bit 3 during emit) and the paint uses those
            // VERBATIM — while the node's live opacity (node+0x2c alpha:
            // SetTextColor alpha × frame-chain effective alpha, re-folded by
            // the engine on every alpha change) is only consulted for UNIFORM
            // text. A bit-3 node's colour-change path skips the rebuild-
            // invalidate, so a fade (parent SetAlpha) updated +0x2c while the
            // baked glyph alphas froze at their build-time value: text stayed
            // put while the icons faded. Mirror the live alpha into every
            // baked colour, change-detected on each page's first entry (all
            // baked alphas are uniform — the engine's own |c splice writes the
            // node alpha too). One-frame lag (flush runs post-paint), same as
            // the icon fade mirror. Chat (bit-3 clear) is excluded: the engine
            // re-bakes those on colour change itself.
            if ((Game::Read<uint32_t>(n, Offsets::OFF_TEXT_NODE_FLAGS) & 8u) != 0) {
                const uint32_t liveColor = Game::Read<uint32_t>(n, Offsets::OFF_TEXT_NODE_COLOR);
                auto baked = g_nodeBakedColor.find(node);
                if (baked != g_nodeBakedColor.end() &&
                    ((baked->second ^ liveColor) & 0x00FFFFFFu) != 0) {
                    // The base colour's RGB changed since the bake (the glue
                    // AddonList's gold↔gray toggle). Baked per-glyph RGB can't
                    // be patched in place — |c runs own their colours — so
                    // re-bake, exactly what the engine's colour setter does
                    // for accumulation nodes: invalidate the node and the next
                    // paint's pre-pass re-runs the builder (which re-stamps
                    // g_nodeBakedColor and re-records the icons). Stamp first
                    // so a delayed rebuild can't re-trigger every flush.
                    baked->second = liveColor;
                    reinterpret_cast<void(__fastcall *)(void *)>(
                        Offsets::FUN_TEXT_NODE_INVALIDATE)(node);
                } else {
                    const uint8_t liveA = static_cast<uint8_t>(liveColor >> 24);
                    for (int page = 0; page < Offsets::TEXT_NODE_PAGE_COUNT; ++page) {
                        auto *buf = Game::Read<uint8_t *>(
                            n, Offsets::OFF_TEXT_NODE_PAGE_BUFFERS + page * 4);
                        if (buf == nullptr)
                            continue;
                        const int count = Game::Read<int>(buf, Offsets::OFF_TEXT_PAGE_COLOR_COUNT);
                        auto *colors = Game::Read<uint8_t *>(buf, Offsets::OFF_TEXT_PAGE_COLORS);
                        if (colors == nullptr || count <= 0 || colors[3] == liveA)
                            continue;
                        for (int i = 0; i < count; ++i)
                            colors[i * 4 + 3] = liveA;
                    }
                }
            }
            // Geometry + readiness gate factored into ComputePlacementsForNode
            // (shared with the SMF-refresh early apply so both produce
            // bit-identical placements — the pool dedup depends on it). It
            // returns false when K or the fs rect isn't ready; SKIP the queue
            // then and retry next paint. An empty queue here would HIDE the
            // line's icons and the dedup would freeze it hidden forever
            // (identical empty re-queues never dirty) — the scroll-landing
            // bug's second head, where lines painted before K was available
            // queued {} and stayed iconless until their text changed.
            std::vector<Text::InlineTexturePool::Placement> places;
            if (ComputePlacementsForNode(node, fs, places))
                Text::InlineTexturePool::QueuePlacements(fs, std::move(places));
        } else if (fs != nullptr) {
            Text::InlineTexturePool::QueuePlacements(fs, {});
        }
        node = next;
    }
}

// SEH latch: a fault anywhere in the flush disables the whole feature for the
// session instead of crashing the client (re-enable via
// _classicapi_InlineTexEnable after a /reload if it was transient).
void SafeFlush(void *layout) {
    __try {
        FlushLayout(layout);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
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

// --- SMF-refresh early apply (kills the one-frame chat-icon lag) -------------
//
// The lag: a ScrollingMessageFrame re-SetTexts every visible line slot on each
// new message (FUN_00788750), but the engine builds each line lazily at PAINT.
// So a line's icons are recorded — and its regions placed — one frame AFTER its
// glyphs draw, because a frame's textures (our icon regions) draw before its
// fontstring glyphs and the placement isn't computed until the glyph build runs
// mid-paint. This co-hook closes it: after the refresh sets up the lines (still
// PRE-RENDER — the refresh runs from the SMF's Lua/event-driven methods, never
// mid-render), force each icon-bearing line to resolve+build and apply its icon
// placement synchronously, so the icons draw with their glyphs the same frame.
//
// Additive and best-effort: any line we can't resolve/build in time (rect still
// pending) falls back to the existing paint-flush + FrameTick pipeline (correct,
// one-frame lagged). Gated by its own toggle (default on) so it can be A/B'd and
// disabled independently of the whole feature; the SEH latch trips it on a fault.
bool g_earlyApply = true;

using SmfRefresh_t = void(__fastcall *)(void *smf, void *edx, int newestIdx);
SmfRefresh_t g_smfRefreshOriginal = nullptr;

using EnsureBuilt_t = int(__fastcall *)(void *node);
using RealizeAnchor_t = void(__thiscall *)(void *anchor, int flag);
using RebuildStringFs_t = void(__fastcall *)(void *fs);

// Force `fs` to resolve its rect and lay out its text node NOW (pre-render),
// mirroring what the engine's pending-layout pass + paint would do later.
// Returns the live text node, or nullptr if it couldn't be built yet (rect
// still unresolved — caller falls back to the lagged pipeline). Uses only the
// engine's own supported force paths (the same Realize/RebuildString/ensure-
// built the Show and paint paths call).
void *ForceBuildFontString(void *fs) {
    // 1. Synchronous rect resolve (Realize flag 0 — the call Show itself uses;
    //    it unlinks the anchor from the pending-layout list and resolves now).
    //    The caller walks lines bottom-up, so this line's relativeTo (the line
    //    below it / the frame) is already resolved when we get here.
    reinterpret_cast<RealizeAnchor_t>(Offsets::FUN_REGION_LAYOUT_REALIZE)(
        reinterpret_cast<uint8_t *>(fs) + Offsets::OFF_REGION_ANCHOR, 0);
    // 2. Rebuild the text block — creates the fresh node (but only if the rect
    //    is now resolved). Internally gated on the fs dirty bit, so a clean
    //    line is a no-op and we avoid depending on reading that bit ourselves.
    reinterpret_cast<RebuildStringFs_t>(Offsets::FUN_FONTSTRING_REBUILD_STRING)(fs);
    void *block = Game::Read<void *>(fs, Offsets::OFF_FONTSTRING_TEXT_BLOCK);
    if (!LooksReadable(block))
        return nullptr; // rect still unresolved — no node created
    void *node = Game::Read<void *>(block, Offsets::OFF_TEXTBLOCK_NODE);
    if (!LooksReadable(node))
        return nullptr;
    // 3. Lay the node out now: runs the glyph emitter (our Emitter_h records the
    //    icons) and finalizes the node origin. Idempotent — paint's own ensure-
    //    built re-runs later and the flush dedups the identical placement.
    reinterpret_cast<EnsureBuilt_t>(Offsets::FUN_TEXT_ENSURE_BUILT)(node);
    return node;
}

// Walk the SMF's visible line array and early-apply each icon-bearing line.
// The C++-object-holding body is kept out of the SEH frame (see
// SafeSmfEarlyApply) — MSVC won't allow __try where objects need unwinding.
void SmfEarlyApplyImpl(void *smf) {
    if (!LooksReadable(smf))
        return;
    const int count = Game::Read<int>(smf, Offsets::OFF_SMF_LINE_COUNT);
    if (count <= 0 || count > 1024)
        return;
    auto *arrayBase = Game::Read<uint8_t *>(smf, Offsets::OFF_SMF_LINE_ARRAY);
    if (!LooksReadable(arrayBase))
        return;
    for (int i = 0; i < count; ++i) {
        uint8_t *entry = arrayBase + i * Offsets::SMF_LINE_STRIDE;
        if (!LooksReadable(entry))
            break;
        void *fs = Game::Read<void *>(entry, Offsets::OFF_SMF_LINE_FONTSTRING);
        if (!LooksReadable(fs))
            continue;
        // Only shown lines (the desired-shown latch the SMF just set) that carry
        // inline markup — a plain chat line costs nothing but the text scan.
        if (Game::Read<uint32_t>(fs, Offsets::OFF_REGION_DESIRED_SHOWN) == 0)
            continue;
        const uint8_t *text = Game::Read<const uint8_t *>(fs, Offsets::OFF_FONTSTRING_TEXT);
        if (!LooksReadable(text))
            continue;
        int len = 0;
        while (len < 0x4000 && text[len] != '\0')
            ++len;
        if (!HasInlineTexture(text, len))
            continue;
        void *node = ForceBuildFontString(fs);
        if (node == nullptr || NodeEditable(node))
            continue; // couldn't build in time, or editable (raw markup)
        std::vector<Text::InlineTexturePool::Placement> places;
        if (ComputePlacementsForNode(node, fs, places))
            Text::InlineTexturePool::ApplyNow(fs, std::move(places));
    }
}

// SEH latch mirroring SafeFlush — a fault disables the early path for the
// session (the baseline paint+tick pipeline still runs) instead of crashing.
void SafeSmfEarlyApply(void *smf) {
    __try {
        SmfEarlyApplyImpl(smf);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_earlyApply = false;
    }
}

void __fastcall SmfRefresh_h(void *smf, void *edx, int newestIdx) {
    g_smfRefreshOriginal(smf, edx, newestIdx);
    if (g_inlineEnabled && g_earlyApply)
        SafeSmfEarlyApply(smf);
}

static const Game::HookAutoRegister _smfRefreshHook{
    Offsets::FUN_SMF_DISPLAY_REFRESH, reinterpret_cast<void *>(&SmfRefresh_h),
    reinterpret_cast<void **>(&g_smfRefreshOriginal)};

// --- Lua control surface ---------------------------------------------------

// _classicapi_InlineTexEnable([on]) -> enabled. The feature kill switch (also
// what the SEH latch trips on a flush fault).
int __fastcall Script_InlineTexEnable(void *L) {
    if (Game::Lua::GetTop(L) == 0)
        g_inlineEnabled = true;
    else
        g_inlineEnabled = Game::Lua::ToBoolean(L, 1) != 0;
    Game::Lua::PushBool(L, g_inlineEnabled);
    return 1;
}

// _classicapi_InlineTexEarly([on]) -> enabled. Toggles the SMF-refresh
// pre-render early apply (the chat-icon-lag fix) independently of the whole
// feature — for A/B'ing the lag against the baseline paint+tick pipeline, which
// keeps running either way. Also what the SMF SEH latch trips on a fault.
int __fastcall Script_InlineTexEarly(void *L) {
    if (Game::Lua::GetTop(L) == 0)
        g_earlyApply = true;
    else
        g_earlyApply = Game::Lua::ToBoolean(L, 1) != 0;
    Game::Lua::PushBool(L, g_earlyApply);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexEnable", &Script_InlineTexEnable);
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexEarly", &Script_InlineTexEarly);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void PrepareForReload() {
    // /reload frees every gxu text node — the per-node maps hold stale node
    // pointers. Forget them (records rebuild as the reloaded UI re-emits its
    // text). K needs no state at all (read live from engine globals).
    g_nodeIcons.clear();
    g_nodeOwner.clear();
    g_nodeBakedColor.clear();
}

static const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

} // namespace Text::InlineTexture
