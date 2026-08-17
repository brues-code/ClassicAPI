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

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_TEXTURE_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Texture::ColorTexture
