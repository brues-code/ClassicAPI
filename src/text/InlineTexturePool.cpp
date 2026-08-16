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

// Texture-residency holders for inline-|T icons — see the header. One managed
// CSimpleTexture per texture path, drawn every frame, so the engine keeps that
// texture resident for the raw quads that render the actual (pixel-positioned)
// icons.
//
// A CSimpleTexture renders as a gxu text node whose page texture IS the
// region's CGxTex: the layout paint (FUN_005c8fe0) binds it (GxRsSet 0x17) and
// submits its quad EVERY FRAME the region is shown — that per-frame bind+draw
// is the residency reference. Two hard-won constraints on the holder config:
//
//   • ALPHA MUST BE OPAQUE. The engine's effective alpha is the integer
//     product (parentAlphaByte * regionAlphaByte) / 255 (see the CSimpleTexture
//     color update FUN_00772180) — a near-zero region alpha truncates to 0 and
//     the holder goes inert (never batched → never bound → no residency). The
//     alpha-1/255 "invisible" holder shipped in baf1ccc regressed the chat
//     flicker exactly this way.
//   • Invisibility comes from GEOMETRY instead: the quad hangs almost entirely
//     off the bottom-left screen edge with a sub-pixel sliver on-screen. The
//     region screen cull (FUN_00772e00) only rejects rects fully outside
//     [0..1]², so the sliver keeps the node batched and its texture bound, but
//     no pixel center is covered so nothing rasterizes.

#include "text/InlineTexturePool.h"

#include "Game.h"
#include "Offsets.h"
#include "text/InlineTexture.h"
#include "tick/WorldTick.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Text::InlineTexturePool {

namespace {

using PoolAlloc_t = void *(__thiscall *)(void *pool, int zeroInit, const char *tag, int line);
using TexCtor_t = void *(__thiscall *)(void *mem, void *parent, int layer, int sublayer);
using SetTexture_t = uint32_t(__thiscall *)(void *tex, const char *path, int a, uint32_t blend,
                                            int b);
using SetColor_t = void(__thiscall *)(void *tex, const void *colorBGRA);
using SetPoint_t = void(__thiscall *)(void *anchor, int point, void *relAnchor, int relPoint,
                                      float x, float y, int flag);
using Realize_t = void(__thiscall *)(void *anchor, int flag);
using ShowHide_t = void(__fastcall *)(void *tex);

bool LooksReadable(const void *p) {
    auto a = reinterpret_cast<uintptr_t>(p);
    return a >= 0x00010000u && a < 0x7FFF0000u;
}

using SetTexCoord_t = void(__thiscall *)(void *tex, const float *coords4);
using SetParentAndLayer_t = void(__thiscall *)(void *region, void *parentFrame, int layer,
                                               int show);

void *g_uiParent = nullptr;
// path → holder texture (one per distinct path, created once).
std::unordered_map<std::string, void *> g_holders;
// Paths recorded by the paint hook, awaiting holder creation on the next tick.
std::unordered_set<std::string> g_pending;

// --- region-mode placement state ---------------------------------------------

// A pooled icon region and the config it currently carries (for dedup).
struct IconRegion {
    void *tex = nullptr;
    void *parent = nullptr;
    std::string path;
    // Accumulated offset-scale correction, converged from rect readback (see
    // the verification pass in Maintain). Starts at 1; survives reuse (the
    // resolver scale is a property of the parent chain, not the placement).
    float corr = 1.0f;
};

// Keyed by FONTSTRING (stable across SetText — text nodes are reallocated per
// rebuild, so a node key would orphan regions every chat scroll step: the
// "wall of icons" bug).
struct FsPlacements {
    std::vector<Placement> want; // queued by the paint hook
    bool dirty = false;
    std::vector<IconRegion> regions; // engine regions currently serving this fs
    int shown = 0;                   // how many of `regions` are visible
    // Maintain-tick of the last QueuePlacements touch (deduped ones included).
    // A LIVE painted icon line is re-queued every paint; a parked/orphaned fs
    // (recycled chat line whose node never rebuilds — the "ghost icons at the
    // parked position" class) stops being touched entirely, so its regions
    // expire via the freshness check in Maintain.
    uint32_t lastTouchTick = 0;
};

std::unordered_map<void *, FsPlacements> g_fsIcons;
uint32_t g_maintainTick = 0;

// Holder geometry/alpha, live-tunable via _classicapi_InlineTexHolderCfg for
// in-game verification. Units are the region anchor space ([0..1] across the
// screen — see the cull constants at 0x7FFD74/0x7FF9D8).
float g_cfgVisible = 0.0001f; // on-screen sliver (~0.1–0.4px; sub-pixel-center)
float g_cfgSize = 0.016f;     // quad edge (~16px at 1024 — comfortably real geometry)
uint32_t g_cfgAlpha = 0xFFu;  // OPAQUE — load-bearing, see the header comment

void Show(void *tex) {
    // Show no-ops unless the desired-shown flag (+0xC4) is set; engine callers
    // always write it first (see the region RE notes).
    *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(tex) +
                                  Offsets::OFF_REGION_DESIRED_SHOWN) = 1;
    reinterpret_cast<ShowHide_t>(Offsets::FUN_FONTSTRING_SHOW)(tex);
}

