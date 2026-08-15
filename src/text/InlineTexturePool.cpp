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

// CSimpleTexture pool backing inline-|T icons — the 4.3.4 CSimpleEmbeddedTexture
// model. See the header for the architecture and docs/InlineTextureResidency.md
// for the elimination trail that led here.
//
// Anchoring: each icon gets its TOPLEFT and BOTTOMRIGHT anchored to the OWNING
// fontstring's TOPLEFT at pixel offsets converted with the engine's own
// Script_SetPoint scale (PixelToInternal — anchor y is up-positive, so the
// owner-local y-down offsets are negated). Corner realize order matters: the
// renderer draws the corner array (+0xD4), which SetTexCoord stores from the
// rect — so Realize() must run between the anchors and SetTexCoord (see
// FUN_REGION_LAYOUT_REALIZE in Offsets.h).

#include "text/InlineTexturePool.h"

#include "Game.h"
#include "Offsets.h"

#include <string>
#include <vector>

namespace Text::InlineTexturePool {

namespace {

constexpr int kPoolSize = 128;

using PoolAlloc_t = void *(__thiscall *)(void *pool, int zeroInit, const char *tag, int line);
using TexCtor_t = void *(__thiscall *)(void *mem, void *parent, int layer, int sublayer);
using SetTexture_t = uint32_t(__thiscall *)(void *tex, const char *path, int a, uint32_t blend,
                                            int b);
using SetTexCoord_t = void(__thiscall *)(void *tex, const float *ltrb);
using SetColor_t = void(__thiscall *)(void *tex, const void *colorBGRA);
using SetPoint_t = void(__thiscall *)(void *anchor, int point, void *relAnchor, int relPoint,
                                      float x, float y, int flag);
using SetParentAndLayer_t = void(__thiscall *)(void *region, void *parent, int layer, int show);
using Realize_t = void(__thiscall *)(void *anchor, int flag);
using ShowHide_t = void(__fastcall *)(void *tex);

bool LooksReadable(const void *p) {
    auto a = reinterpret_cast<uintptr_t>(p);
    return a >= 0x00010000u && a < 0x7FFF0000u;
}

// One icon's target state for this pass, plus the per-slot cache of what was
// last APPLIED (so an unchanged icon costs zero engine calls).
struct Desired {
    void *owner = nullptr;
    std::string path;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    uint32_t color = 0;
};

struct Slot {
    void *tex = nullptr;
    void *owner = nullptr;
    void *parent = nullptr;
    bool shown = false;
    bool valid = false;
    std::string path;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    uint32_t color = 0;
};

void *g_uiParent = nullptr;
std::vector<Slot> g_pool;
std::vector<Desired> g_desired;
int g_desiredCount = 0;

// --- engine primitive wrappers ----------------------------------------------

// Show/Hide with the desired-shown flag (+0xC4) written first — Show NO-OPS
// without it, and SetParentAndLayer(show=0) clears it on every reparent (which
// silently blanked every icon until this was mirrored from the engine's own
// callers, e.g. the message frame's per-line show/hide in FUN_00788750).
void Show(void *tex) {
    *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(tex) +
                                  Offsets::OFF_REGION_DESIRED_SHOWN) = 1;
    reinterpret_cast<ShowHide_t>(Offsets::FUN_FONTSTRING_SHOW)(tex);
}
void Hide(void *tex) {
    *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(tex) +
                                  Offsets::OFF_REGION_DESIRED_SHOWN) = 0;
    reinterpret_cast<ShowHide_t>(Offsets::FUN_FONTSTRING_HIDE)(tex);
}

void SetTexture(void *tex, const char *path) {
    const uint32_t blend = *reinterpret_cast<uint32_t *>(Offsets::VAR_TEXTURE_BLEND_DEFAULT);
    reinterpret_cast<SetTexture_t>(Offsets::FUN_SIMPLETEXTURE_SET_TEXTURE)(tex, path, 0, blend, 0);
}

void SetTexCoord(void *tex, float v0, float u0, float v1, float u1) {
    const float ltrb[4] = {v0, u0, v1, u1};
    reinterpret_cast<SetTexCoord_t>(Offsets::FUN_SIMPLETEXTURE_SET_TEXCOORD)(tex, ltrb);
}

void SetVertexColor(void *tex, uint32_t color) {
    reinterpret_cast<SetColor_t>(Offsets::FUN_FONTSTRING_SET_COLOR)(tex, &color);
}

// Anchor `tex`'s `point` to `owner`'s BOTTOMLEFT at internal offsets (ox, oy).
// The pen coordinates the emitter records are y-down with the origin at the
// owning fontstring's BOTTOM edge (the node position in RebuildString derives
// from rect[0], the bottom in the y-up rect) — a line's pen y is ~-fontH.
void AnchorToOwner(void *tex, int point, void *owner, float ox, float oy) {
    auto setPoint = reinterpret_cast<SetPoint_t>(Offsets::FUN_REGION_SET_POINT);
    setPoint(reinterpret_cast<uint8_t *>(tex) + Offsets::OFF_REGION_ANCHOR, point,
             reinterpret_cast<uint8_t *>(owner) + Offsets::OFF_REGION_ANCHOR,
             Offsets::FRAMEPOINT_BOTTOMLEFT, ox, oy, 1);
}

