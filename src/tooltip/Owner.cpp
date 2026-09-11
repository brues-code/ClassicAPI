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

// `GameTooltip:GetOwner()` — backport of the modern Classic Era method.
// Vanilla 1.12 ships `SetOwner` and `IsOwned` but never added the
// matching reader. The owner is stored on the tooltip frame at
// `OFF_TOOLTIP_OWNER`, written by the helper at 0x0052FFE0 called from
// the SetOwner method's tail.
//
// Stored value is `owner_CObject + OFF_FRAME_LAYOUT_SUBOBJECT` (the
// LayoutFrame sub-object), not the bare Frame pointer — multiple
// inheritance lets the engine call LayoutFrame methods polymorphically
// on the stored pointer without an extra cast. We subtract the offset
// back out, then `lua_rawgeti(L, REGISTRY_INDEX, [cobj+0x08])` pushes
// the frame's Lua table. The registry refkey was set up when the
// engine first exposed the frame to Lua (i.e. `CreateFrame`-time), so
// there's no lazy-init to worry about — any frame that's been a
// SetOwner target is by definition already Lua-visible.
//
// Returns only the owner frame, not `(owner, anchorPoint)` like modern
// Classic. The anchor string is already reachable via vanilla's native
// `GameTooltip:GetAnchorType()` (slot 5 in the GameTooltip method
// registry), so addons that want it can call that separately.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Tooltip::Owner {

namespace {
// FUN_FRAMESCRIPT_PUSH_OBJECT is `lua_rawgeti` — see Offsets.h comment.
using LuaRawGetI_t = void(__fastcall *)(void *L, int idx, int n);
} // namespace

static int __fastcall Script_GameTooltipGetOwner(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: GameTooltip:GetOwner()");
        return 0;
    }
    void *tooltip = Game::Lua::ResolveTooltip(L);
    if (tooltip == nullptr)
        return 0;

    auto *layoutFrame = Game::Read<uint8_t *>(tooltip, Offsets::OFF_TOOLTIP_OWNER);
    if (layoutFrame == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    auto *ownerFrame = layoutFrame - Offsets::OFF_FRAME_LAYOUT_SUBOBJECT;
    const int refKey =
        Game::Read<int>(ownerFrame, Offsets::OFF_COBJECT_LUA_REGISTRY_REF);
    auto rawgeti = reinterpret_cast<LuaRawGetI_t>(
        Offsets::FUN_FRAMESCRIPT_PUSH_OBJECT);
    rawgeti(L, Game::Lua::REGISTRY_INDEX, refKey);
    return 1;
}

static const Game::Lua::FrameMethodEntry g_methods[] = {
    {"GetOwner", &Script_GameTooltipGetOwner},
};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_GAMETOOLTIP_METHOD_REGISTRY),
        g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Tooltip::Owner
