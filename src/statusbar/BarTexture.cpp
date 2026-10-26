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

// `bar:SetColorFill(r, g, b [, a])` and the bar texture's desaturation:
// `SetStatusBarDesaturated`, `IsStatusBarDesaturated`,
// `SetStatusBarDesaturation`, `GetStatusBarDesaturation`.
//
// SetColorFill fills the bar texture with a solid colour. A bar with no
// texture gets one the way SetStatusBarTexture-by-path (0x00783140) makes it -
// a pooled CSimpleTexture in ARTWORK, pinned to every point of the bar and
// installed with FUN_STATUSBAR_INSTALL_BAR_TEXTURE - less the file. The colour
// goes through the engine's own Texture:SetTexture handler with the texture as
// self, whose numeric form is the solid fill (the same reuse as
// Texture:SetColorTexture), so the clamp and opaque default alpha are the
// engine's.
//
// The desaturation methods act on the bar texture through Texture::
// Desaturation; a bar with no texture reports 0 / false.

#include "Game.h"
#include "Offsets.h"
#include "frame/Region.h"
#include "texture/Desaturation.h"

namespace StatusBar::BarTexture {

namespace {

using SetAllPoints_t = void(__thiscall *)(void *anchor, void *relative, int doResize);
using Install_t = void(__thiscall *)(void *bar, void *texture, int drawLayer);

void *BarTextureOf(void *bar) { return Game::Read<void *>(bar, Offsets::OFF_STATUSBAR_BAR_TEXTURE); }

void *CreateBarTexture(void *bar) {
    void *texture = Frame::Region::CreateTexture(nullptr, Offsets::DRAWLAYER_ARTWORK, 1);
    if (texture == nullptr)
        return nullptr;
    reinterpret_cast<SetAllPoints_t>(Offsets::FUN_LAYOUT_SET_ALL_POINTS)(
        Game::Ptr<uint8_t>(texture, Offsets::OFF_REGION_ANCHOR), Game::Ptr<uint8_t>(bar, Offsets::OFF_REGION_ANCHOR),
        1);
    reinterpret_cast<Install_t>(Offsets::FUN_STATUSBAR_INSTALL_BAR_TEXTURE)(bar, texture,
                                                                           Offsets::DRAWLAYER_ARTWORK);
    return texture;
}

void *ResolveBar(void *L) { return Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_STATUSBAR_LUA_TYPE_ID); }

int __fastcall Script_SetColorFill(void *L) {
    void *bar = ResolveBar(L);
    if (bar == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2) || !Game::Lua::IsNumber(L, 3) || !Game::Lua::IsNumber(L, 4)) {
        Game::Lua::Error(L, "Usage: %s:SetColorFill(r, g, b [, a])", Game::Lua::ObjectDisplayName(bar));
        return 0;
    }
    void *texture = BarTextureOf(bar);
    if (texture == nullptr)
        texture = CreateBarTexture(bar);
    if (texture == nullptr)
        return 0;
    Game::Lua::PushObject(L, texture);
    Game::Lua::Remove(L, 1);
    Game::Lua::Insert(L, 1);
    reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_TEXTURE_SET_TEXTURE)(L);
    return 0;
}

int __fastcall Script_SetStatusBarDesaturated(void *L) {
    void *bar = ResolveBar(L);
    if (bar == nullptr)
        return 0;
    if (void *texture = BarTextureOf(bar))
        Texture::Desaturation::SetAmount(texture, Game::Lua::ToBoolean(L, 2) != 0 ? 1.0 : 0.0);
    return 0;
}

int __fastcall Script_IsStatusBarDesaturated(void *L) {
    void *bar = ResolveBar(L);
    if (bar == nullptr)
        return 0;
    void *texture = BarTextureOf(bar);
    Game::Lua::PushBool(L, texture != nullptr && Texture::Desaturation::GetAmount(texture) > 0.0);
    return 1;
}

int __fastcall Script_SetStatusBarDesaturation(void *L) {
    void *bar = ResolveBar(L);
    if (bar == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:SetStatusBarDesaturation(desaturation)", Game::Lua::ObjectDisplayName(bar));
        return 0;
    }
    if (void *texture = BarTextureOf(bar))
        Texture::Desaturation::SetAmount(texture, Game::Lua::ToNumber(L, 2));
    return 0;
}

int __fastcall Script_GetStatusBarDesaturation(void *L) {
    void *bar = ResolveBar(L);
    if (bar == nullptr)
        return 0;
    void *texture = BarTextureOf(bar);
    Game::Lua::PushNumber(L, texture != nullptr ? Texture::Desaturation::GetAmount(texture) : 0.0);
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetColorFill", &Script_SetColorFill},
    {"SetStatusBarDesaturated", &Script_SetStatusBarDesaturated},
    {"IsStatusBarDesaturated", &Script_IsStatusBarDesaturated},
    {"SetStatusBarDesaturation", &Script_SetStatusBarDesaturation},
    {"GetStatusBarDesaturation", &Script_GetStatusBarDesaturation},
};

const Game::Doc::Field kSetColorFillArgs[] = {
    Game::Doc::Req("colorR", "number"),
    Game::Doc::Req("colorG", "number"),
    Game::Doc::Req("colorB", "number"),
    Game::Doc::Opt("a", "SingleColorValue"),
};
const Game::Doc::Field kSetDesaturatedArgs[] = {Game::Doc::Opt("desaturated", "bool", "false")};
const Game::Doc::Field kIsDesaturatedRets[] = {Game::Doc::Req("desaturated", "bool")};
const Game::Doc::Field kSetDesaturationArgs[] = {Game::Doc::Req("desaturation", "normalizedValue")};
const Game::Doc::Field kGetDesaturationRets[] = {Game::Doc::Req("desaturation", "normalizedValue")};

const Game::Doc::Function kSetColorFill{"Fills the bar texture with a solid colour, creating the texture if the bar has none.",
                                        kSetColorFillArgs, {}};
const Game::Doc::Function kSetDesaturated{"Sets whether the bar texture is drawn fully desaturated.",
                                          kSetDesaturatedArgs, {}};
const Game::Doc::Function kIsDesaturated{"Returns whether the bar texture is desaturated at all.", {},
                                         kIsDesaturatedRets};
const Game::Doc::Function kSetDesaturation{"Sets how desaturated the bar texture is drawn, from 0 to 1.",
                                           kSetDesaturationArgs, {}};
const Game::Doc::Function kGetDesaturation{"Returns how desaturated the bar texture is drawn, from 0 to 1.", {},
                                           kGetDesaturationRets};

const Game::Doc::Method g_methodDocs[] = {
    {"SetColorFill", &kSetColorFill},
    {"SetStatusBarDesaturated", &kSetDesaturated},
    {"IsStatusBarDesaturated", &kIsDesaturated},
    {"SetStatusBarDesaturation", &kSetDesaturation},
    {"GetStatusBarDesaturation", &kGetDesaturation},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_STATUSBAR_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace StatusBar::BarTexture