void Hide(void *tex) {
    *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(tex) +
                                  Offsets::OFF_REGION_DESIRED_SHOWN) = 0;
    reinterpret_cast<ShowHide_t>(Offsets::FUN_FONTSTRING_HIDE)(tex);
}

void *ResolveUIParent() {
    void *L = Game::Lua::State();
    if (!LooksReadable(L))
        return nullptr;
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, "UIParent");
    Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
    void *frame = Game::Lua::ResolveObject(L, -1);
    Game::Lua::SetTop(L, top);
    return frame;
}

// Applies the current color + geometry config to a holder: opaque white tint
// and a g_cfgSize quad whose top-right corner pokes g_cfgVisible units past the
// screen's bottom-left corner.
void ConfigureHolder(void *tex) {
    const uint32_t color = (g_cfgAlpha << 24) | 0x00FFFFFFu;
    reinterpret_cast<SetColor_t>(Offsets::FUN_FONTSTRING_SET_COLOR)(tex, &color);

    auto setPoint = reinterpret_cast<SetPoint_t>(Offsets::FUN_REGION_SET_POINT);
    void *texAnchor = reinterpret_cast<uint8_t *>(tex) + Offsets::OFF_REGION_ANCHOR;
    void *uiAnchor = reinterpret_cast<uint8_t *>(g_uiParent) + Offsets::OFF_REGION_ANCHOR;
    const float lo = g_cfgVisible - g_cfgSize;
    setPoint(texAnchor, Offsets::FRAMEPOINT_BOTTOMLEFT, uiAnchor, Offsets::FRAMEPOINT_BOTTOMLEFT,
             lo, lo, 1);
    setPoint(texAnchor, Offsets::FRAMEPOINT_TOPRIGHT, uiAnchor, Offsets::FRAMEPOINT_BOTTOMLEFT,
             g_cfgVisible, g_cfgVisible, 1);
    reinterpret_cast<Realize_t>(Offsets::FUN_REGION_LAYOUT_REALIZE)(texAnchor, 0);
}

