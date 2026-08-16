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
// (size, pen offset, sprite-sheet texcoord crop, and r:g:b vertex tint). The one
// deliberate gap is MEASURE width: the tokenizer reports an icon as ~zero width,
// so `GetStringWidth`/wrap/hyperlink-hittest undercount it — there is no width
// field in the engine's token contract, and the only fixes are hot per-glyph
// hooks (against the MinHook-collision guidance) or several parallel measure-loop
// hooks for a mostly-cosmetic gain. See docs/InlineTextureEscapes.md for the RE
// map and the full rationale.

#include "text/InlineTexture.h"

#include "Game.h"
#include "Offsets.h"
#include "text/InlineTexturePool.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace Text::InlineTexture {

namespace {

// True if `p` looks like a readable in-process pointer (heap/.data range), so we
// can probe engine structures without faulting on a bad/uninitialized field.
bool LooksReadable(const void *p) {
    auto a = reinterpret_cast<uintptr_t>(p);
    return a >= 0x00010000u && a < 0x7FFF0000u;
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

// Icons keyed by render node. Rebuilt whenever the emitter runs for a node (see
// Emitter_h), so it tracks the node's current text. Entries for freed nodes are
// never dereferenced (flush only walks the layout's live node list) and are
// cleared when the address is reused and rebuilt.
std::unordered_map<void *, std::vector<IconRecord>> g_nodeIcons;

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
    if (g_inlineEnabled && !g_suppressInline && (flags & 0x40u) == 0 && text != nullptr &&
        text[0] == '|' && !TextInReentry(text) && !TextInFocusedEditbox(text)) {
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

    // The draw builder walks a node's wrapped lines by calling the emitter once per
    // line on the SAME node, advancing the text pointer; the first line's pointer
    // equals the node's text start. Clear the icon list once here so wrapped lines
    // ACCUMULATE their icons instead of each wiping the previous.
    const bool firstLine =
        text == *reinterpret_cast<uint8_t **>(reinterpret_cast<uint8_t *>(node) +
                                              Offsets::OFF_TEXT_NODE_TEXT);
    if (firstLine)
        g_nodeIcons.erase(node);

    if (!HasInlineTexture(text, len)) {
        // No inline texture on this line — render it normally without touching the
        // node's icon list (icons from other wrapped lines are preserved).
        g_emitterOriginal(node, edx, text, len, colorState, penXYZ, pageMask, linkState);
        return;
    }

    // This line owns inline textures — append them, rendering the plain runs by
    // delegating to the original per segment.
    std::vector<IconRecord> &icons = g_nodeIcons[node];

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
        float iconW = 0.0f;
        for (int k = 0; k < len;) {
            const int ml = IconStartLen(text, len, k);
            if (ml == 0) {
                ++k;
                continue;
            }
            int cl = 0;
            const size_t ce = FindIconClose(text, len, static_cast<size_t>(k) + ml, ml == 3, &cl);
            if (ce == static_cast<size_t>(-1))
                break;
            IconDesc d;
            const char *pl = reinterpret_cast<const char *>(text) + k + ml;
            if (ParseIcon(pl, ce - (static_cast<size_t>(k) + ml), d)) {
                const float bh = (d.height > 0.0f) ? d.height : fontH;
                const float iw = (d.width > 0.0f ? d.width : bh) * g_sizeScale;
                iconW += iw + 1.5f * (iw * g_iconPadFrac); // include lead + half-trail pad
            }
            k = static_cast<int>(ce) + cl;
        }
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
            // draw position in FlushLayout; a positive offsetX adds extra lead; a
            // negative one (deliberate leftward overlap) doesn't shrink the advance.
            const float pad = w * g_iconPadFrac;
            penX += w + 1.5f * pad + (r.offsetX > 0.0f ? r.offsetX : 0.0f);
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

        auto it = g_nodeIcons.find(node);
        if (it == g_nodeIcons.end() || it->second.empty()) {
            node = next;
            continue;
        }

        // Drop stale records: the chat frame reuses its line FontStrings, and when
        // a line is cleared or reused for markup-free text the draw builder
        // early-returns on it, so the emitter never runs and never erases the old
        // record — drawing it would paint ghost icons at the parked position. So
        // gate on the node's live text still containing `|T`.
        const uint8_t *ntext = *reinterpret_cast<uint8_t *const *>(n + Offsets::OFF_TEXT_NODE_TEXT);
        bool hasMarkup = false;
        if (LooksReadable(ntext)) {
            for (int k = 0; k < 2048 && ntext[k] != '\0'; ++k)
                if (ntext[k] == '|' && ntext[k + 1] == 'T') {
                    hasMarkup = true;
                    break;
                }
        }
        if (!hasMarkup) {
            g_nodeIcons.erase(it);
            node = next;
            continue;
        }

        // Never draw over editable text (flags bit 6) — safety net for records
        // made before the emitter's editable-suppress applied.
        if (!NodeEditable(node)) {
            const float ox = *reinterpret_cast<float *>(n + Offsets::OFF_TEXT_NODE_ORIGIN_X);
            const float oy = *reinterpret_cast<float *>(n + Offsets::OFF_TEXT_NODE_ORIGIN_Y);
            for (const IconRecord &r : it->second) {
                const float cx = r.x + ox + r.offsetX; // screen left
                // Centre on the line: penY sits near the text top, so add a
                // fraction of the font height (plus the fine-tune bias). offsetY
                // shifts up (WoW convention), so subtract it.
                const float cy = r.y + r.fontH * g_centerFrac + oy + g_vBias - r.offsetY;
                DrawTexturedQuad(r.tex, cx, cy - r.h * 0.5f, cx + r.w, cy + r.h * 0.5f, r.u0, r.v0,
                                 r.u1, r.v1, r.color, r.z);
                Text::InlineTexturePool::Hold(r.path.c_str());
            }
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

// _classicapi_InlineTexStats() -> enabled, suppressed, trackedNodes, faultCode.
int __fastcall Script_InlineTexStats(void *L) {
    Game::Lua::PushBool(L, g_inlineEnabled);
    Game::Lua::PushBool(L, g_suppressInline);
    Game::Lua::PushNumber(L, static_cast<double>(g_nodeIcons.size()));
    Game::Lua::PushNumber(L, static_cast<double>(g_faultCode));
    return 4;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexEnable", &Script_InlineTexEnable);
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexSuppress", &Script_InlineTexSuppress);
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexTune", &Script_InlineTexTune);
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexStats", &Script_InlineTexStats);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void PrepareForReload() {
    // /reload frees every gxu text node — g_nodeIcons holds stale node pointers.
    // Forget them (records rebuild as the reloaded UI re-emits its text). The
    // texture-handle cache stays valid (engine keeps its by-name cache).
    g_nodeIcons.clear();
}

} // namespace Text::InlineTexture
