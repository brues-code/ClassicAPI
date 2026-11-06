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

// `texture:SetTexture(file, wrapModeHorizontal, wrapModeVertical)`, the XML
// `hWrapMode` / `vWrapMode` attributes, and the repeat a tiled axis needs
// (Texture::Tile).
//
// 1.12 fixes a texture's wrap when its file loads. CSimpleTexture::SetTexture
// (FUN_SIMPLETEXTURE_SET_TEXTURE) builds the texture flags word with ONE wrap
// argument for both axes, and TextureCreate caches the handle under the name
// and that word, so a repeating copy of a file is its own shared texture. The
// device samples with the word's two wrap bits, repeat or clamp per axis
// (FUN_GX_TEXFLAGS_INIT). Its callers pass 0, except the XML texture loader,
// which passes 1 when a <TexCoords> value lies outside 0..1.
//
// A texture with a mode or a tile flag loads through a co-hook on that
// SetTexture: its own body, with a repeat per axis built into the flags word -
// the engine's wrap argument, the mode, or a tile flag. The engine's same-file
// early-out (FUN_TEXTURE_NAME_MATCH) ignores flags, so the co-hook also
// compares the held image's wrap, and for the same file with another wrap it
// swaps in the other copy and keeps the pixel shader the early-out would have
// kept. A change of mode or tiling reloads the held file the same way.
//
// The wrap arguments follow retail: true or "REPEAT" repeats, and anything
// else clamps, nil included, so SetTexture(file) clamps again. The device has
// no mirror or border mode, so "MIRROR", "CLAMPTOBLACK", "CLAMPTOBLACKADDITIVE"
// and "CLAMPTOWHITE" clamp. Tile flags survive SetTexture, as 4.3.4's do;
// SetAtlas clears modes and tiling (Clear), and while a texture shows an atlas
// its image keeps the atlas's clamp: a reload would replace the image the
// atlas binding is tied to.

#include "texture/Wrap.h"

#include "Game.h"
#include "Offsets.h"
#include "texture/Atlas.h"
#include "texture/Construct.h"
#include "texture/Load.h"
#include "xml/FrameLoad.h"
#include "xml/TypeLoad.h"

#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace Texture::Wrap {

namespace {

struct State {
    bool baseH = false, baseV = false; // the engine's own wrap argument for the image
    bool modeH = false, modeV = false;
    bool tileH = false, tileV = false;

