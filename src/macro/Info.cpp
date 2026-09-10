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

// `GetMacroInfo(index)` with the `#showtooltip` / `#show` icon rule: for a
// macro whose icon is the question mark and whose directive resolved to a
// spell or item, the `texture` return is that spell's / item's icon. This is
// what the Macro UI grid and its selected-macro button draw, so the macro
// window shows the same icon as the action bar.
//
// Registered in front of the engine's `GetMacroInfo` (last registration
// wins). The engine function runs unchanged and its four returns are kept;
// only the second value is swapped, in place on the Lua stack. Everything
// else — the stored `?` for an unresolved directive, empty slots, the usage
// error — is the engine's.
//
// The Macro UI's icon popup pre-checks the icon whose name equals this
// texture, but saving still needs an explicit icon click
// (`MacroPopupButton_SelectTexture` is the only writer of `selectedIcon`), so
// the swapped texture can't silently turn a `?` macro into a fixed-icon one.

#include "Game.h"
#include "Offsets.h"
#include "macro/ShowTooltip.h"

#include <cstdint>

namespace Macro::Info {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);

constexpr uint32_t kIconPathSize = 0x104; // the engine's own texture buffer size

int __fastcall Script_GetMacroInfo(void *L) {
    auto engine = reinterpret_cast<ScriptFn_t>(Offsets::FUN_SCRIPT_GET_MACRO_INFO);
    if (!Game::Lua::IsNumber(L, 1))
        return engine(L); // raises the engine's own usage error
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1));

    const int base = Game::Lua::GetTop(L);
    const int count = engine(L); // (name, texture, body, isLocal)
    if (count < 2 || slot < 1 || slot > Offsets::MACRO_SLOT_MAP_COUNT)
        return count;

    const uint32_t macroID = Game::Read<uint32_t>(
        static_cast<uintptr_t>(Offsets::VAR_MACRO_SLOT_MAP) + static_cast<uintptr_t>(slot - 1) * 4);
    Macro::ShowTooltip::Info info;
    char icon[kIconPathSize];
    if (macroID == 0 || !Macro::ShowTooltip::Lookup(macroID, &info) ||
        !Macro::ShowTooltip::HasQuestionMarkIcon(macroID) ||
        !Macro::ShowTooltip::ResolvedIconPath(info, /*activeIcon=*/false, icon, sizeof(icon)))
        return count;

    // Replace the texture (second return, absolute index base + 2): push
    // ours, move it into that slot, drop the engine's one slot below it.
    Game::Lua::PushString(L, icon);
    Game::Lua::Insert(L, base + 2);
    Game::Lua::Remove(L, base + 3);
    return count;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetMacroInfo", &Script_GetMacroInfo);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Macro::Info
