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

// `texture:SetSnapToPixelGrid(snap)` / `texture:SetTexelSnappingBias(bias)`:
// retail's switches for moving a texture's edges and texels onto whole screen
// pixels. Code turns them off for art that must not shift, such as
// NineSlice's DisableSharpening and textures that animate.
//
// 1.12 draws every texture where its rect falls, with no snapping to turn off,
// so both are accepted and ignored: what they turn off is already off. No
// getter is registered; it would report a snapping state this client does not
// have.

#include "Game.h"
#include "Offsets.h"

namespace Texture::Snapping {

namespace {

int __fastcall Script_Ignored(void * /*L*/) { return 0; }

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetSnapToPixelGrid", &Script_Ignored},
    {"SetTexelSnappingBias", &Script_Ignored},
};

const Game::Doc::Field kSnapArgs[] = {
    Game::Doc::Req("snap", "bool", "Ignored: this client does not snap textures."),
};
const Game::Doc::Function kSetSnapToPixelGrid{
    "Sets whether the texture snaps to whole screen pixels. Accepted and ignored.", kSnapArgs, {}};

const Game::Doc::Field kBiasArgs[] = {
    Game::Doc::Req("bias", "normalizedValue", "Ignored: this client does not snap textures."),
};
const Game::Doc::Function kSetTexelSnappingBias{
    "Sets how strongly the texture's texels snap to screen pixels. Accepted and ignored.",
    kBiasArgs, {}};

const Game::Doc::Method g_methodDocs[] = {
    {"SetSnapToPixelGrid", &kSetSnapToPixelGrid},
    {"SetTexelSnappingBias", &kSetTexelSnappingBias},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_TEXTURE_METHOD_REGISTRY), g_methods, g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Texture::Snapping