// Creates the managed CSimpleTexture holder for `path`, configured per the
// invisible-by-geometry scheme above, and never re-touched afterwards.
void *CreateHolder(const char *path) {
    auto alloc = reinterpret_cast<PoolAlloc_t>(Offsets::FUN_REGION_POOL_ALLOC);
    void *mem = alloc(reinterpret_cast<void *>(Offsets::VAR_SIMPLETEXTURE_POOL), 0,
                      reinterpret_cast<const char *>(Offsets::VAR_SIMPLETEXTURE_CLASS_TAG), -2);
    if (mem == nullptr)
        return nullptr;
    auto ctor = reinterpret_cast<TexCtor_t>(Offsets::FUN_SIMPLETEXTURE_CTOR);
    void *tex = ctor(mem, g_uiParent, Offsets::DRAWLAYER_ARTWORK, 1);
    if (tex == nullptr)
        return nullptr;

    const uint32_t blend = *reinterpret_cast<uint32_t *>(Offsets::VAR_TEXTURE_BLEND_DEFAULT);
    reinterpret_cast<SetTexture_t>(Offsets::FUN_SIMPLETEXTURE_SET_TEXTURE)(tex, path, 0, blend, 0);
    ConfigureHolder(tex);
    Show(tex);
    return tex;
}

// Re-applies the live config to every holder (after a _classicapi_InlineTexHolderCfg
// change). The Hide/Show cycle forces the region relink + parent-layer dirty so
// the new geometry/color rebuilds into the batch.
void Reconfigure() {
    for (auto &kv : g_holders) {
        ConfigureHolder(kv.second);
        Hide(kv.second);
        Show(kv.second);
    }
}

// Creates a bare pooled CSimpleTexture parented to `parent` (ARTWORK).
void *CreateRegion(void *parent) {
    auto alloc = reinterpret_cast<PoolAlloc_t>(Offsets::FUN_REGION_POOL_ALLOC);
    void *mem = alloc(reinterpret_cast<void *>(Offsets::VAR_SIMPLETEXTURE_POOL), 0,
                      reinterpret_cast<const char *>(Offsets::VAR_SIMPLETEXTURE_CLASS_TAG), -2);
    if (mem == nullptr)
        return nullptr;
    auto ctor = reinterpret_cast<TexCtor_t>(Offsets::FUN_SIMPLETEXTURE_CTOR);
    return ctor(mem, parent, Offsets::DRAWLAYER_ARTWORK, 1);
}