void Realize(void *tex) {
    reinterpret_cast<Realize_t>(Offsets::FUN_REGION_LAYOUT_REALIZE)(
        reinterpret_cast<uint8_t *>(tex) + Offsets::OFF_REGION_ANCHOR, 0);
}

// The engine's own reparent (unlinks from the old parent's region registry,
// inserts into the new one). show=0 — visibility is applied separately.
void Reparent(void *tex, void *parent) {
    reinterpret_cast<SetParentAndLayer_t>(Offsets::FUN_REGION_SET_PARENT_AND_LAYER)(
        tex, parent, Offsets::DRAWLAYER_ARTWORK, 0);
}

void *CreateTexture() {
    auto alloc = reinterpret_cast<PoolAlloc_t>(Offsets::FUN_REGION_POOL_ALLOC);
    void *mem = alloc(reinterpret_cast<void *>(Offsets::VAR_SIMPLETEXTURE_POOL), 0,
                      reinterpret_cast<const char *>(Offsets::VAR_SIMPLETEXTURE_CLASS_TAG), -2);
    if (mem == nullptr)
        return nullptr;
    auto ctor = reinterpret_cast<TexCtor_t>(Offsets::FUN_SIMPLETEXTURE_CTOR);
    void *tex = ctor(mem, g_uiParent, Offsets::DRAWLAYER_ARTWORK, 1);
    if (tex == nullptr)
        return nullptr;
    Hide(tex);
    return tex;
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

// UI-pixel → internal anchor-offset conversion — the exact scale Script_SetPoint
// applies (same formula as LinePool::PixelToInternal; verified by a Lua-created
// control texture rendering at the identical corner values). Returns 0 while
// the scale globals are unpopulated (start of boot) — icons just defer a tick.
float PixelToInternal(float pixels) {
    const float mul = *reinterpret_cast<const float *>(Offsets::VAR_UI_COORD_SCALE_MUL);
    const float divBase = *reinterpret_cast<const float *>(Offsets::VAR_UI_COORD_SCALE_DIV);
    const float denom = divBase * static_cast<float>(Offsets::UI_COORD_SCALE_UNIT);
    if (denom == 0.0f)
        return 0.0f;
    return pixels * mul / denom;
}

bool ScaleReady() { return PixelToInternal(1.0f) != 0.0f; }

// Applies one desired entry to one slot.
void ApplySlot(Slot &s, const Desired &d) {
    // Mirror the owner's shown state: a hidden/faded-out line hides its icons.
    const bool ownerShown =
        *reinterpret_cast<const uint32_t *>(reinterpret_cast<const uint8_t *>(d.owner) +
                                            Offsets::OFF_REGION_ACTUALLY_SHOWN) != 0;

    const bool same = s.valid && s.owner == d.owner && s.x0 == d.x0 && s.y0 == d.y0 &&
                      s.x1 == d.x1 && s.y1 == d.y1 && s.u0 == d.u0 && s.v0 == d.v0 &&
                      s.u1 == d.u1 && s.v1 == d.v1 && s.color == d.color && s.path == d.path;
    if (!same) {
        void *parent = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(d.owner) +
                                                  Offsets::OFF_REGION_PARENT);
        if (parent != nullptr && s.parent != parent) {
            Reparent(s.tex, parent);
            s.parent = parent;
            s.shown = false; // Reparent hides; visibility re-applied below
        }
        if (s.path != d.path) {
            SetTexture(s.tex, d.path.c_str());
            s.path = d.path;
        }
        // Owner-local px (y down) → anchor offsets (y up), rel owner TOPLEFT.
        AnchorToOwner(s.tex, Offsets::FRAMEPOINT_TOPLEFT, d.owner, PixelToInternal(d.x0),
                      -PixelToInternal(d.y0));
        AnchorToOwner(s.tex, Offsets::FRAMEPOINT_BOTTOMRIGHT, d.owner, PixelToInternal(d.x1),
                      -PixelToInternal(d.y1));
        Realize(s.tex);
        SetTexCoord(s.tex, d.v0, d.u0, d.v1, d.u1);
        SetVertexColor(s.tex, d.color);
        s.owner = d.owner;
        s.x0 = d.x0;
        s.y0 = d.y0;
        s.x1 = d.x1;
        s.y1 = d.y1;
        s.u0 = d.u0;
        s.v0 = d.v0;
        s.u1 = d.u1;
        s.v1 = d.v1;
        s.color = d.color;
        s.valid = true;
    }
    if (ownerShown != s.shown) {
        if (ownerShown)
            Show(s.tex);
        else
            Hide(s.tex);
        s.shown = ownerShown;
    }
}

} // namespace

