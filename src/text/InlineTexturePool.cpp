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

// Engine-region rendering for inline-|T icons — see the header. One pooled
// CSimpleTexture per visible icon, anchored to its owning fontstring; the
// engine draws it every frame like any UI region, so the texture is resident
// by construction and the icon moves with its line for free.
//
// A CSimpleTexture renders as a gxu text node whose page texture IS the
// region's CGxTex: the layout paint (FUN_005c8fe0) binds it (GxRsSet 0x17) and
// submits its quad EVERY FRAME the region is shown.

#include "text/InlineTexturePool.h"

#include "Offsets.h"
#include "text/PtrProbe.h"
#include "tick/FrameTick.h"

#include <windows.h> // SEH (__try/__except, EXCEPTION_EXECUTE_HANDLER)

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

// LAA-aware pointer sanity probe: Text::LooksReadable (text/PtrProbe.h).

// Vtables seen at object+0x00 of REAL, live fontstrings — learned at paint time
// (see LearnFontStringVtable / QueuePlacements). A fontstring is polymorphic, so
// its vtable is a fixed .rdata pointer per class. We LEARN rather than hardcode
// because chat (ScrollingMessageFrame's internal lines), tooltip, and addon
// fontstrings are different classes with different vtables — a single hardcoded
// value filtered live chat icons out entirely.
std::unordered_set<uintptr_t> g_fsVtables;

// A vtable pointer that lands in the WoW.exe image (.rdata) — engine C++ classes
// (fontstrings, our CSimpleTexture regions) carry their vtable there. Gates what
// the learned-vtable sets admit so a stray value never poisons them.
bool InWowImage(uintptr_t vt) { return vt >= 0x00400000u && vt < 0x00D2B000u; }

// Record a live fontstring's vtable. Called only from QueuePlacements, which the
// paint hook invokes with a fontstring the engine is actively drawing — so
// *(void**)fs is a valid vtable here. The module-range gate is belt-and-braces
// against ever poisoning the set with a stray value.
void LearnFontStringVtable(const void *fs) {
    if (!LooksReadable(fs))
        return;
    const uintptr_t vt = *reinterpret_cast<const uintptr_t *>(fs);
    if (InWowImage(vt))
        g_fsVtables.insert(vt);
}

// True only for a LIVE fontstring: its object+0x00 vtable matches one learned
// from a genuinely-painted fontstring. A bare VA-range check is not enough — a
// freed fontstring whose heap memory is reused (as a string buffer, …) still
// passes it, and reading a garbage "parent" (fs+0x9C) from such a stale key
// crashed the region attach (FUN_0076a750 dereferencing *(parent+0x1B4), parent
// = ASCII "-Rac" string bytes). Reused memory never carries a fontstring vtable.
// The vtable read is safe: the range check bounds the pointer, and these object
// pages stay committed (we already read fs+0xC8 on the same pointer).
bool IsLiveFontString(const void *fs) {
    if (!LooksReadable(fs))
        return false;
    return g_fsVtables.count(*reinterpret_cast<const uintptr_t *>(fs)) != 0;
}

// Same technique, for our pooled CSimpleTexture regions. Learned from
// CreateRegion's freshly-ctor'd object (the ctor writes the vtable). A region is
// parented to the fontstring's PARENT FRAME, so it outlives the fontstring — EXCEPT
// when that parent frame itself dies, which frees the fontstring AND its child
// regions together. The eviction path runs with a non-live fontstring, so it can't
// assume the regions survived; this gate keeps its Hide off a region whose memory
// was freed and recycled (a bare pointer there would fault).
std::unordered_set<uintptr_t> g_regionVtables;

void LearnRegionVtable(const void *tex) {
    if (!LooksReadable(tex))
        return;
    const uintptr_t vt = *reinterpret_cast<const uintptr_t *>(tex);
    if (InWowImage(vt))
        g_regionVtables.insert(vt);
}

