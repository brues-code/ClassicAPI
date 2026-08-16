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
// CSimpleTexture per texture path, anchored offscreen and drawn every frame, so
// the engine keeps that texture resident for the raw quads that render the
// actual (pixel-positioned) icons. We reuse the CSimpleTexture creation +
// configure primitives proven elsewhere (Tooltip::LinePool / the earlier region
// experiments): pool-alloc, ctor, SetTexture, one SetPoint, Realize, Show.

#include "text/InlineTexturePool.h"

#include "Game.h"
#include "Offsets.h"
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

void *g_uiParent = nullptr;
// path → holder texture (one per distinct path, created once, never touched again).
std::unordered_map<std::string, void *> g_holders;
// Paths recorded by the paint hook, awaiting holder creation on the next tick.
std::unordered_set<std::string> g_pending;

void Show(void *tex) {
    // Show no-ops unless the desired-shown flag (+0xC4) is set; engine callers
    // always write it first (see the region RE notes).
    *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(tex) +
                                  Offsets::OFF_REGION_DESIRED_SHOWN) = 1;
    reinterpret_cast<ShowHide_t>(Offsets::FUN_FONTSTRING_SHOW)(tex);
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

// Creates the managed CSimpleTexture holder for `path`: a 1px region anchored to
// UIParent's bottom-left corner, textured, realized and shown. It draws every
// frame (holding the texture resident) but is a single pixel in the corner —
// visually negligible, and never re-touched, so it can't churn or flicker.
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

    // Near-transparent so the corner holder is invisible, but a NON-zero alpha
    // keeps the engine actually drawing it (a fully-transparent region may be
    // culled → residency lost). Alpha 1/255 is imperceptible yet still a draw.
    const uint32_t holderColor = 0x01FFFFFFu;
    reinterpret_cast<SetColor_t>(Offsets::FUN_FONTSTRING_SET_COLOR)(tex, &holderColor);

    // Anchor a TINY box in the bottom-left corner. The anchor space is ~[0..1]
    // across the screen, so the offset must be small — 1.0 spanned the whole
    // screen (the "giant icon"). kHolderSize ≈ 2px.
    constexpr float kHolderSize = 0.002f;
    auto setPoint = reinterpret_cast<SetPoint_t>(Offsets::FUN_REGION_SET_POINT);
    void *texAnchor = reinterpret_cast<uint8_t *>(tex) + Offsets::OFF_REGION_ANCHOR;
    void *uiAnchor = reinterpret_cast<uint8_t *>(g_uiParent) + Offsets::OFF_REGION_ANCHOR;
    setPoint(texAnchor, Offsets::FRAMEPOINT_BOTTOMLEFT, uiAnchor, Offsets::FRAMEPOINT_BOTTOMLEFT,
             0.0f, 0.0f, 1);
    setPoint(texAnchor, Offsets::FRAMEPOINT_TOPRIGHT, uiAnchor, Offsets::FRAMEPOINT_BOTTOMLEFT,
             kHolderSize, kHolderSize, 1);
    reinterpret_cast<Realize_t>(Offsets::FUN_REGION_LAYOUT_REALIZE)(texAnchor, 0);
    Show(tex);
    return tex;
}

// WorldTick: create holders for any paths the paint recorded since last tick.
void Maintain() {
    if (g_pending.empty())
        return;
    if (g_uiParent == nullptr) {
        g_uiParent = ResolveUIParent();
        if (g_uiParent == nullptr)
            return;
    }
    for (const std::string &p : g_pending) {
        if (g_holders.find(p) != g_holders.end())
            continue;
        void *tex = CreateHolder(p.c_str());
        if (tex != nullptr)
            g_holders.emplace(p, tex);
    }
    g_pending.clear();
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

void PrepareForReload() {
    // The reload teardown frees the holder textures (regions die with UIParent);
    // only forget the pointers — never Hide/free them.
    g_holders.clear();
    g_pending.clear();
    g_uiParent = nullptr;
}

int HolderCount() { return static_cast<int>(g_holders.size()); }

namespace {

// _classicapi_InlineTexHolders() -> holderCount.
int __fastcall Script_InlineTexHolders(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(HolderCount()));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("_classicapi_InlineTexHolders", &Script_InlineTexHolders);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Text::InlineTexturePool
