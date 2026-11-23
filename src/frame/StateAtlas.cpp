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

// Atlas names in the button state-texture setters: Button:SetNormalTexture,
// SetPushedTexture, SetDisabledTexture, SetHighlightTexture and CheckButton:
// SetCheckedTexture, SetDisabledCheckedTexture take an asset that retail
// resolves as an atlas when the name is one (Blizzard_CategoryList.lua swaps
// its toggle with SetNormalTexture("common-button-dropdown-open")). 1.12's
// setters take a texture object or a file path, so an atlas name loaded as a
// missing file.
//
// The engine still does the work. For a bound atlas name the override puts the
// atlas's file in the name's place and calls the engine's own setter, which
// makes or reloads the state texture, lays it over the button and sets the
// blend mode exactly as for any path. Then it asks the engine's getter for that
// texture and runs texture:SetAtlas on it (Texture::Atlas::SetAtlas), which
// selects the sprite. Any other argument goes to the engine's setter as it is.
//
// Registered on the Button and CheckButton method registries over the
// engine's entries (the most recent registration wins; see Frame::ClickMask).

#include "Game.h"
#include "Offsets.h"
#include "texture/Atlas.h"

#include <cstdint>
#include <string>

namespace Frame::StateAtlas {

namespace {

int Set(void *L, uintptr_t setter, uintptr_t getter) {
    using namespace Game::Lua;
    const Texture::Atlas::Info *info =
        IsString(L, 2) ? Texture::Atlas::Find(ToString(L, 2)) : nullptr;
    if (info == nullptr)
        return CallScript(setter, L);

    // `info` points into the atlas registry, which none of these calls reach.
    PushString(L, info->file.c_str());
    Replace(L, 2);
    CallScript(setter, L);

    SetTop(L, 1); // (self)
    if (CallScript(getter, L) < 1 || Type(L, -1) != TYPE_TABLE) {
        SetTop(L, 0);
        return 0;
    }
    Remove(L, 1);                     // (texture)
    PushString(L, info->name.c_str()); // (texture, atlas)
    Texture::Atlas::SetAtlas(L);
    SetTop(L, 0);
    return 0;
}

int __fastcall Script_SetNormalTexture(void *L) {
    return Set(L, Offsets::FUN_SCRIPT_BUTTON_SET_NORMAL_TEXTURE,
               Offsets::FUN_SCRIPT_BUTTON_GET_NORMAL_TEXTURE);
}
int __fastcall Script_SetPushedTexture(void *L) {
    return Set(L, Offsets::FUN_SCRIPT_BUTTON_SET_PUSHED_TEXTURE,
               Offsets::FUN_SCRIPT_BUTTON_GET_PUSHED_TEXTURE);
}
int __fastcall Script_SetDisabledTexture(void *L) {
    return Set(L, Offsets::FUN_SCRIPT_BUTTON_SET_DISABLED_TEXTURE,
               Offsets::FUN_SCRIPT_BUTTON_GET_DISABLED_TEXTURE);
}
int __fastcall Script_SetHighlightTexture(void *L) {
    return Set(L, Offsets::FUN_SCRIPT_BUTTON_SET_HIGHLIGHT_TEXTURE,
               Offsets::FUN_SCRIPT_BUTTON_GET_HIGHLIGHT_TEXTURE);
}
int __fastcall Script_SetCheckedTexture(void *L) {
    return Set(L, Offsets::FUN_SCRIPT_CHECKBUTTON_SET_CHECKED_TEXTURE,
               Offsets::FUN_SCRIPT_CHECKBUTTON_GET_CHECKED_TEXTURE);
}
int __fastcall Script_SetDisabledCheckedTexture(void *L) {
    return Set(L, Offsets::FUN_SCRIPT_CHECKBUTTON_SET_DISABLED_CHECKED_TEXTURE,
               Offsets::FUN_SCRIPT_CHECKBUTTON_GET_DISABLED_CHECKED_TEXTURE);
}

const Game::Lua::FrameMethodEntry g_buttonMethods[] = {
    {"SetNormalTexture", &Script_SetNormalTexture},
    {"SetPushedTexture", &Script_SetPushedTexture},
    {"SetDisabledTexture", &Script_SetDisabledTexture},
    {"SetHighlightTexture", &Script_SetHighlightTexture},
};

const Game::Lua::FrameMethodEntry g_checkButtonMethods[] = {
    {"SetCheckedTexture", &Script_SetCheckedTexture},
    {"SetDisabledCheckedTexture", &Script_SetDisabledCheckedTexture},
};

const Game::Doc::Field kStateTextureArgs[] = {
    Game::Doc::Req("asset", "TextureAsset",
                   "A texture object, a file path, or the name of an atlas."),
};
const Game::Doc::Field kHighlightTextureArgs[] = {
    Game::Doc::Req("asset", "TextureAsset",
                   "A texture object, a file path, or the name of an atlas."),
    Game::Doc::Opt("blendMode", "BlendMode"),
};

const Game::Doc::Function kSetNormalTexture{
    "Sets the texture the button shows in its normal state.", kStateTextureArgs, {}};
const Game::Doc::Function kSetPushedTexture{
    "Sets the texture the button shows while it is pushed.", kStateTextureArgs, {}};
const Game::Doc::Function kSetDisabledTexture{
    "Sets the texture the button shows while it is disabled.", kStateTextureArgs, {}};
const Game::Doc::Function kSetHighlightTexture{
    "Sets the texture the button shows while the mouse is over it.", kHighlightTextureArgs,
    {}};
const Game::Doc::Function kSetCheckedTexture{
    "Sets the texture the check button shows while it is checked.", kStateTextureArgs, {}};
const Game::Doc::Function kSetDisabledCheckedTexture{
    "Sets the texture the check button shows while it is checked and disabled.",
    kStateTextureArgs, {}};

const Game::Doc::Method g_buttonMethodDocs[] = {
    {"SetNormalTexture", &kSetNormalTexture},
    {"SetPushedTexture", &kSetPushedTexture},
    {"SetDisabledTexture", &kSetDisabledTexture},
    {"SetHighlightTexture", &kSetHighlightTexture},
};

const Game::Doc::Method g_checkButtonMethodDocs[] = {
    {"SetCheckedTexture", &kSetCheckedTexture},
    {"SetDisabledCheckedTexture", &kSetDisabledCheckedTexture},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_BUTTON_METHOD_REGISTRY),
                                    g_buttonMethods, g_buttonMethodDocs);
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_CHECKBUTTON_METHOD_REGISTRY), g_checkButtonMethods,
        g_checkButtonMethodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::StateAtlas
