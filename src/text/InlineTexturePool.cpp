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

// Engine-texture rendering for inline-|T icons — see the header. One pooled
// CSimpleTexture per icon, anchored to its owning fontstring, drawn by the
// fontstring itself (Emit_h below), so the texture is resident by
// construction, moves with its line and shows exactly when its line does.

#include "text/InlineTexturePool.h"

#include "Game.h"
#include "Offsets.h"
#include "frame/Region.h"
#include "frame/RegionDestruction.h"
#include "text/PtrProbe.h"
#include "tick/FrameTick.h"

#include <windows.h> // SEH (__try/__except, EXCEPTION_EXECUTE_HANDLER)

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Text::InlineTexturePool {

namespace {

using SetTexture_t = uint32_t(__thiscall *)(void *tex, const char *path, int a, uint32_t blend,
                                            int b);
using SetColor_t = void(__thiscall *)(void *tex, const void *colorBGRA);
using SetPoint_t = void(__thiscall *)(void *anchor, int point, void *relAnchor, int relPoint,
                                      float x, float y, int flag);
using Realize_t = void(__thiscall *)(void *anchor, int flag);
using SetTexCoord_t = void(__thiscall *)(void *tex, const float *coords4);
using DeletingDestructor_t = void(__thiscall *)(void *region, int flags);
using Emit_t = void(__fastcall *)(void *region, void *edx, void *batch);
using NotifyLayerChanged_t = void(__fastcall *)(void *region);
using OnColorChanged_t = void(__fastcall *)(void *fs, void *edx, int changed);

// LAA-aware pointer sanity probe: Text::LooksReadable (text/PtrProbe.h).

// --- texture placement state -------------------------------------------------

// A pooled icon texture and the config it currently carries (for dedup).
struct IconRegion {
    void *tex = nullptr;
    std::string path;
};

// Keyed by FONTSTRING (stable across SetText — text nodes are reallocated per
// rebuild, so a node key would orphan textures every chat scroll step: the
// "wall of icons" bug).
struct FsPlacements {
    std::vector<Placement> want;     // the placements the textures carry
    std::vector<IconRegion> regions; // textures currently serving this fs
    int shown = 0;                   // how many of `regions` the fs draws
};

std::unordered_map<void *, FsPlacements> g_fsIcons;

// Every icon texture still alive. They have no frame, so nothing but this
// module deletes them.
std::unordered_set<void *> g_iconRegions;

// Textures to delete at the next tick: those of destroyed fontstrings and,
// after a reload, all of them. By the tick the draw has rebuilt the batches
// that held them (the fontstring's removal marked its layer).
std::vector<void *> g_orphans;

// Frame::RegionDestruction. A destroyed key fontstring takes its entry with it
// at once: its block goes back to the region pool and can come back as an
// unrelated fontstring at the same address, which must start with no entry.
// Its textures are deleted at the next tick, never from inside the destructor
// that reported the death.
void OnRegionDestroyed(void *region) {
    const auto it = g_fsIcons.find(region);
    if (it == g_fsIcons.end())
        return;
    for (const IconRegion &r : it->second.regions)
        g_orphans.push_back(r.tex);
    g_fsIcons.erase(it);
}

const Frame::RegionDestruction::AutoSubscribe _regionDestructionSub{&OnRegionDestroyed};

// Latched off by the pool's SEH guards on a fault; re-armed on /reload (see
// PrepareForReload). Mirrors InlineTexture.cpp's g_inlineEnabled paint latch.
bool g_maintainDisabled = false;

// The fontstring's live folded colour (slot 0's BGRA dword) — THE FADE
// SIGNAL. Its alpha byte is the chat-fade channel: the ScrollingMessageFrame's
// fader animates each visible line fontstring's alpha through the fs SetColor
// every frame (see OFF_FONTSTRING_COLOR_ARRAY's derivation), and the engine
// keeps the byte folded with the frame-chain effective alpha. Count 0 = never
// coloured = opaque white default. The icons have no frame, so this is the
// only alpha they get (see OnColorChanged_h).
uint32_t FsColorKey(const void *fs) {
    if (Game::Read<uint32_t>(fs, Offsets::OFF_FONTSTRING_COLOR_COUNT) < 1u)
        return 0xFFFFFFFFu;
    const uint8_t *colors = Game::Read<const uint8_t *>(fs, Offsets::OFF_FONTSTRING_COLOR_ARRAY);
    if (!LooksReadable(colors))
        return 0xFFFFFFFFu;
    return *reinterpret_cast<const uint32_t *>(colors); // slot 0, BGRA
}

// Placement colour with the fs's live colour folded in. The markup tint's own
// alpha (0xFF except explicit tints) is modulated by the line's live alpha —
// everywhere. On the GLUE screen the icon's brightness ALSO follows the text
// colour's VALUE (max of R/G/B): the glue AddonList greys disabled entries
// through SetTextColor RGB alone (alpha stays 1.0), so value-follow is the
// only channel that dims a disabled addon title's icons with its text. It can
// never TINT — a gold or red title has a 255-max channel, factor 1.0, icons
// untouched — and it is gated to glue (no local player) so in-world text
// colours (guild green, whisper pink) keep icons exactly as before.
uint32_t FadedColor(const Placement &p, uint32_t fsColor) {
    const uint32_t a = ((p.color >> 24) * (fsColor >> 24)) / 0xFFu;
    uint32_t rgb = p.color & 0x00FFFFFFu;
    const bool inGlue =
        *reinterpret_cast<void *volatile *>(Offsets::VAR_LOCAL_PLAYER_PTR) == nullptr;
    if (inGlue) {
        const uint32_t b = fsColor & 0xFFu, g = (fsColor >> 8) & 0xFFu,
                       r = (fsColor >> 16) & 0xFFu;
        uint32_t value = r > g ? r : g;
        if (b > value)
            value = b;
        if (value < 0xFFu) {
            const uint32_t pr = ((rgb >> 16) & 0xFFu) * value / 0xFFu;
            const uint32_t pg = ((rgb >> 8) & 0xFFu) * value / 0xFFu;
            const uint32_t pb = (rgb & 0xFFu) * value / 0xFFu;
            rgb = (pr << 16) | (pg << 8) | pb;
        }
    }
    return (a << 24) | rgb;
}

// Creates a bare pooled CSimpleTexture with no frame, as 4.3.4's
// CSimpleEmbeddedTexture does: it is in no draw layer, its layout is live (a
// region is deferred only by a hide), and the frame-alpha fold skips it
// (FUN_0077FAC0 tests the parent), so its colour is exactly what it is given.
void *CreateRegion() {
    void *tex = Frame::Region::CreateTexture(nullptr, Offsets::DRAWLAYER_ARTWORK, 1);
    if (tex == nullptr)
        return nullptr;
    g_iconRegions.insert(tex);
    return tex;
}

// Deletes an icon texture the way the frame destructor deletes its regions.
void DeleteRegion(void *tex) {
    if (g_iconRegions.erase(tex) == 0)
        return;
    auto *vtable = Game::Read<DeletingDestructor_t *>(tex, 0);
    vtable[Offsets::VTBL_REGION_DELETING_DESTRUCTOR / sizeof(void *)](tex, 1);
}

// Applies one placement to one texture: image, texcoords, color (with the
// line's fade alpha folded in), and the two anchor points expressed relative
// to the owning fontstring's BOTTOMLEFT (so the engine moves the icon with its
// line for free).
void ApplyPlacement(IconRegion &r, void *fs, const Placement &p, uint32_t fsColor) {
    auto *f = reinterpret_cast<uint8_t *>(fs);
    // Placement coords are FS-RELATIVE (from the fs rect's min corner), computed
    // at PAINT time in the same flush as the icon coords. Do NOT read the fs
    // rect here: an apply-time read raced the chat relayout (SetText briefly
    // invalidates the rect) and parked icons off their line, where the dedup
    // then froze them — the scroll-landing "randomly hidden icon" bug.

    if (r.path != p.path) {
        const uint32_t blend = Game::Read<uint32_t>(Offsets::VAR_TEXTURE_BLEND_DEFAULT);
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
    const uint32_t color = FadedColor(p, fsColor);
    reinterpret_cast<SetColor_t>(Offsets::FUN_FONTSTRING_SET_COLOR)(r.tex, &color);

    // Placement coords are in resolved-rect (anchor) units. The resolver places
    // a point at the positioned region's own layout scale × the stored offset
    // + the relative edge (RelativePointGetX/Y 0x7A2F90/0x7A3070); the texture
    // has no frame, so nothing changes the ctor's scale of 1 (CLayoutFrame
    // 0x00767680) and the offsets land exactly as given, as 4.3.4's
    // CSimpleEmbeddedTexture's do.
    auto setPoint = reinterpret_cast<SetPoint_t>(Offsets::FUN_REGION_SET_POINT);
    void *texAnchor = reinterpret_cast<uint8_t *>(r.tex) + Offsets::OFF_REGION_ANCHOR;
    void *fsAnchor = f + Offsets::OFF_REGION_ANCHOR;
    setPoint(texAnchor, Offsets::FRAMEPOINT_BOTTOMLEFT, fsAnchor, Offsets::FRAMEPOINT_BOTTOMLEFT,
             p.x0, p.y0, 1);
    setPoint(texAnchor, Offsets::FRAMEPOINT_TOPRIGHT, fsAnchor, Offsets::FRAMEPOINT_BOTTOMLEFT,
             p.x1, p.y1, 1);
    reinterpret_cast<Realize_t>(Offsets::FUN_REGION_LAYOUT_REALIZE)(texAnchor, 0);
}

// Applies `want` to the fontstring's textures: create and configure each. The
// layer batch copies the texture set, images and colour counts when it is
// built (FUN_00772FD0), so the fontstring's layer is marked for a rebuild —
// the call SetTexture makes for a texture in a layer.
void ApplyPlacements(void *fs, FsPlacements &np, uint32_t fsColor) {
    const int want = static_cast<int>(np.want.size());
    for (int i = 0; i < want; ++i) {
        if (i >= static_cast<int>(np.regions.size())) {
            void *tex = CreateRegion();
            if (tex == nullptr)
                break;
            np.regions.push_back(IconRegion{tex, std::string()});
        }
        IconRegion &r = np.regions[static_cast<size_t>(i)];
        ApplyPlacement(r, fs, np.want[static_cast<size_t>(i)], fsColor);
    }
    np.shown = (want < static_cast<int>(np.regions.size()))
                   ? want
                   : static_cast<int>(np.regions.size());
    reinterpret_cast<NotifyLayerChanged_t>(Offsets::FUN_REGION_NOTIFY_LAYER_CHANGED)(fs);
}

// Per-frame tick (FrameTick — glue AND world): deletes orphaned textures.
// Wrapped by Maintain() below; kept destructor-holding C++ objects out of the
// SEH frame (MSVC won't allow __try in a function that needs object
// unwinding).
void MaintainImpl() {
    if (g_orphans.empty())
        return;
    std::vector<void *> orphans;
    orphans.swap(g_orphans);
    for (void *tex : orphans)
        DeleteRegion(tex);
}

// SEH latch mirroring InlineTexture.cpp's SafeFlush: the pool drives engine
// texture calls (SetTexture with addon-supplied paths, SetPoint), so a fault
// disables it for the session instead of crashing the client. A /reload
// re-arms it (PrepareForReload). The __try body holds no C++ objects — see
// MaintainImpl.
void Maintain() {
    if (g_maintainDisabled)
        return;
    __try {
        MaintainImpl();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_maintainDisabled = true;
    }
}

// Applies `icons` to one fontstring unless its textures already carry them.
void ApplyImpl(void *fs, std::vector<Placement> &icons) {
    auto it = g_fsIcons.find(fs);
    if (it == g_fsIcons.end()) {
        if (icons.empty())
            return; // nothing drawn, nothing wanted — don't create state
        it = g_fsIcons.emplace(fs, FsPlacements{}).first;
    }
    FsPlacements &np = it->second;
    if (np.want == icons && np.shown == static_cast<int>(icons.size()))
        return;
    np.want = std::move(icons);
    ApplyPlacements(fs, np, FsColorKey(fs));
}

// SEH split mirroring Maintain/MaintainImpl, on the same kill switch.
void SafeApply(void *fs, std::vector<Placement> &icons) {
    if (g_maintainDisabled)
        return;
    __try {
        ApplyImpl(fs, icons);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_maintainDisabled = true;
    }
}

// FrameTick, not WorldTick: icon textures also exist on the glue screen
// (addon-list titles with |T emotes), and FrameTick fires wherever the UI
// renders — glue and world.
static const Tick::FrameTick::AutoSubscribe _tick{&Maintain};

// The fontstring's draw call: its icons go into the layer batch right after
// its text, and only when the text went in (the append counts it), as 4.3.4's
// CSimpleFontString::Draw queues its embedded textures after the string. So
// an icon draws exactly when its line's text does. A texture's own emit
// (FUN_SIMPLETEXTURE_EMIT) appends its quad when it has an image.
Emit_t g_origEmit = nullptr;

void __fastcall Emit_h(void *fs, void *edx, void *batch) {
    const int before = Game::Read<int>(batch, Offsets::OFF_LAYER_BATCH_COUNT);
    g_origEmit(fs, edx, batch);
    if (g_maintainDisabled || Game::Read<int>(batch, Offsets::OFF_LAYER_BATCH_COUNT) == before)
        return;
    const auto it = g_fsIcons.find(fs);
    if (it == g_fsIcons.end())
        return;
    const FsPlacements &np = it->second;
    for (int i = 0; i < np.shown && i < static_cast<int>(np.regions.size()); ++i)
        reinterpret_cast<Emit_t>(Offsets::FUN_SIMPLETEXTURE_EMIT)(np.regions[static_cast<size_t>(i)].tex,
                                                                  nullptr, batch);
}

const Game::HookAutoRegister _emitHook{Offsets::FUN_FONTSTRING_EMIT, reinterpret_cast<void *>(&Emit_h),
                                       reinterpret_cast<void **>(&g_origEmit)};

// The fontstring's colour change (FUN_FONTSTRING_ON_COLOR_CHANGED), where
// 4.3.4's CSimpleFontString::OnColorChanged copies the string's alpha into its
// embedded textures. Every change of the fs colour ends here, folded by the
// time the original returns, so the icons take the line's fade (and, on glue,
// its value — see FadedColor) in the same call. The batch reads the colour
// through a pointer, so no rebuild.
OnColorChanged_t g_origOnColorChanged = nullptr;

void MirrorFade(void *fs) {
    const auto it = g_fsIcons.find(fs);
    if (it == g_fsIcons.end())
        return;
    const FsPlacements &np = it->second;
    const uint32_t fsColor = FsColorKey(fs);
    for (int i = 0; i < np.shown; ++i) {
        const uint32_t color = FadedColor(np.want[static_cast<size_t>(i)], fsColor);
        reinterpret_cast<SetColor_t>(Offsets::FUN_FONTSTRING_SET_COLOR)(
            np.regions[static_cast<size_t>(i)].tex, &color);
    }
}

// SEH split mirroring Maintain/MaintainImpl, on the same kill switch.
void SafeMirrorFade(void *fs) {
    if (g_maintainDisabled)
        return;
    __try {
        MirrorFade(fs);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_maintainDisabled = true;
    }
}

void __fastcall OnColorChanged_h(void *fs, void *edx, int changed) {
    g_origOnColorChanged(fs, edx, changed);
    SafeMirrorFade(fs);
}

const Game::HookAutoRegister _colorHook{Offsets::FUN_FONTSTRING_ON_COLOR_CHANGED,
                                        reinterpret_cast<void *>(&OnColorChanged_h),
                                        reinterpret_cast<void **>(&g_origOnColorChanged)};

} // namespace

void Apply(void *fs, std::vector<Placement> &&icons) {
    if (fs != nullptr)
        SafeApply(fs, icons);
}

void Release(void *fs) {
    const auto it = g_fsIcons.find(fs);
    if (it == g_fsIcons.end())
        return;
    // The rebuild that called this marks the fontstring's layer itself.
    it->second.want.clear();
    it->second.shown = 0;
}

void PrepareForReload() {
    // The textures have no frame, so the teardown leaves them alive (a
    // destroyed fontstring only clears their anchor). Delete them at the next
    // tick, once the new UI's first draw has built batches without them.
    for (void *tex : g_iconRegions)
        g_orphans.push_back(tex);
    g_fsIcons.clear();
    g_maintainDisabled = false; // re-arm the SEH latch for the fresh UI
}

static const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

} // namespace Text::InlineTexturePool