    bool Any() const { return modeH || modeV || tileH || tileV; }
    bool RepeatH() const { return baseH || modeH || tileH; }
    bool RepeatV() const { return baseV || modeV || tileV; }
};

std::unordered_map<const void *, State> g_state;

constexpr uint32_t kWrapBits = Offsets::GXTEX_FLAG_WRAP_U | Offsets::GXTEX_FLAG_WRAP_V;

using SetTexture_t = uint32_t(__fastcall *)(void *texture, void *edx, const char *name, uint32_t wrap,
                                            uint32_t filter, uint32_t shader);
using NameMatch_t = int(__fastcall *)(void *hTexture, const char *name);
using GetUiPixelShader_t = void *(__fastcall *)(uint32_t index);
using NotifyLayerChanged_t = void(__fastcall *)(void *region);

SetTexture_t g_origSetTexture = nullptr;

void *HeldImage(const void *texture) { return Game::Read<void *>(texture, Offsets::OFF_SIMPLETEXTURE_HTEXTURE); }

// The flags word the held image samples with: its device texture's, or for a
// BLP still loading, the word the handle was made with.
uint32_t HeldFlags(void *image) {
    if (const void *device = Game::Read<void *>(image, Offsets::OFF_HTEXTURE_DEVICE))
        return Game::Read<uint32_t>(device, Offsets::OFF_GXTEX_FLAGS);
    return Game::Read<uint32_t>(image, Offsets::OFF_HTEXTURE_TEX_FLAGS);
}

// CSimpleTexture::SetTexture with a repeat per axis, and a same-file
// early-out that also compares the wrap. An empty name is the engine's clear.
uint32_t Load(void *texture, const char *name, bool repeatH, bool repeatV, uint32_t filter, uint32_t shader) {
    if (name == nullptr || *name == '\0')
        return g_origSetTexture(texture, nullptr, name, 0, filter, shader);
    void *held = HeldImage(texture);
    const uint32_t flags = Texture::Flags(filter, repeatH, repeatV);
    const bool sameFile =
        held != nullptr && reinterpret_cast<NameMatch_t>(Offsets::FUN_TEXTURE_NAME_MATCH)(held, name) != 0;
    if (sameFile && (HeldFlags(held) & kWrapBits) == (flags & kWrapBits))
        return 1;
    void *image = Texture::LoadByPath(name, flags);
    if (image == nullptr)
        return 0;
    if (!sameFile)
        Game::Ref<void *>(texture, Offsets::OFF_SIMPLETEXTURE_SHADER) =
            reinterpret_cast<GetUiPixelShader_t>(Offsets::FUN_UI_GET_PIXEL_SHADER)(shader);
    Texture::ReleaseHandle(held);
    Game::Ref<void *>(texture, Offsets::OFF_SIMPLETEXTURE_HTEXTURE) = image;
    reinterpret_cast<NotifyLayerChanged_t>(Offsets::FUN_REGION_NOTIFY_LAYER_CHANGED)(texture);
    return 1;
}

uint32_t __fastcall SetTexture_h(void *texture, void *edx, const char *name, uint32_t wrap, uint32_t filter,
                                 uint32_t shader) {
    const auto it = g_state.find(texture);
    if (it == g_state.end())
        return g_origSetTexture(texture, edx, name, wrap, filter, shader);
    State next = it->second;
    next.baseH = next.baseV = wrap != 0;
    const uint32_t loaded = Load(texture, name, next.RepeatH(), next.RepeatV(), filter, shader);
    if (loaded != 0)
        it->second = next;
    return loaded;
}

const Game::HookAutoRegister _setTextureHook{Offsets::FUN_SIMPLETEXTURE_SET_TEXTURE,
                                             reinterpret_cast<void *>(&SetTexture_h),
                                             reinterpret_cast<void **>(&g_origSetTexture)};

// The held file again with the state's wrap. A solid colour or a pixel source
// has no file to load, and an atlas keeps its image (see the header).
void Reconcile(void *texture, const State &state) {
    void *held = HeldImage(texture);
    if (held == nullptr ||
        (Game::Read<uint32_t>(held, Offsets::OFF_HTEXTURE_SOURCE) & Offsets::HTEXTURE_SOURCE_FILE) == 0 ||
        Texture::Atlas::Shows(texture))
        return;
    char name[0x104];
    std::memcpy(name, Game::Ptr<const char>(held, Offsets::OFF_HTEXTURE_NAME), sizeof(name));
    name[sizeof(name) - 1] = '\0';
    Load(texture, name, state.RepeatH(), state.RepeatV(), HeldFlags(held) & Offsets::GXTEX_FLAG_FILTER_MASK, 0);
}

// The texture's state, made on first use with the held image's wrap as the
// engine's own: without state, the engine loaded it.
State &Touch(void *texture) {
    const auto it = g_state.find(texture);
    if (it != g_state.end())
        return it->second;
    State state;
    if (void *held = HeldImage(texture)) {
        const uint32_t flags = HeldFlags(held);
        state.baseH = (flags & Offsets::GXTEX_FLAG_WRAP_U) != 0;
        state.baseV = (flags & Offsets::GXTEX_FLAG_WRAP_V) != 0;
    }
    return g_state.emplace(texture, state).first->second;
}

// Retail's wrap mode argument: true or "REPEAT" repeats; nil, false and the
// other modes clamp.
bool RepeatsArg(void *L, int index) {
    if (Game::Lua::Type(L, index) == Game::Lua::TYPE_STRING)
        return Game::StrCmpI(Game::Lua::ToString(L, index), "REPEAT") == 0;
    return Game::Lua::ToBoolean(L, index) != 0;
}

// texture:SetTexture(file[, wrapModeHorizontal, wrapModeVertical]): the engine
// method, with the modes stored first so its load takes them. The colour form
// keeps the modes.
int __fastcall Script_SetTexture(void *L) {
    void *texture = Game::Lua::IsNumber(L, 2) ? nullptr : Game::Lua::ResolveTexture(L, 1, false);
    const bool repeatH = texture != nullptr && RepeatsArg(L, 3);
    const bool repeatV = texture != nullptr && RepeatsArg(L, 4);
    if (texture != nullptr && (repeatH || repeatV || g_state.count(texture) != 0)) {
        State &state = Touch(texture);
        state.modeH = repeatH;
        state.modeV = repeatV;
    }
    const int results = reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_TEXTURE_SET_TEXTURE)(L);
    if (texture != nullptr)
        Settle(texture); // a file that did not load leaves the old image to match
    return results;
}

