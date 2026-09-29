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

// `GameTooltip:AddTexture(texture)` — puts a texture at the start of the
// tooltip's last line, as on 3.x clients.
//
// Vanilla already has the whole feature, unexposed: GameTooltipTemplate
// declares `<name>Texture1..3`, the tooltip binds them at load, a routine in
// the engine lays one out on the last line (FUN_GAMETOOLTIP_ADD_TEXTURE), and
// the tooltip clear undoes it. Nothing in the 1.12 binary calls that routine;
// this method is its Lua entry point, so the layout, the text shift and the
// clear are the engine's own.
//
// The engine's limits apply: a tooltip holds three textures (the 3.x
// template declares ten), and the routine needs at least two lines, so the
// header line never takes one. Past either limit the call does nothing.

#include "Game.h"
#include "Offsets.h"

namespace Tooltip::AddTexture {

namespace {

using AddTexture_t = void(__thiscall *)(void *tooltip, const char *texturePath);

int __fastcall Script_GameTooltipAddTexture(void *L) {
    void *self = Game::Lua::ResolveTooltip(L);
    if (self == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: GameTooltip:AddTexture(\"texture\")");
        return 0;
    }
    reinterpret_cast<AddTexture_t>(Offsets::FUN_GAMETOOLTIP_ADD_TEXTURE)(
        self, Game::Lua::ToString(L, 2));
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"AddTexture", &Script_GameTooltipAddTexture},
};

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("texture", "string", "Texture path, e.g. \"Interface\\\\Icons\\\\INV_Misc_Bag_08\"."),
};
const Game::Doc::Function kAddTexture{
    "Puts a texture at the start of the last line. Up to three per tooltip; not on the first line.",
    kArgs, {}};

const Game::Doc::Method g_methodDocs[] = {
    {"AddTexture", &kAddTexture},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_GAMETOOLTIP_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Tooltip::AddTexture
