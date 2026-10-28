// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

// `GetMacroBody(macro)` — the body text of a macro, by index or name.
//
// 4.3.4's Script_GetMacroBody (0x009C56E0) resolves argument 1 through
// GetMacroFromLua (Macro::Arg::Slot) and pushes the entry's body; no macro
// there returns nothing, with no usage error. 1.12 keeps the body inline at
// OFF_MACRO_BODY, the same field its GetMacroInfo (0x004F1760) pushes.

#include "macro/Arg.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Macro::Body {

namespace {

using SlotToEntry_t = const uint8_t *(__fastcall *)(uint32_t slot0);

int __fastcall Script_GetMacroBody(void *L) {
    const int slot = Macro::Arg::Slot(L);
    if (slot == 0)
        return 0;
    const uint8_t *entry = reinterpret_cast<SlotToEntry_t>(Offsets::FUN_MACRO_SLOT_TO_ENTRY)(
        static_cast<uint32_t>(slot - 1));
    Game::Lua::PushString(L, reinterpret_cast<const char *>(entry + Offsets::OFF_MACRO_BODY));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("macro", "string", "The macro's index or name."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("body", "string", "The macro's text."),
};
const Game::Doc::Function kGetMacroBody{
    "Returns the text of a macro.",
    kArgs, kRets, "MacroGlobals"};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetMacroBody", &Script_GetMacroBody, &kGetMacroBody);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Macro::Body
