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

// `GetMacroInfo` / `EditMacro` / `PickupMacro` / `DeleteMacro` take a macro
// name as well as an index, as on 2.0+ clients (`index or "name"`).
//
// Vanilla's four take only a 1-based index and raise "Usage:
// DeleteMacro(index)" (etc.) for anything else. Ported code uses the name
// form, and relies on an unknown name being harmless: Zygor Guides Viewer
// clears its scratch macros with `DeleteMacro("ZygorGuidesMacro" .. i)` for
// i = 1..20 - most of which don't exist - every time a guide step starts.
//
// Co-hook: when argument 1 is a string that isn't numeric (Lua 5.0's
// lua_isnumber accepts "3", and so do the originals), it is replaced by the
// macro's 1-based index from FUN_MACRO_NAME_TO_SLOT, the resolver behind
// GetMacroIndexByName, so a name matches exactly the macro that function
// finds. An unknown name becomes index 0, which each original rejects the
// way it rejects any out-of-range index: GetMacroInfo returns nils, the
// other three do nothing. Everything else - usage errors, the remaining
// argument checks, the workers and their UI refreshes - is the engine's own.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Macro::ByName {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);
using MacroNameToSlot_t = uint32_t(__fastcall *)(const char *name);

constexpr uint32_t kSlotMiss = 0xFFFFFFFFu;

ScriptFn_t g_origGetMacroInfo = nullptr;
ScriptFn_t g_origEditMacro = nullptr;
ScriptFn_t g_origPickupMacro = nullptr;
ScriptFn_t g_origDeleteMacro = nullptr;

// Swaps a macro-name first argument for its 1-based index (0 when no macro
// has that name). Numbers, numeric strings and other types are left for the
// original's own checks.
void NameArgToIndex(void *L) {
    if (!Game::Lua::IsString(L, 1) || Game::Lua::IsNumber(L, 1))
        return;
    auto nameToSlot =
        reinterpret_cast<MacroNameToSlot_t>(Offsets::FUN_MACRO_NAME_TO_SLOT);
    const uint32_t slot0 = nameToSlot(Game::Lua::ToString(L, 1));
    Game::Lua::PushNumber(
        L, slot0 == kSlotMiss ? 0.0 : static_cast<double>(slot0 + 1));
    Game::Lua::Insert(L, 1); // index at 1, the name moves to 2
    Game::Lua::Remove(L, 2);
}

int __fastcall GetMacroInfo_h(void *L) {
    NameArgToIndex(L);
    return g_origGetMacroInfo(L);
}

int __fastcall EditMacro_h(void *L) {
    NameArgToIndex(L);
    return g_origEditMacro(L);
}

int __fastcall PickupMacro_h(void *L) {
    NameArgToIndex(L);
    return g_origPickupMacro(L);
}

int __fastcall DeleteMacro_h(void *L) {
    NameArgToIndex(L);
    return g_origDeleteMacro(L);
}

const Game::HookAutoRegister _getMacroInfoHook{
    Offsets::FUN_SCRIPT_GET_MACRO_INFO,
    reinterpret_cast<void *>(&GetMacroInfo_h),
    reinterpret_cast<void **>(&g_origGetMacroInfo)};

const Game::HookAutoRegister _editMacroHook{
    Offsets::FUN_SCRIPT_EDIT_MACRO, reinterpret_cast<void *>(&EditMacro_h),
    reinterpret_cast<void **>(&g_origEditMacro)};

const Game::HookAutoRegister _pickupMacroHook{
    Offsets::FUN_SCRIPT_PICKUP_MACRO, reinterpret_cast<void *>(&PickupMacro_h),
    reinterpret_cast<void **>(&g_origPickupMacro)};

const Game::HookAutoRegister _deleteMacroHook{
    Offsets::FUN_SCRIPT_DELETE_MACRO, reinterpret_cast<void *>(&DeleteMacro_h),
    reinterpret_cast<void **>(&g_origDeleteMacro)};

} // namespace

} // namespace Macro::ByName
