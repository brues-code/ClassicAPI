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
//   1. The rendering PRIMITIVE (LoadTextureByPath + DrawTexturedQuad) — a path
//      string becomes a bound, resident, coloured textured quad drawn through
//      the same GxU dynamic-VB path the glyph paint pass uses. See the "SOLVED"
//      section in docs/InlineTextureEscapes.md for the full recipe (the key was
//      FUN_0044acf0 for residency + correct bind).
//   2. POSITIONING — co-hook the per-line glyph emitter (FUN_005ccbe0) and, for
//      a line containing `|T…|t`, render the plain text runs by DELEGATING to
//      the original emitter per segment (threading the pen through linkState[4])
//      while recording an icon quad at the pen between runs. The recorded icons
//      are flushed in the paint co-hook (FUN_005c8fe0), translated by each
//      render node's screen origin — the same transform the paint pass applies
//      to glyph batches. This avoids reimplementing the emitter's intricate
//      vertex math: the engine still draws every glyph; we only insert icons.
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

#include "Game.h"
#include "Offsets.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace Text::InlineTexture {

namespace {

// The UI text vertex the GxU path expects (stride 0x18 = 24 bytes), verified
// from the glyph vertex assembler FUN_005c8710's store loop:
//   +0x00 x  +0x04 y  +0x08 z  +0x0c colorBGRA  +0x10 u  +0x14 v
struct Vertex {
    float x;
    float y;
    float z;
    uint32_t color;
    float u;
    float v;
};
static_assert(sizeof(Vertex) == 0x18, "text vertex must be 24 bytes");

// True if `p` looks like a readable in-process pointer (heap/.data range), so
// we can probe engine structures without faulting on a bad/uninitialized field.
bool LooksReadable(const void *p) {
    auto a = reinterpret_cast<uintptr_t>(p);
    return a >= 0x00010000u && a < 0x7FFF0000u;
}

// --- texture-by-path load --------------------------------------------------

// Byte-identical to the 5-dword on-stack descriptor FUN_00770200 builds before
// calling FUN_00449d90. On a successful load the loader never touches it; only
// the load-failure log path dereferences it, so a faithful copy keeps the
// failure path (missing texture) as safe as the engine's own call.
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

// Resolves a texture PATH string to a bindable CGxTexture handle, caching by
// path. Mirrors FUN_00770200's load exactly: build the flags via
// FUN_0058a980, then FUN_00449d90(path, &desc, flags, 0, 1). The engine keeps
// its own by-name texture cache alive (refcounted), so the returned handle is
// stable for the session's UI usage.
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
    auto texFlagsInit = reinterpret_cast<TexFlagsInit_t>(Offsets::FUN_GX_TEXFLAGS_INIT);
    texFlagsInit(&flags, blend, 0, 0, 0, 0, 0, 1, 0);

    auto loader = reinterpret_cast<TextureLoad_t>(Offsets::FUN_TEXTURE_LOAD_BY_PATH);
    void *handle = reinterpret_cast<void *>(loader(path, &desc, flags, 0, 1));

    g_texCache.emplace(std::move(key), handle);
    return handle;
}

// --- textured-quad draw ----------------------------------------------------

using GxBind_t = void(__fastcall *)(int selector, void *tex);
using GxLockVB_t = int(__fastcall *)(int zero, int stride, int vertCount);
using GxVBData_t = void *(__fastcall *)(int handle);
using GxSubmit_t = void *(__fastcall *)(int *handle, int vertCount);
using GxUnlock_t = void(__fastcall *)(int handle, int zero);

// The dynamic VB is a fixed ring the paint pass always locks at 0x800 verts;
// we mirror that reservation exactly (writing only the 4 we need).
constexpr int kRingVerts = 0x800;

