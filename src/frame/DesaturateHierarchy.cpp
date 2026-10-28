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


// `frame:DesaturateHierarchy(desaturation [, excludeRoot])`: the amount
// texture:SetDesaturation takes, set on every texture of the frame and of each
// frame under it, through Texture::Desaturation. With excludeRoot the frame's
// own textures are left. The walk is the one the engine's alpha setter makes:
// OFF_FRAME_REGION_LIST holds every region, shown or not, and
// OFF_FRAME_CHILD_LIST every child frame.

#include "Game.h"
#include "Offsets.h"
#include "texture/Desaturation.h"

namespace Frame::DesaturateHierarchy {

namespace {

void Apply(void *frame, double amount, bool regions) {
    if (regions)
        Game::ForEachFrameListObject(frame, Offsets::OFF_FRAME_REGION_LIST, [amount](void *region) {
            if (Game::Lua::IsA(region, Offsets::VAR_TEXTURE_LUA_TYPE_ID))
                Texture::Desaturation::SetAmount(region, amount);
        });
    Game::ForEachFrameListObject(frame, Offsets::OFF_FRAME_CHILD_LIST,
                                 [amount](void *child) { Apply(child, amount, true); });
}

int __fastcall Script_DesaturateHierarchy(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:DesaturateHierarchy(desaturation [, excludeRoot])",
                         Game::Lua::ObjectDisplayName(frame));
        return 0;
    }
    Apply(frame, Game::Lua::ToNumber(L, 2), !Game::Lua::ToFlag(L, 3));
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"DesaturateHierarchy", &Script_DesaturateHierarchy},
};

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("desaturation", "number", "From 0 (full colour) to 1 (grey)."),
    Game::Doc::Opt("excludeRoot", "bool", "false", "When true, the frame's own textures are left unchanged."),
};
const Game::Doc::Function kDesaturateHierarchy{
    "Sets the desaturation of every texture of the frame and of all frames under it.", kArgs, {}};

const Game::Doc::Method g_methodDocs[] = {
    {"DesaturateHierarchy", &kDesaturateHierarchy},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::DesaturateHierarchy