bool IsLiveRegion(const void *tex) {
    if (!LooksReadable(tex))
        return false;
    return g_regionVtables.count(*reinterpret_cast<const uintptr_t *>(tex)) != 0;
}

using SetTexCoord_t = void(__thiscall *)(void *tex, const float *coords4);
using SetParentAndLayer_t = void(__thiscall *)(void *region, void *parentFrame, int layer,
                                               int show);

// --- region placement state --------------------------------------------------

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
    // The fs color alpha last folded into the shown regions' colors (the
    // chat-fade mirror — see FsColorAlpha below). Re-applied on change only.
    uint8_t appliedAlpha = 0xFF;
};

std::unordered_map<void *, FsPlacements> g_fsIcons;
uint32_t g_maintainTick = 0;

// Latched off by Maintain's SEH guard on a fault; re-armed on /reload (see
// PrepareForReload). Mirrors InlineTexture.cpp's g_inlineEnabled paint latch.
bool g_maintainDisabled = false;

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

// The fontstring's live color alpha byte — THE CHAT-FADE SIGNAL. The
// ScrollingMessageFrame's fader animates each visible line fontstring's alpha
// through the fs SetColor every frame (see OFF_FONTSTRING_COLOR_ARRAY's
// derivation); mirroring the byte onto the line's icon regions makes inline
// icons fade with their text. Count 0 = never colored = opaque default.
uint8_t FsColorAlpha(const void *fs) {
    auto *f = reinterpret_cast<const uint8_t *>(fs);
    if (*reinterpret_cast<const uint32_t *>(f + Offsets::OFF_FONTSTRING_COLOR_COUNT) < 1u)
        return 0xFFu;
    const uint8_t *colors =
        *reinterpret_cast<const uint8_t *const *>(f + Offsets::OFF_FONTSTRING_COLOR_ARRAY);
    if (!LooksReadable(colors))
        return 0xFFu;
    return colors[3]; // slot 0's BGRA dword, alpha byte
}

// Placement color with the fs's fade alpha folded in: the markup tint's own
// alpha (0xFF except explicit tints) modulated by the line's live alpha.
uint32_t FadedColor(const Placement &p, uint8_t fsAlpha) {
    const uint32_t a = ((p.color >> 24) * fsAlpha) / 0xFFu;
    return (a << 24) | (p.color & 0x00FFFFFFu);
}

// Creates a bare pooled CSimpleTexture parented to `parent` (ARTWORK).
void *CreateRegion(void *parent) {
    auto alloc = reinterpret_cast<PoolAlloc_t>(Offsets::FUN_REGION_POOL_ALLOC);
    void *mem = alloc(reinterpret_cast<void *>(Offsets::VAR_SIMPLETEXTURE_POOL), 0,
                      reinterpret_cast<const char *>(Offsets::VAR_SIMPLETEXTURE_CLASS_TAG), -2);
    if (mem == nullptr)
        return nullptr;
    auto ctor = reinterpret_cast<TexCtor_t>(Offsets::FUN_SIMPLETEXTURE_CTOR);
    void *tex = ctor(mem, parent, Offsets::DRAWLAYER_ARTWORK, 1);
    LearnRegionVtable(tex); // for the eviction-path liveness gate
    return tex;
}