// Draws one textured quad through the UI text VB primitive. Must run while the
// device is in the text-paint state (i.e. from the paint co-hook, after the
// original glyph flush). Corner order TL, TR, BL, BR matches the shared quad
// index buffer ({0,1,2, 2,1,3}) and the device cull winding.
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
        // drives the streaming load. Calling it every frame is the live
        // reference that makes the texture resident. Binding the raw HTEXTURE
        // instead is SetTexture(0) = white.
        auto getRenderable = reinterpret_cast<void *(__fastcall *)(void *, int, void *)>(
            Offsets::FUN_TEXTURE_GET_RENDERABLE);
        void *cgxTex = getRenderable(tex, 1, nullptr);
        if (!LooksReadable(cgxTex)) {
            auto unlockEarly = reinterpret_cast<GxUnlock_t>(Offsets::FUN_GX_UNLOCK_VB);
            unlockEarly(handle, 0);
            return;
        }
        auto bind = reinterpret_cast<GxBind_t>(Offsets::FUN_GX_BIND_TEXTURE);
        bind(Offsets::GX_TEXTURE_SELECTOR, cgxTex);

        // The UI device backend is OpenGL (bottom-left texture origin, v=0 at
        // the bottom), so the top screen row maps to the LARGER v and the bottom
        // to the smaller v — otherwise the texture renders vertically flipped
        // (invisible on symmetric icons, obvious on directional ones like the
        // raid-target markers). Map top corners to v1, bottom corners to v0.
        v[0] = {x0, y0, z, color, u0, v1};
        v[1] = {x1, y0, z, color, u1, v1};
        v[2] = {x0, y1, z, color, u0, v0};
        v[3] = {x1, y1, z, color, u1, v0};

        auto submit = reinterpret_cast<GxSubmit_t>(Offsets::FUN_GX_SUBMIT_VB);
        submit(&handle, 4);
    }

    auto unlock = reinterpret_cast<GxUnlock_t>(Offsets::FUN_GX_UNLOCK_VB);
    unlock(handle, 0);
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

// Reads a numeric field starting at `s` (bounded by `end`), stopping at the
// next ':' or the end. Returns the parsed value; sets `next` past the field's
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
    if (nf < 1 || f[0] <= 0.0f)
        return false; // height required
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
    void *tex;  // bindable HTEXTURE (from LoadTextureByPath)
    float x;    // node-local left
    float y;    // node-local pen reference (penXYZ[1]) — near the text top
    float fontH; // font pixel height of the line, for vertical centering
    float w;
    float h;
    float z;
    float offsetX, offsetY;      // pen-relative pixel shift
    float u0, v0, u1, v1;        // texture crop
    uint32_t color;              // vertex tint (0xAARRGGBB; white = untinted)
};

// Icons keyed by render node. Rebuilt whenever the emitter runs for a node
// (see Emitter_h), so it tracks the node's current text. Entries for freed
// nodes are never dereferenced (flush only walks the layout's live node list)
// and are cleared when the address is reused and rebuilt.
std::unordered_map<void *, std::vector<IconRecord>> g_nodeIcons;

// Runtime toggle (default ON — the feature is proven; `_classicapi_InlineTexEnable(false)`
// still turns it off). When off, the emitter/tokenizer co-hooks fast-path
// straight to the originals.
bool g_inlineEnabled = true;
// Manual suppression override (Lua _classicapi_InlineTexSuppress) — normally
// unused; the automatic editbox-focus check below covers the real case.
bool g_suppressInline = false;

// True while an EditBox has keyboard focus (the engine's cursor global is set).
// We suppress icon RECORDING + measure intervention (but NOT the flush of
// already-recorded display icons) while input is active, so a focused input
// field shows raw, editable `|T…|t` markup while cached display text keeps its
// icons. This is the engine's own focus signal — the one that drives the text
// cursor — so it needs no per-node guessing or Lua enumeration.
inline bool InputFocused() {
    return *reinterpret_cast<void *const *>(Offsets::VAR_FOCUSED_EDITBOX) != nullptr;
}
inline bool Suppressed() { return g_suppressInline || InputFocused(); }

// A text node's flags (`[node+0x5c]`) bit 6 (0x40) distinguishes editable input
// text (set on the macro editbox: flags 0x4D) from display text (chat 0x20D,
// FontStrings 0x0D — bit 6 clear). It's a per-node property the emitter and
// tokenizer don't otherwise use, and it rides in the tokenizer's flags
// argument, so we can suppress inline rendering per node — covering editboxes
// the focus global misses (multi-line editors build once, un-focused). This
// mirrors 4.3.4's per-render texture-disable flag, adapted to 1.12's layout.
inline bool NodeEditable(const void *node) {
    return (*reinterpret_cast<const uint32_t *>(reinterpret_cast<const uint8_t *>(node) +
                                                Offsets::OFF_TEXT_NODE_FLAGS) &
            0x40u) != 0;
}
float g_vBias = 0.0f;     // extra node-local Y added to the icon centre (fine-tune)
float g_sizeScale = 1.0f; // multiplies the parsed icon size
// Icon vertical centre = penY + fontHeight * centerFrac. penY sits near the
// text top, so ~0.5 centres the icon on the line regardless of font size.
float g_centerFrac = 0.5f;