// Applies one placement to one region: texture, texcoords, color, and the two
// anchor points expressed relative to the owning fontstring's BOTTOMLEFT (so the
// engine moves the icon with its line for free).
void ApplyPlacement(IconRegion &r, void *fs, const Placement &p) {
    auto *f = reinterpret_cast<uint8_t *>(fs);
    // Placement coords are FS-RELATIVE (from the fs rect's min corner), computed
    // at PAINT time in the same flush as the icon coords. Do NOT read the fs
    // rect here: an apply-time read raced the chat relayout (SetText briefly
    // invalidates the rect) and parked icons off their line, where the dedup
    // then froze them — the scroll-landing "randomly hidden icon" bug.

    if (r.path != p.path) {
        const uint32_t blend = *reinterpret_cast<uint32_t *>(Offsets::VAR_TEXTURE_BLEND_DEFAULT);
        reinterpret_cast<SetTexture_t>(Offsets::FUN_SIMPLETEXTURE_SET_TEXTURE)(r.tex, p.path.c_str(),
                                                                               0, blend, 0);
        r.path = p.path;
    }
    // The engine's 4-float texcoord rect is Y-FIRST INTERLEAVED — {v0, u0, v1,
    // u1} — matching its {yA, left, yB, right} rect convention (diagnosed
    // in-game: {u0,v0,u1,v1} rendered the raid-mark moon crop as the circle
    // cell). v is top-down, same as the |T payload's top/bottom fields.
    const float coords[4] = {p.v0, p.u0, p.v1, p.u1};
    reinterpret_cast<SetTexCoord_t>(Offsets::FUN_SIMPLETEXTURE_SET_TEXCOORD)(r.tex, coords);
    reinterpret_cast<SetColor_t>(Offsets::FUN_FONTSTRING_SET_COLOR)(r.tex, &p.color);

    // Placement coords are in RESOLVED-rect (anchor) units, but anchor
    // resolution multiplies STORED SetPoint offsets by the positioned region's
    // effective scale chain (+0x7C — uiScale × per-frame scales). Verified from
    // the probe: a 50px SetPoint offset stored as 0.0319 internal resolved to a
    // rect at 0.0290 = 0.0319 × s (s = 0.91). So divide the desired resolved
    // delta by the region's own scale before storing.
    float sReg = *reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(r.tex) +
                                            Offsets::OFF_LAYOUT_SCALE);
    if (!(sReg > 0.01f && sReg < 100.0f))
        sReg = *reinterpret_cast<float *>(f + Offsets::OFF_LAYOUT_SCALE);
    const float inv = (sReg > 0.01f && sReg < 100.0f) ? 1.0f / sReg : 1.0f;

    // Single deterministic placement; the resolver's true offset scale is
    // converged by the verification pass in Maintain via r.corr (a same-call
    // rect readback is unreliable: fresh regions read zeros and reused pooled
    // regions read their PREVIOUS placement — that poisoned fix rendered the
    // 14px crest at 7.4px).
    const float scale = inv * r.corr;
    auto setPoint = reinterpret_cast<SetPoint_t>(Offsets::FUN_REGION_SET_POINT);
    void *texAnchor = reinterpret_cast<uint8_t *>(r.tex) + Offsets::OFF_REGION_ANCHOR;
    void *fsAnchor = f + Offsets::OFF_REGION_ANCHOR;
    setPoint(texAnchor, Offsets::FRAMEPOINT_BOTTOMLEFT, fsAnchor, Offsets::FRAMEPOINT_BOTTOMLEFT,
             p.x0 * scale, p.y0 * scale, 1);
    setPoint(texAnchor, Offsets::FRAMEPOINT_TOPRIGHT, fsAnchor, Offsets::FRAMEPOINT_BOTTOMLEFT,
             p.x1 * scale, p.y1 * scale, 1);
    reinterpret_cast<Realize_t>(Offsets::FUN_REGION_LAYOUT_REALIZE)(texAnchor, 0);
    Show(r.tex);
}