// Applies one placement to one region: texture, texcoords, color (with the
// line's fade alpha folded in), and the two anchor points expressed relative to
// the owning fontstring's BOTTOMLEFT (so the engine moves the icon with its
// line for free).
void ApplyPlacement(IconRegion &r, void *fs, const Placement &p, uint8_t fsAlpha) {
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
    const uint32_t color = FadedColor(p, fsAlpha);
    reinterpret_cast<SetColor_t>(Offsets::FUN_FONTSTRING_SET_COLOR)(r.tex, &color);

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

// Per-frame tick (FrameTick — glue AND world): apply queued placements
// (deferred out of the text paint — regions must never be mutated mid-paint).
// Wrapped by Maintain() below; kept destructor-holding C++ objects out of the
// SEH frame (MSVC won't allow __try in a function that needs object unwinding).
void MaintainImpl() {
    ++g_maintainTick;
    for (auto it = g_fsIcons.begin(); it != g_fsIcons.end();) {
        void *fs = it->first;
        FsPlacements &np = it->second;

        // Stale-key eviction (the crash fix). A destroyed fontstring leaves a
        // dangling key here; its heap memory may be reused as unrelated data, so
        // a VA-range check is not enough — verify the live-fontstring vtable. If
        // it is gone, hide our regions and erase the entry. This also stops dead
        // keys from accumulating: only live fontstrings survive, and the
        // chat/tooltip pools reuse a bounded set of them.
        if (!IsLiveFontString(fs)) {
            // The regions usually outlive the fontstring (they're parented to its
            // parent FRAME, which persists while lines churn). But if the fontstring
            // died because that parent frame died, the regions died with it — so
            // gate each Hide on the region's OWN vtable liveness, else Hide would
            // write to freed/recycled region memory. A dead region needs no hiding
            // (it's already gone); a live one still gets hidden so it can't ghost.
            for (int i = 0; i < np.shown && i < static_cast<int>(np.regions.size()); ++i)
                if (IsLiveRegion(np.regions[static_cast<size_t>(i)].tex))
                    Hide(np.regions[static_cast<size_t>(i)].tex);
            // ACCEPTED LEAK: erasing the entry drops np.regions without freeing
            // the pooled CSimpleTextures — they stay (hidden) on their parent
            // frame until it dies (/reload frees all of them). We do NOT reclaim
            // them, on purpose: any reclaim (a free list, or an explicit destroy)
            // would reuse/free a region pointer whose parent frame may already be
            // gone — a use-after-free on the exact path this liveness check was
            // added to stop crashing. The leak is bounded and near-zero in
            // practice: chat/tooltip fontstrings are a reused, never-evicted set,
            // so only genuinely-destroyed transient |T fontstrings ever land here.
            it = g_fsIcons.erase(it);
            continue;
        }

        // Freshness expiry: a LIVE painted icon line re-queues every paint, so
        // its touch stamp stays current. A parked/orphaned fs (recycled chat
        // line whose node never rebuilds and may not even be walked) stops
        // being touched — hide its regions and drop the stale want so nothing
        // ghosts at the parked position. ~1s at a per-frame tick.
        constexpr uint32_t kWantTTL = 60;
        if (np.shown > 0 && g_maintainTick - np.lastTouchTick > kWantTTL) {
            for (int i = 0; i < np.shown && i < static_cast<int>(np.regions.size()); ++i)
                Hide(np.regions[static_cast<size_t>(i)].tex);
            np.shown = 0;
            np.want.clear();
            np.dirty = false;
            ++it;
            continue;
        }

        // Mirror the fontstring's own visibility EVERY tick, not just on dirty:
        // expired/hidden chat lines get their fs Hidden by the message frame,
        // and our regions are parented to the CHAT FRAME (regions can't parent
        // regions), so they don't inherit the line's own hide. fs is a live
        // fontstring here (checked above), so this read is safe.
        const bool fsShown =
            *reinterpret_cast<const uint32_t *>(reinterpret_cast<const uint8_t *>(fs) +
                                                Offsets::OFF_REGION_ACTUALLY_SHOWN) != 0;
        if (!fsShown) {
            for (int i = 0; i < np.shown && i < static_cast<int>(np.regions.size()); ++i)
                Hide(np.regions[static_cast<size_t>(i)].tex);
            np.shown = 0;
            np.dirty = true; // re-place if/when the fs shows again
            ++it;
            continue;
        }

        // The line's live fade alpha (see FsColorAlpha) — folded into every
        // color write below and mirrored on change.
        const uint8_t fsAlpha = FsColorAlpha(fs);

        if (np.dirty) {
            np.dirty = false;
            void *parent = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(fs) +
                                                      Offsets::OFF_REGION_PARENT);
            if (!LooksReadable(parent)) {
                ++it;
                continue;
            }
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
                ApplyPlacement(r, fs, np.want[static_cast<size_t>(i)], fsAlpha);
            }
            // Hide surplus pooled regions from a previous, larger icon set.
            for (int i = want; i < np.shown; ++i)
                if (i < static_cast<int>(np.regions.size()))
                    Hide(np.regions[static_cast<size_t>(i)].tex);
            np.shown = (want < static_cast<int>(np.regions.size()))
                           ? want
                           : static_cast<int>(np.regions.size());
            np.appliedAlpha = fsAlpha;
        } else if (np.shown > 0 && fsAlpha != np.appliedAlpha) {
            // CHAT-FADE MIRROR: the ScrollingMessageFrame fader animates the
            // line fontstring's color alpha every frame; keep the icon regions'
            // alpha in lockstep so inline icons fade with their text. Change-
            // driven — steady-state (no fade running) costs one byte compare.
            np.appliedAlpha = fsAlpha;
            const int live = (np.shown < static_cast<int>(np.want.size()))
                                 ? np.shown
                                 : static_cast<int>(np.want.size());
            for (int i = 0; i < live && i < static_cast<int>(np.regions.size()); ++i) {
                IconRegion &r = np.regions[static_cast<size_t>(i)];
                if (r.tex == nullptr)
                    continue;
                const uint32_t color = FadedColor(np.want[static_cast<size_t>(i)], fsAlpha);
                reinterpret_cast<SetColor_t>(Offsets::FUN_FONTSTRING_SET_COLOR)(r.tex, &color);
            }
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
                ApplyPlacement(r, fs, p, fsAlpha); // rect not resolved yet — re-kick
                continue;
            }
            const float err = gotW / wantW;
            if (err > 1.005f || err < 0.995f) {
                r.corr *= wantW / gotW;
                if (r.corr < 0.05f)
                    r.corr = 0.05f;
                else if (r.corr > 20.0f)
                    r.corr = 20.0f;
                ApplyPlacement(r, fs, p, fsAlpha);
            }
        }

        ++it;
    }
}

