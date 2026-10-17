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
// tooltip's last line, as on 3.x clients. Returns whether it placed one.
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
//
// One more limit is ours. The routine moves line N's text right by the
// texture's width and moves line N+1 back by the same amount — by
// re-anchoring, which REPLACES whatever offset the line had. So a second
// texture on the same line lands on the first (the text shift is replaced,
// not added to), and a texture on the line right below one undoes that
// line's correction (its text ends up two widths in). The call refuses both,
// leaving every texture on a line with no texture directly above it.
// Tooltip::Clear repairs the routine's other defect, in the clear.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Tooltip::AddTexture {

namespace {

using AddTexture_t = void(__thiscall *)(void *tooltip, const char *texturePath);

constexpr uint32_t kMaxTextures = 3;

// A texture already sits on `line` or on the line above it.
bool NextToTexture(const uint8_t *tooltip, uint32_t line) {
    uint32_t count = Game::Read<uint32_t>(tooltip, Offsets::OFF_GAMETOOLTIP_NUM_TEXTURES);
    if (count > kMaxTextures)
        count = kMaxTextures;
    const auto *lines = Game::Ptr<uint32_t>(tooltip, Offsets::OFF_GAMETOOLTIP_TEXTURE_LINES);
    for (uint32_t i = 0; i < count; ++i)
        if (lines[i] == line || lines[i] + 1 == line)
            return true;
    return false;
}

int __fastcall Script_GameTooltipAddTexture(void *L) {
    void *self = Game::Lua::ResolveTooltip(L);
    if (self == nullptr)
        return 0;
    if (Game::Lua::Type(L, 2) != Game::Lua::TYPE_STRING) {
        Game::Lua::Error(L, "Usage: GameTooltip:AddTexture(\"texture\")");
        return 0;
    }
    auto *tooltip = static_cast<uint8_t *>(self);
    const uint32_t numLines = Game::Read<uint32_t>(tooltip, Offsets::OFF_GAMETOOLTIP_NUM_LINES);
    const uint32_t before = Game::Read<uint32_t>(tooltip, Offsets::OFF_GAMETOOLTIP_NUM_TEXTURES);
    if (numLines != 0 && !NextToTexture(tooltip, numLines - 1))
        reinterpret_cast<AddTexture_t>(Offsets::FUN_GAMETOOLTIP_ADD_TEXTURE)(
            self, Game::Lua::ToString(L, 2));
    Game::Lua::PushBool(
        L, Game::Read<uint32_t>(tooltip, Offsets::OFF_GAMETOOLTIP_NUM_TEXTURES) != before);
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"AddTexture", &Script_GameTooltipAddTexture},
};

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("texture", "string", "Texture path, e.g. \"Interface\\\\Icons\\\\INV_Misc_Bag_08\"."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("added", "bool", "False when the texture could not be placed."),
};
const Game::Doc::Function kAddTexture{
    "Puts a texture at the start of the last line. Up to three per tooltip, not on the first line, "
    "and not on or right below a line that already has one.",
    kArgs, kRets};

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
