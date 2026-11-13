// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// Texture:SetColorTexture(r, g, b [, a]) — a 7.0 method that fills a texture with
// a solid colour. 1.12 already does exactly this through the NUMERIC form of
// SetTexture: pass colour components instead of a path and the engine's
// Script_Texture_SetTexture handler clamps r/g/b/a to [0,1] and calls the
// solid-colour fill (see Offsets::FUN_SCRIPT_TEXTURE_SET_TEXTURE). So
// SetColorTexture is just a second name on that same engine handler — aliasing it
// gives exact vanilla behaviour (clamping + opaque-alpha default) with no
// reimplementation. (The alias also accepts a path string, a harmless superset of
// the retail contract.)

#include "Game.h"
#include "Offsets.h"

namespace Texture::ColorTexture {
namespace {

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetColorTexture",
     reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_TEXTURE_SET_TEXTURE)},
};

const Game::Doc::Field kSetColorTextureArgs[] = {
    Game::Doc::Req("colorR", "number", "From 0 to 1; values outside are clamped."),
    Game::Doc::Req("colorG", "number"),
    Game::Doc::Req("colorB", "number"),
    Game::Doc::Opt("a", "number", "1"),
};
const Game::Doc::Field kSetColorTextureRets[] = {
    Game::Doc::Req("success", "number", "Always 1."),
};
const Game::Doc::Function kSetColorTexture{"Fills the texture with a solid color.",
                                           kSetColorTextureArgs, kSetColorTextureRets};

const Game::Doc::Method g_methodDocs[] = {
    {"SetColorTexture", &kSetColorTexture},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_TEXTURE_METHOD_REGISTRY), g_methods, g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Texture::ColorTexture