// --- diagnostics (temporary; strip once the path is proven) ----------------
// Localize where the pipeline breaks: does the emitter fire for the text under
// test, does it detect `|T`, and what are the node flags (bit 3 gates whether
// segmented delegation is safe).
volatile int g_emCalls = 0;   // Emitter_h invocations while enabled
volatile int g_emFound = 0;   // …where the line contains an inline `|T`
volatile int g_emSegmented = 0; // …where we actually took the segmented path
volatile int g_emPipeCalls = 0; // …whose text contains ANY '|' (colour codes etc.)
uint32_t g_lastFlags = 0;     // [node+0x5c] of the last `|T`-containing line
int g_lastLen = 0;            // its length
volatile int g_flushNodesSeen = 0; // nodes the flush walked (last paint)
volatile int g_lastIconN = 0; // icons recorded on the last segmented line
volatile int g_lastFirstLine = 0; // was that line the first wrapped line?
uint32_t g_lastVtable = 0;    // [node+0] vtable of the last `|T` node
volatile int g_lastFocused = 0; // focus global set at last `|T` build?
volatile int g_lastMono = 0;    // monochrome flag set at last `|T` build?

// Marker capture — grabs the text of the first emitter call whose text starts
// with '~', so a prefixed test string proves whether it reaches FUN_005ccbe0.
char g_capBuf[96] = {0};
volatile bool g_capLatched = false;
int g_capLen = 0;

// The 1.12 FontString text sanitizer DOUBLES any pipe that doesn't begin a
// recognized escape (so it renders as a literal `|`). Since 1.12 doesn't know
// `|T`, a caller's `|Tpath:h|t` arrives at the emitter as `||Tpath:h||t`
// (verified in-game: emitter text length +2, pipes doubled). So we accept both
// the doubled form (the real-world case) and a clean `|T` (should one ever
// reach us undoubled, e.g. after a future sanitizer hook). A literal user
// `||T…||t` is effectively nonexistent, and ordinary doubled pipes (`|| `)
// never match because the char after must be `T`.