// SEH latch mirroring InlineTexture.cpp's SafeFlush. Maintain's liveness probe
// dereferences a fontstring pointer whose object *may* have been freed (see
// IsLiveFontString) — a residual the design accepts. The paint path is already
// guarded; this closes the matching hole on the tick path, so a fault disables
// maintenance for the session instead of crashing the client. A /reload re-arms
// it (PrepareForReload). The __try body holds no C++ objects — see MaintainImpl.
void Maintain() {
    if (g_maintainDisabled)
        return;
    __try {
        MaintainImpl();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_maintainDisabled = true;
    }
}

// FrameTick, not WorldTick: icon regions are UI objects that also exist on the
// glue screen (addon-list titles with |T emotes), and FrameTick fires wherever
// the UI renders — glue and world. (An earlier FrameTick attempt was blamed
// for an in-world regression and reverted; that was the LAA pointer-bound
// phantom — see laa-pointer-bounds / dc61f77. Single tick is the clean design.)
static const Tick::FrameTick::AutoSubscribe _tick{&Maintain};

} // namespace

void QueuePlacements(void *fs, std::vector<Placement> &&icons) {
    if (fs == nullptr)
        return;
    // fs is a fontstring the engine is actively painting — learn its class
    // vtable so Maintain's liveness check recognizes it (and stale, reused-memory
    // keys, which never carry one). Must precede the early returns below so an
    // entry's vtable is always known before Maintain evaluates it.
    LearnFontStringVtable(fs);
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

void PrepareForReload() {
    // The reload teardown frees the icon regions (they die with their parent
    // frames); only forget the pointers — never Hide/free them.
    g_fsIcons.clear();
    g_maintainDisabled = false; // re-arm the SEH latch for the fresh UI
}

} // namespace Text::InlineTexturePool