bool PlaceOwned(void *ownerFs, float x0, float y0, float x1, float y1, const char *path, float u0,
                float v0, float u1, float v1, uint32_t color) {
    if (ownerFs == nullptr || path == nullptr || path[0] == '\0')
        return false;
    if (g_desiredCount >= kPoolSize)
        return false;
    if (g_desired.empty())
        g_desired.resize(kPoolSize);
    Desired &d = g_desired[g_desiredCount++];
    d.owner = ownerFs;
    d.path.assign(path);
    d.x0 = x0;
    d.y0 = y0;
    d.x1 = x1;
    d.y1 = y1;
    d.u0 = u0;
    d.v0 = v0;
    d.u1 = u1;
    d.v1 = v1;
    d.color = color;
    return true;
}

void ApplyPass() {
    if (!ScaleReady()) {
        g_desiredCount = 0;
        return;
    }
    if (g_uiParent == nullptr) {
        g_uiParent = ResolveUIParent();
        if (g_uiParent == nullptr) {
            g_desiredCount = 0;
            return;
        }
    }
    if (g_pool.empty()) {
        g_pool.reserve(kPoolSize);
        for (int i = 0; i < kPoolSize; ++i) {
            void *tex = CreateTexture();
            if (tex == nullptr)
                break;
            Slot s;
            s.tex = tex;
            g_pool.push_back(std::move(s));
        }
    }
    const int poolCount = static_cast<int>(g_pool.size());
    const int n = g_desiredCount < poolCount ? g_desiredCount : poolCount;
    for (int i = 0; i < n; ++i)
        ApplySlot(g_pool[i], g_desired[i]);
    for (int i = n; i < poolCount; ++i) {
        if (g_pool[i].shown) {
            Hide(g_pool[i].tex);
            g_pool[i].shown = false;
        }
    }
    g_desiredCount = 0;
}

void PrepareForReload() {
    // The engine's reload teardown frees these objects (regions die with their
    // frames); only FORGET the pointers here — never Hide/free them.
    g_pool.clear();
    g_desiredCount = 0;
    g_uiParent = nullptr;
}

int PoolReady() { return ScaleReady() && !g_pool.empty() ? 1 : 0; }
int PoolSize() { return static_cast<int>(g_pool.size()); }
int ShownCount() {
    int n = 0;
    for (const Slot &s : g_pool)
        if (s.shown)
            ++n;
    return n;
}

namespace {

// _classicapi_InlineTexPool() -> ready, poolSize, shownCount.
int __fastcall Script_InlineTexPool(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(PoolReady()));
    Game::Lua::PushNumber(L, static_cast<double>(PoolSize()));
    Game::Lua::PushNumber(L, static_cast<double>(ShownCount()));
    return 3;
}

// _classicapi_InlineTexPoolSlot(i | "pathSubstr") -> shown, owner, x0, y0, x1,
// y1 (applied owner-local px), rc0..rc3 (the icon's raw engine rect at +0x64),
// orc0..orc3 (the OWNER's raw rect — for expected-vs-actual position math),
// path. A string argument finds the first applied slot whose path contains it.
int __fastcall Script_InlineTexPoolSlot(void *L) {
    int idx = -1;
    if (Game::Lua::IsNumber(L, 1)) {
        idx = static_cast<int>(Game::Lua::ToNumber(L, 1));
    } else if (Game::Lua::IsString(L, 1)) {
        const char *needle = Game::Lua::ToString(L, 1);
        for (int i = 0; i < static_cast<int>(g_pool.size()); ++i) {
            if (g_pool[i].valid && g_pool[i].path.find(needle) != std::string::npos) {
                idx = i;
                break;
            }
        }
    } else {
        idx = 0;
    }
    if (idx < 0 || idx >= static_cast<int>(g_pool.size())) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const Slot &s = g_pool[idx];
    Game::Lua::PushNumber(L, static_cast<double>(s.shown ? 1 : 0));
    Game::Lua::PushNumber(L, static_cast<double>(reinterpret_cast<uintptr_t>(s.owner)));
    Game::Lua::PushNumber(L, static_cast<double>(s.x0));
    Game::Lua::PushNumber(L, static_cast<double>(s.y0));
    Game::Lua::PushNumber(L, static_cast<double>(s.x1));
    Game::Lua::PushNumber(L, static_cast<double>(s.y1));
    const float *rc = reinterpret_cast<const float *>(reinterpret_cast<uint8_t *>(s.tex) +
                                                      Offsets::OFF_REGION_RECT);
    for (int k = 0; k < 4; ++k)
        Game::Lua::PushNumber(L, static_cast<double>(rc[k]));
    if (LooksReadable(s.owner)) {
        const float *orc = reinterpret_cast<const float *>(reinterpret_cast<uint8_t *>(s.owner) +
                                                           Offsets::OFF_REGION_RECT);
        for (int k = 0; k < 4; ++k)
            Game::Lua::PushNumber(L, static_cast<double>(orc[k]));
    } else {
        for (int k = 0; k < 4; ++k)
            Game::Lua::PushNumber(L, 0.0);
    }
    Game::Lua::PushString(L, s.path.c_str());
    return 15;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexPool", &Script_InlineTexPool);
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexPoolSlot", &Script_InlineTexPoolSlot);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Text::InlineTexturePool