// The value of a wrap mode attribute, warning on a name retail does not have.
bool RepeatsAttr(const char *value, const void *node, void *status) {
    static const char *const kClamps[] = {"CLAMP", "CLAMPTOBLACK", "CLAMPTOBLACKADDITIVE", "CLAMPTOWHITE", "MIRROR"};
    if (Game::StrCmpI(value, "REPEAT") == 0)
        return true;
    for (const char *mode : kClamps)
        if (Game::StrCmpI(value, mode) == 0)
            return false;
    Xml::Warn(status, "Unknown wrap mode %s in element %s", value, Xml::Tag(node));
    return false;
}

// Retail's `hWrapMode` / `vWrapMode`, applied when present and non-empty.
void OnTextureNode(void *object, const void *node, void *status) {
    const char *h = Xml::Attr(node, "hWrapMode");
    const char *v = Xml::Attr(node, "vWrapMode");
    const bool hasH = h != nullptr && *h != '\0';
    const bool hasV = v != nullptr && *v != '\0';
    if (!hasH && !hasV)
        return;
    State &state = Touch(object);
    if (hasH)
        state.modeH = RepeatsAttr(h, node, status);
    if (hasV)
        state.modeV = RepeatsAttr(v, node, status);
    Settle(object);
}

const Xml::TypeLoad::AutoSubscribe _xmlSub{Xml::TypeLoad::TEXTURE, &OnTextureNode};

void OnTextureConstructed(void *region) { g_state.erase(region); }

const Texture::Construct::AutoSubscribe _constructSub{&OnTextureConstructed};

void PrepareForReload() { g_state.clear(); }

const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetTexture", &Script_SetTexture},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_TEXTURE_METHOD_REGISTRY), g_methods);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

// The image back in step with the state, and the state dropped once it holds
// nothing beyond the engine's own wrap.
void Settle(void *texture) {
    const auto it = g_state.find(texture);
    if (it == g_state.end())
        return;
    const State state = it->second;
    Reconcile(texture, state);
    if (!state.Any())
        g_state.erase(texture);
}

void SetModes(void *texture, bool repeatH, bool repeatV) {
    State &state = Touch(texture);
    state.modeH = repeatH;
    state.modeV = repeatV;
    Settle(texture);
}

void SetTiles(void *texture, bool h, bool v) {
    State &state = Touch(texture);
    state.tileH = h;
    state.tileV = v;
}

bool Tiles(const void *texture, bool &h, bool &v) {
    h = v = false;
    if (g_state.empty())
        return false;
    const auto it = g_state.find(texture);
    if (it == g_state.end())
        return false;
    h = it->second.tileH;
    v = it->second.tileV;
    return h || v;
}

bool Tiles(const void *texture) {
    bool h, v;
    return Tiles(texture, h, v);
}

void Clear(void *texture) {
    if (g_state.find(texture) == g_state.end())
        return;
    State &state = g_state[texture];
    state.modeH = state.modeV = state.tileH = state.tileV = false;
    Settle(texture);
}

} // namespace Texture::Wrap