// If an inline-texture escape starts at text[i], returns its opening-marker
// length: 3 for the doubled `||T`, 2 for a clean `|T`; 0 otherwise. The doubled
// form is checked first so the leading `|` of `||T` wins over reading the 2nd
// `|` as a clean `|T`.
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
    // Only intervene at a pipe when enabled, not suppressed, and not editable
    // text (flags bit 0x40) — editboxes measure/wrap the raw markup literally.
    if (g_inlineEnabled && !Suppressed() && (flags & 0x40u) == 0 && text != nullptr &&
        text[0] == '|') {
        const int span = InlineSpanLen(text);
        if (span > 0) {
            // Consume the whole escape as one glyph-type token with a payload
            // that resolves to no glyph — so every measure/wrap caller advances
            // past the path text and counts it as near-zero width instead of
            // ~40 literal characters. The emitter detects icons itself and is
            // unaffected (it only ever delegates plain, `|T`-free segments).
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
// colorState, penXYZ, pageMask, linkState); declared __fastcall with a dummy
// edx (the established pattern for co-hooking __thiscall engine methods).
using Emitter_t = void(__fastcall *)(void *node, void *edx, uint8_t *text, int len,
                                     uint32_t *colorState, float *penXYZ, uint32_t *pageMask,
                                     int *linkState);
Emitter_t g_emitterOriginal = nullptr;

void __fastcall Emitter_h(void *node, void *edx, uint8_t *text, int len, uint32_t *colorState,
                          float *penXYZ, uint32_t *pageMask, int *linkState) {
    // Fast path — feature off: delegate verbatim, touching nothing. Keeps the
    // hot text-render path a single bool check when disabled.
    if (!g_inlineEnabled) {
        g_emitterOriginal(node, edx, text, len, colorState, penXYZ, pageMask, linkState);
        return;
    }

    // Suppressed — render the node's text verbatim and drop any icons
    // previously recorded for it, so editable input shows raw markup. Covers a
    // focused editbox (focus global) and any editable node (flags bit 6),
    // including un-focused multi-line editors. Display nodes that don't rebuild
    // keep their icons (the flush is unaffected).
    if (Suppressed() || (node != nullptr && NodeEditable(node))) {
        if (node != nullptr)
            g_nodeIcons.erase(node);
        g_emitterOriginal(node, edx, text, len, colorState, penXYZ, pageMask, linkState);
        return;
    }

    ++g_emCalls;
    if (text != nullptr && len > 0) {
        bool pipe = false;
        for (int k = 0; k < len; ++k) {
            if (text[k] == '|') {
                pipe = true;
                break;
            }
        }
        if (pipe)
            ++g_emPipeCalls;
        if (!g_capLatched && text[0] == 0x7E /* '~' */) {
            int n = len < 95 ? len : 95;
            for (int k = 0; k < n; ++k)
                g_capBuf[k] = static_cast<char>(text[k]);
            g_capBuf[n] = '\0';
            g_capLen = len;
            g_capLatched = true;
        }
    }
    const bool hasSpan =
        node != nullptr && text != nullptr && len > 0 && HasInlineTexture(text, len);
    if (hasSpan) {
        ++g_emFound;
        g_lastFlags =
            *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(node) +
                                          Offsets::OFF_TEXT_NODE_FLAGS);
        g_lastLen = len;
        g_lastVtable = *reinterpret_cast<uint32_t *>(node);
        g_lastFocused = InputFocused() ? 1 : 0;
        g_lastMono = NodeEditable(node) ? 1 : 0; // editable-flag (bit 6) at capture
    }

    if (node == nullptr || text == nullptr || len <= 0) {
        g_emitterOriginal(node, edx, text, len, colorState, penXYZ, pageMask, linkState);
        return;
    }

    // The draw builder walks a node's wrapped lines by calling the emitter once
    // per line on the SAME node, advancing the text pointer; the first line's
    // pointer equals the node's text start. Clear the icon list once here so
    // wrapped lines ACCUMULATE their icons instead of each wiping the previous.
    const bool firstLine =
        text == *reinterpret_cast<uint8_t **>(reinterpret_cast<uint8_t *>(node) +
                                              Offsets::OFF_TEXT_NODE_TEXT);
    if (firstLine)
        g_nodeIcons.erase(node);

    if (!hasSpan) {
        // No inline texture on this line — render it normally without touching
        // the node's icon list (icons from other wrapped lines are preserved).
        g_emitterOriginal(node, edx, text, len, colorState, penXYZ, pageMask, linkState);
        return;
    }

    // This line owns inline textures — append them, rendering the plain runs by
    // delegating to the original per segment.
    ++g_emSegmented;
    std::vector<IconRecord> &icons = g_nodeIcons[node];

    // Bit 3 of the node flags gates the emitter's per-call batch-clear. When
    // set (the standalone-FontString case), each original call would wipe the
    // page batches, so segmenting would lose all but the last run. Handle it by
    // doing the batch-clear ONCE per build (a len-0 original call with bit 3
    // still set, only on the first wrapped line), then clearing bit 3 so our
    // per-segment calls APPEND. Restore the flags before returning. Bit 5
    // (shadow) is not set for these nodes, so the only extra work the cleared
    // bit enables is a harmless colour-array append.
    uint32_t *const flagsPtr = reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(node) +
                                                            Offsets::OFF_TEXT_NODE_FLAGS);
    const uint32_t savedFlags = *flagsPtr;
    const bool batchClearMode = (savedFlags & 8u) != 0;
    if (batchClearMode) {
        if (firstLine)
            g_emitterOriginal(node, edx, text, 0, colorState, penXYZ, pageMask, linkState);
        *flagsPtr = savedFlags & ~8u;
    }

    // Font pixel height of this line — used to centre icons vertically (penY
    // sits near the text top). Mirrors the emitter's own call: ecx =
    // (nodeFlags>>7)&1, stack = the node's font size [node+0x1c].
    const int fontFlag = static_cast<int>((savedFlags >> 7) & 1u);
    const float fontSize =
        *reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(node) +
                                   Offsets::OFF_TEXT_NODE_FONT_SIZE);
    const float fontH = reinterpret_cast<float(__fastcall *)(int, float)>(
        Offsets::FUN_TEXT_FONT_HEIGHT)(fontFlag, fontSize);

    // The original emitter never writes penXYZ[0]; we mutate it to thread the
    // pen across segments, so snapshot and restore it. The draw builder does
    // NOT reset penXYZ[0] between left-justified lines, so leaving it mutated
    // would cascade-shift every following line.
    const float startX = penXYZ[0];
    const float penY = penXYZ[1];
    const float penZ = penXYZ[2];
    float penX = penXYZ[0];

    // Draws a plain run [start,start+n) via the original emitter, threading the
    // pen: the original starts at penXYZ[0] and leaves the final node-local pen
    // x (truncated to int) in linkState[4]. n == 0 is a safe no-op that just
    // finalizes linkState (used to keep the pen/link state consistent between
    // and after icons).
    auto drawRun = [&](size_t start, int n) {
        penXYZ[0] = penX;
        g_emitterOriginal(node, edx, text + start, n, colorState, penXYZ, pageMask, linkState);
        // linkState[4] holds the final pen x, stored by the engine with `FSTP
        // dword` — i.e. a FLOAT bit pattern, not an int. Read it as a float
        // (an int-cast reinterprets the bits and yields garbage).
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
            void *tex = LoadTextureByPath(d.path.c_str());
            if (tex != nullptr) {
                float w = d.width * g_sizeScale;
                float h = d.height * g_sizeScale;
                IconRecord r;
                r.tex = tex;
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
                icons.push_back(r);
                penX += w; // reserve horizontal space for the icon
            }
        }
        penXYZ[0] = penX;
        i = static_cast<int>(close) + closeLen; // skip past the closing marker
        runStart = static_cast<size_t>(i);
    }

    // Trailing plain run (also finalizes pen/link state for the builder).
    drawRun(runStart, len - static_cast<int>(runStart));

    g_lastIconN = static_cast<int>(icons.size());
    g_lastFirstLine = firstLine ? 1 : 0;

    // Restore the pen origin to match the engine's own post-call invariant, and
    // the node flags (bit 3) we cleared for the segmented append.
    penXYZ[0] = startX;
    if (batchClearMode)
        *flagsPtr = savedFlags;
}