// WorldTick: create holders + apply queued placements (both deferred out of the
// render pass — regions must never be mutated mid-paint).
void Maintain() {
    if (!g_pending.empty()) {
        if (g_uiParent == nullptr)
            g_uiParent = ResolveUIParent();
        if (g_uiParent != nullptr) {
            for (const std::string &p : g_pending) {
                if (g_holders.find(p) != g_holders.end())
                    continue;
                void *tex = CreateHolder(p.c_str());
                if (tex != nullptr)
                    g_holders.emplace(p, tex);
            }
            g_pending.clear();
        }
    }

    ++g_maintainTick;
    for (auto &kv : g_fsIcons) {
        void *fs = kv.first;
        FsPlacements &np = kv.second;

        // Freshness expiry: a LIVE painted icon line re-queues every paint, so
        // its touch stamp stays current. A parked/orphaned fs (recycled chat
        // line whose node never rebuilds and may not even be walked) stops
        // being touched — hide its regions and drop the stale want so nothing
        // ghosts at the parked position. ~1s at a per-frame tick.
        constexpr uint32_t kWantTTL = 60;
        if (np.shown > 0 && g_maintainTick - np.lastTouchTick > kWantTTL) {
            for (int i = 0; i < np.shown; ++i)
                Hide(np.regions[static_cast<size_t>(i)].tex);
            np.shown = 0;
            np.want.clear();
            np.dirty = false;
            continue;
        }

        // Mirror the fontstring's visibility EVERY tick, not just on dirty:
        // expired/hidden chat lines get their fs Hidden by the message frame,
        // and our regions are parented to the CHAT FRAME (regions can't parent
        // regions), so they don't inherit the line's own hide.
        const bool fsAlive =
            LooksReadable(fs) &&
            *reinterpret_cast<const uint32_t *>(reinterpret_cast<const uint8_t *>(fs) +
                                                Offsets::OFF_REGION_ACTUALLY_SHOWN) != 0;
        if (!fsAlive) {
            for (int i = 0; i < np.shown; ++i)
                Hide(np.regions[static_cast<size_t>(i)].tex);
            np.shown = 0;
            np.dirty = true; // re-place if/when the fs shows again
            continue;
        }

        if (np.dirty) {
            np.dirty = false;
            void *parent = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(fs) +
                                                      Offsets::OFF_REGION_PARENT);
            if (!LooksReadable(parent))
                continue;
            const int want = static_cast<int>(np.want.size());
            for (int i = 0; i < want; ++i) {
                if (i >= static_cast<int>(np.regions.size())) {
                    void *tex = CreateRegion(parent);
                    if (tex == nullptr)
                        break;
                    np.regions.push_back(IconRegion{tex, parent, std::string()});
                }
                IconRegion &r = np.regions[static_cast<size_t>(i)];
                if (r.parent != parent) {
                    reinterpret_cast<SetParentAndLayer_t>(Offsets::FUN_REGION_SET_PARENT_AND_LAYER)(
                        r.tex, parent, Offsets::DRAWLAYER_ARTWORK, 0);
                    r.parent = parent;
                }
                ApplyPlacement(r, fs, np.want[static_cast<size_t>(i)]);
            }
            // Hide surplus pooled regions from a previous, larger icon set.
            for (int i = want; i < np.shown; ++i)
                if (i < static_cast<int>(np.regions.size()))
                    Hide(np.regions[static_cast<size_t>(i)].tex);
            np.shown = want;
        }

        // Convergence pass: compare each shown region's RESOLVED width against
        // its wanted width and fold the ratio into the region's correction. The
        // resolver's true offset scale is whatever it is (uiScale chains, pfUI
        // frame scales) — a tick or two of this converges the placement exactly
        // and self-heals any stale-pool or timing artifact. 0.5% deadband.
        const int live = (np.shown < static_cast<int>(np.want.size()))
                             ? np.shown
                             : static_cast<int>(np.want.size());
        for (int i = 0; i < live && i < static_cast<int>(np.regions.size()); ++i) {
            IconRegion &r = np.regions[static_cast<size_t>(i)];
            const Placement &p = np.want[static_cast<size_t>(i)];
            const float wantW = p.x1 - p.x0;
            if (r.tex == nullptr || wantW <= 1e-6f)
                continue;
            const float *rr = reinterpret_cast<const float *>(
                reinterpret_cast<uint8_t *>(r.tex) + Offsets::OFF_REGION_RECT);
            const float gotW = (rr[3] > rr[1]) ? (rr[3] - rr[1]) : (rr[1] - rr[3]);
            if (gotW <= 1e-7f) {
                ApplyPlacement(r, fs, p); // rect not resolved yet — re-kick
                continue;
            }
            const float err = gotW / wantW;
            if (err > 1.005f || err < 0.995f) {
                r.corr *= wantW / gotW;
                if (r.corr < 0.05f)
                    r.corr = 0.05f;
                else if (r.corr > 20.0f)
                    r.corr = 20.0f;
                ApplyPlacement(r, fs, p);
            }
        }
    }
}

static const Tick::WorldTick::AutoSubscribe _tick{&Maintain};

} // namespace

void Hold(const char *path) {
    if (path == nullptr || path[0] == '\0')
        return;
    if (g_holders.find(path) != g_holders.end())
        return; // already resident — the common steady state, no work
    g_pending.emplace(path);
}