static const Game::HookAutoRegister _emitterHook{Offsets::FUN_TEXT_EMITTER,
                                                 reinterpret_cast<void *>(&Emitter_h),
                                                 reinterpret_cast<void **>(&g_emitterOriginal)};

// --- paint co-hook: flush recorded icons -----------------------------------

using Paint_t = void(__fastcall *)(void *layout);
Paint_t g_paintOriginal = nullptr;

// Draws all icons recorded for the layout's live render nodes, translating each
// from node-local coords by the node's screen origin (+0x70/+0x74) — the same
// transform the paint pass applies to glyph batches. Runs after the original
// paint so icons land over the reserved blank space, in the correct device
// state. SEH-guarded: a fault here (bad node/texture) disables the feature
// rather than crashing the client.
int CaptureFault(EXCEPTION_POINTERS *ep, uint32_t *code, uintptr_t *addr) {
    *code = ep->ExceptionRecord->ExceptionCode;
    *addr = reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}

uint32_t g_faultCode = 0;
uintptr_t g_faultAddr = 0;
volatile int g_iconsDrawn = 0;

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
        ++g_flushNodesSeen;
        auto *n = reinterpret_cast<uint8_t *>(node);
        auto it = g_nodeIcons.find(node);
        // Never draw icons over editable text (flags bit 6) — safety net for any
        // records made before the emitter's editable-suppress applied.
        if (it != g_nodeIcons.end() && !it->second.empty() && !NodeEditable(node)) {
            const float ox = *reinterpret_cast<float *>(n + Offsets::OFF_TEXT_NODE_ORIGIN_X);
            const float oy = *reinterpret_cast<float *>(n + Offsets::OFF_TEXT_NODE_ORIGIN_Y);
            for (const IconRecord &r : it->second) {
                const float cx = r.x + ox + r.offsetX; // screen left
                // Centre on the line: penY is near the text top, so add a
                // fraction of the font height (plus the fine-tune bias). offsetY
                // shifts up (WoW convention), so subtract it.
                const float cy = r.y + r.fontH * g_centerFrac + oy + g_vBias - r.offsetY;
                const float x0 = cx;
                const float x1 = cx + r.w;
                const float y0 = cy - r.h * 0.5f;
                const float y1 = cy + r.h * 0.5f;
                DrawTexturedQuad(r.tex, x0, y0, x1, y1, r.u0, r.v0, r.u1, r.v1, r.color, r.z);
                ++g_iconsDrawn;
            }
        }
        node = *reinterpret_cast<void **>(n + linkOff + 4);
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
// Toggles the inline-texture feature. No arg / truthy = on; false / 0 = off.
int __fastcall Script_InlineTexEnable(void *L) {
    if (Game::Lua::GetTop(L) == 0)
        g_inlineEnabled = true;
    else
        g_inlineEnabled = Game::Lua::ToBoolean(L, 1) != 0;
    Game::Lua::PushBool(L, g_inlineEnabled);
    return 1;
}

// _classicapi_InlineTexSuppress([on]) -> suppressed
// Temporarily suppress icon rendering in newly-built text (used while an editbox
// has focus so input shows raw markup). Already-drawn display icons are
// unaffected. No arg / truthy = suppress; false / 0 = resume.
int __fastcall Script_InlineTexSuppress(void *L) {
    if (Game::Lua::GetTop(L) == 0)
        g_suppressInline = true;
    else
        g_suppressInline = Game::Lua::ToBoolean(L, 1) != 0;
    Game::Lua::PushBool(L, g_suppressInline);
    return 1;
}

// _classicapi_InlineTexTune(vBias, sizeScale, centerFrac) -> vBias, sizeScale,
// centerFrac. Live calibration: vBias is a fine node-local Y nudge, sizeScale
// multiplies the icon size, centerFrac is the fraction of the font height added
// to penY to centre the icon (default 0.5). All optional; omitted args keep
// their current value.
int __fastcall Script_InlineTexTune(void *L) {
    g_vBias = static_cast<float>(Arg(L, 1, g_vBias));
    g_sizeScale = static_cast<float>(Arg(L, 2, g_sizeScale));
    g_centerFrac = static_cast<float>(Arg(L, 3, g_centerFrac));
    Game::Lua::PushNumber(L, g_vBias);
    Game::Lua::PushNumber(L, g_sizeScale);
    Game::Lua::PushNumber(L, g_centerFrac);
    return 3;
}

// _classicapi_InlineTexStats() -> enabled, emCalls, emFound, emSegmented,
// lastFlags, lastLen, trackedNodes, iconsDrawn, flushNodesSeen, faultCode.
// Ground truth on where the pipeline breaks:
//   emCalls == 0        -> the emitter (FUN_005ccbe0) never fires for the text
//   emFound == 0        -> it fires but never sees `|T` (wrong text/path)
//   lastFlags & 8       -> node is in batch-clear mode; segmentation bails
//   emSegmented > 0 but trackedNodes == 0 -> recording/parse/load failed
//   trackedNodes > 0 but iconsDrawn == 0  -> flush isn't matching the nodes
int __fastcall Script_InlineTexStats(void *L) {
    Game::Lua::PushBool(L, g_inlineEnabled);
    Game::Lua::PushNumber(L, static_cast<double>(g_emCalls));
    Game::Lua::PushNumber(L, static_cast<double>(g_emFound));
    Game::Lua::PushNumber(L, static_cast<double>(g_emPipeCalls));
    Game::Lua::PushNumber(L, static_cast<double>(g_emSegmented));
    Game::Lua::PushNumber(L, static_cast<double>(g_lastFlags));
    Game::Lua::PushNumber(L, static_cast<double>(g_nodeIcons.size()));
    Game::Lua::PushNumber(L, static_cast<double>(g_iconsDrawn));
    Game::Lua::PushNumber(L, static_cast<double>(g_lastIconN));
    Game::Lua::PushNumber(L, static_cast<double>(g_lastFocused));
    Game::Lua::PushNumber(L, static_cast<double>(g_lastMono));
    return 11;
}

// _classicapi_InlineTexCap() -> latched, capturedLength, capturedText
// Returns the text of the first emitter call whose text began with '~'. If
// latched is false, no emitter call ever received text starting with '~' — i.e.
// a '~'-prefixed FontString does NOT flow through FUN_005ccbe0.
int __fastcall Script_InlineTexCap(void *L) {
    Game::Lua::PushBool(L, g_capLatched);
    Game::Lua::PushNumber(L, static_cast<double>(g_capLen));
    Game::Lua::PushString(L, g_capBuf);
    return 3;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexEnable", &Script_InlineTexEnable);
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexSuppress", &Script_InlineTexSuppress);
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexTune", &Script_InlineTexTune);
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexStats", &Script_InlineTexStats);
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexCap", &Script_InlineTexCap);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Text::InlineTexture