void QueuePlacements(void *fs, std::vector<Placement> &&icons) {
    if (fs == nullptr)
        return;
    auto it = g_fsIcons.find(fs);
    if (it == g_fsIcons.end()) {
        if (icons.empty())
            return; // nothing shown, nothing wanted — don't create state
        FsPlacements np;
        np.want = std::move(icons);
        np.dirty = true;
        np.lastTouchTick = g_maintainTick;
        g_fsIcons.emplace(fs, std::move(np));
        return;
    }
    FsPlacements &np = it->second;
    np.lastTouchTick = g_maintainTick; // freshness — deduped touches count too
    if (np.want == icons)
        return; // identical re-queue (the per-paint steady state) — no work
    np.want = std::move(icons);
    np.dirty = true;
}

void HideAll() {
    for (auto &kv : g_fsIcons) {
        kv.second.want.clear();
        kv.second.dirty = true;
    }
}

void PrepareForReload() {
    // The reload teardown frees the holder + icon regions (they die with their
    // parent frames); only forget the pointers — never Hide/free them.
    g_holders.clear();
    g_pending.clear();
    g_fsIcons.clear();
    g_uiParent = nullptr;
}

int HolderCount() { return static_cast<int>(g_holders.size()); }

namespace {

// _classicapi_InlineTexHolders() -> holderCount, shownCount. shownCount sums
// the engine's actually-shown latch (+0xC8) — if it lags holderCount, holders
// exist but aren't drawing (i.e. no residency).
int __fastcall Script_InlineTexHolders(void *L) {
    int shown = 0;
    for (const auto &kv : g_holders) {
        if (*reinterpret_cast<const uint32_t *>(reinterpret_cast<const uint8_t *>(kv.second) +
                                                Offsets::OFF_REGION_ACTUALLY_SHOWN) != 0)
            ++shown;
    }
    Game::Lua::PushNumber(L, static_cast<double>(HolderCount()));
    Game::Lua::PushNumber(L, static_cast<double>(shown));
    return 2;
}

// _classicapi_InlineTexRegionDump() -> flag124, rect{yA,left,yB,right},
// vert0(x,y), vert3(x,y), uv0(u,v), uv3(u,v) — raw engine state of the first
// live CROPPED icon region (UV span < 0.99). Diagnoses the rect-vs-UV-span
// shrink (FUN_00770570, gated on region+0x124) with real numbers.
int __fastcall Script_InlineTexRegionDump(void *L) {
    for (auto &kv : g_fsIcons) {
        FsPlacements &np = kv.second;
        for (int i = 0; i < np.shown && i < static_cast<int>(np.regions.size()); ++i) {
            auto *t = reinterpret_cast<uint8_t *>(np.regions[static_cast<size_t>(i)].tex);
            if (t == nullptr)
                continue;
            const float u0 = *reinterpret_cast<float *>(t + 0x104);
            const float u1 = *reinterpret_cast<float *>(t + 0x114);
            const float span = (u1 > u0) ? (u1 - u0) : (u0 - u1);
            if (span > 0.99f)
                continue; // want a cropped icon — full-texture ones can't diagnose
            Game::Lua::PushNumber(L, static_cast<double>(*reinterpret_cast<uint32_t *>(t + 0x124)));
            for (int k = 0; k < 4; ++k)
                Game::Lua::PushNumber(L,
                                      *reinterpret_cast<float *>(t + Offsets::OFF_REGION_RECT +
                                                                 k * 4));
            Game::Lua::PushNumber(L, *reinterpret_cast<float *>(t + 0xD4)); // vert0 x
            Game::Lua::PushNumber(L, *reinterpret_cast<float *>(t + 0xD8)); // vert0 y
            Game::Lua::PushNumber(L, *reinterpret_cast<float *>(t + 0xF8)); // vert3 x
            Game::Lua::PushNumber(L, *reinterpret_cast<float *>(t + 0xFC)); // vert3 y
            Game::Lua::PushNumber(L, u0);                                   // uv0 u
            Game::Lua::PushNumber(L, *reinterpret_cast<float *>(t + 0x108)); // uv0 v
            Game::Lua::PushNumber(L, u1);                                    // uv3 u... (v3 pair)
            Game::Lua::PushNumber(L, *reinterpret_cast<float *>(t + 0x120)); // uv3 v
            return 13;
        }
    }
    return 0;
}

// _classicapi_InlineTexFsDump() -> total, markup, missingWant, lostApply,
// hiddenRegion, healthy, fsHidden, dirty. Triangulates WHERE the pipeline
// loses an icon when a scrolled chat line lands without one:
//   markup      = tracked fs whose LIVE text (fs+0xF0) contains |T
//   missingWant = markup fs with EMPTY want        -> paint side never queued
//   lostApply   = want>0, not dirty, shown<want    -> tick side lost the apply
//   hiddenRegion= applied but a region reads +0xC8==0 while its fs is shown
//   healthy     = markup fs fully applied and all regions shown
// Run while a scrolled-in line is visibly missing its icon.
int __fastcall Script_InlineTexFsDump(void *L) {
    int total = 0, markup = 0, missingWant = 0, lostApply = 0, hiddenRegion = 0, healthy = 0,
        fsHidden = 0, dirty = 0;
    int chain[7] = {0, 0, 0, 0, 0, 0, 0}; // DebugChainState buckets for missingWant fs
    for (auto &kv : g_fsIcons) {
        ++total;
        FsPlacements &np = kv.second;
        auto *f = reinterpret_cast<uint8_t *>(kv.first);
        if (!LooksReadable(f))
            continue;
        const bool fsShown =
            *reinterpret_cast<const uint32_t *>(f + Offsets::OFF_REGION_ACTUALLY_SHOWN) != 0;
        if (!fsShown)
            ++fsHidden;
        if (np.dirty)
            ++dirty;
        bool hasMarkup = false;
        const char *text = *reinterpret_cast<const char *const *>(f + 0xF0);
        if (LooksReadable(text))
            for (int k = 0; k < 2048 && text[k] != '\0'; ++k)
                if (text[k] == '|' && (text[k + 1] == 'T' || text[k + 1] == 't')) {
                    hasMarkup = (text[k + 1] == 'T');
                    if (hasMarkup)
                        break;
                }
        if (hasMarkup)
            ++markup;
        const int want = static_cast<int>(np.want.size());
        // Anomalies only count for VISIBLE fontstrings: a hidden line's node
        // isn't painted, so it legitimately stops re-queueing — an empty want
        // there is bookkeeping, not a bug (this blind spot produced phantom
        // missingWant counts next to fsHidden).
        if (hasMarkup && want == 0) {
            if (fsShown) {
                ++missingWant;
                const int cs = Text::InlineTexture::DebugChainState(kv.first);
                if (cs >= 0 && cs <= 6)
                    ++chain[cs];
            }
            continue;
        }
        if (want > 0 && !np.dirty && np.shown < want) {
            ++lostApply;
            continue;
        }
        bool anyRegionHidden = false;
        for (int i = 0; i < np.shown && i < static_cast<int>(np.regions.size()); ++i) {
            auto *t = reinterpret_cast<uint8_t *>(np.regions[static_cast<size_t>(i)].tex);
            if (LooksReadable(t) &&
                *reinterpret_cast<const uint32_t *>(t + Offsets::OFF_REGION_ACTUALLY_SHOWN) == 0) {
                anyRegionHidden = true;
                break;
            }
        }
        if (fsShown && want > 0 && anyRegionHidden) {
            ++hiddenRegion;
            continue;
        }
        if (hasMarkup && fsShown && want > 0 && np.shown >= want)
            ++healthy;
    }
    Game::Lua::PushNumber(L, total);
    Game::Lua::PushNumber(L, markup);
    Game::Lua::PushNumber(L, missingWant);
    Game::Lua::PushNumber(L, lostApply);
    Game::Lua::PushNumber(L, hiddenRegion);
    Game::Lua::PushNumber(L, healthy);
    Game::Lua::PushNumber(L, fsHidden);
    Game::Lua::PushNumber(L, dirty);
    // Chain classification of the missingWant fs (see DebugChainState):
    // [9] unreadable, [10] node unmapped, [11] mapped to other fs,
    // [12] authoritative but NO records, [13] authoritative WITH records,
    // [14] blockless rebuild-pending, [15] blockless STUCK (nudged).
    for (int i = 0; i < 7; ++i)
        Game::Lua::PushNumber(L, chain[i]);
    return 15;
}

// _classicapi_InlineTexBroken() -> up to 2 × (textSnippet, chainState,
// nodeFlags, nodeSeenAge) for VISIBLE markup fontstrings whose want is empty —
// names the actual broken lines; nodeSeenAge large/growing = the node's layout
// is never painted, so the flush can never queue it.
int __fastcall Script_InlineTexBroken(void *L) {
    int pushed = 0;
    for (auto &kv : g_fsIcons) {
        if (pushed >= 8)
            break;
        FsPlacements &np = kv.second;
        if (!np.want.empty())
            continue;
        auto *f = reinterpret_cast<uint8_t *>(kv.first);
        if (!LooksReadable(f))
            continue;
        if (*reinterpret_cast<const uint32_t *>(f + Offsets::OFF_REGION_ACTUALLY_SHOWN) == 0)
            continue;
        const char *text = *reinterpret_cast<const char *const *>(f + 0xF0);
        if (!LooksReadable(text))
            continue;
        bool hasMarkup = false;
        for (int k = 0; k < 2048 && text[k] != '\0'; ++k)
            if (text[k] == '|' && text[k + 1] == 'T') {
                hasMarkup = true;
                break;
            }
        if (!hasMarkup)
            continue;
        char snip[96];
        int n = 0;
        while (n < 95 && text[n] != '\0') {
            snip[n] = text[n];
            ++n;
        }
        snip[n] = '\0';
        Game::Lua::PushString(L, snip);
        Game::Lua::PushNumber(L, Text::InlineTexture::DebugChainState(kv.first));
        Game::Lua::PushNumber(L,
                              static_cast<double>(Text::InlineTexture::DebugNodeFlags(kv.first)));
        Game::Lua::PushNumber(L,
                              static_cast<double>(Text::InlineTexture::DebugNodeSeenAge(kv.first)));
        pushed += 4;
    }
    return pushed;
}

// _classicapi_InlineTexHolderCfg([visible][, size][, alpha]) -> the three.
// Live-reconfigures every holder; omitted args keep their value. `visible` and
// `size` are anchor units (~[0..1] across the screen; px ≈ units × screenWidth),
// `alpha` is a 0..255 byte. Diagnostic bisect: crank visible to see the holders
// (e.g. 0.05, size 0.05, alpha 255 = a visible box bottom-left), then walk back.
int __fastcall Script_InlineTexHolderCfg(void *L) {
    if (Game::Lua::IsNumber(L, 1))
        g_cfgVisible = static_cast<float>(Game::Lua::ToNumber(L, 1));
    if (Game::Lua::IsNumber(L, 2))
        g_cfgSize = static_cast<float>(Game::Lua::ToNumber(L, 2));
    if (Game::Lua::IsNumber(L, 3))
        g_cfgAlpha = static_cast<uint32_t>(Game::Lua::ToNumber(L, 3)) & 0xFFu;
    Reconfigure();
    Game::Lua::PushNumber(L, g_cfgVisible);
    Game::Lua::PushNumber(L, g_cfgSize);
    Game::Lua::PushNumber(L, static_cast<double>(g_cfgAlpha));
    return 3;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexHolders", &Script_InlineTexHolders);
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexHolderCfg",
                                      &Script_InlineTexHolderCfg);
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexRegionDump",
                                      &Script_InlineTexRegionDump);
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexFsDump", &Script_InlineTexFsDump);
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexBroken", &Script_InlineTexBroken);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Text::InlineTexturePool
