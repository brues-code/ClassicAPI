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

// `RunMacroText(text [, button])` and the macro runs that other modules start
// (the secure button's `macro` verb, the MACRO binding).
//
// 4.3.4's RunMacroText copies the text into the body of a MACRONODE on the
// stack and hands it to CGUIMacros::RunMacro, the runner a saved macro goes
// through. 1.12's runner, FUN_MACRO_RUN_BODY, reads nothing of the entry but
// the body at +OFF_MACRO_BODY (0x004F14ED), so a zeroed stack entry with the
// text copied there is a whole macro to it. Each line then goes the engine's
// way: EXECUTE_CHAT_LINE to FrameXML's MacroEditBox and ChatEdit_SendText,
// with `#` lines skipped and /stopmacro honored by `Macro::RunBody`.

#include "macro/Execute.h"
#include "macro/RunBody.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace Macro::Execute {

namespace {

int __fastcall Script_RunMacroText(void *L) {
    if (!Game::Lua::IsString(L, 1))
        Game::Lua::Error(L, "Usage: RunMacroText(\"text\" [, \"button\"])");
    Text(Game::Lua::ToString(L, 1), Game::Lua::ToString(L, 2));
    return 0;
}

const Game::Doc::Field kRunMacroTextArgs[] = {
    Game::Doc::Req("text", "string", "The macro text. Lines are separated by line breaks."),
    Game::Doc::Opt("button", "string", nullptr,
                   "The button that the macro runs with, for [button] and "
                   "GetRunningMacroButton()."),
};
const Game::Doc::Function kRunMacroText{
    "Runs text as a macro, one line at a time, as if a macro with this text was used.",
    kRunMacroTextArgs, {}, "MacroGlobals"};

} // namespace

void Text(const char *text, const char *button) {
    if (text == nullptr)
        return;
    uint8_t entry[Offsets::OFF_MACRO_BODY + Offsets::MACRO_BODY_SIZE] = {};
    // The body keeps its last byte for the NUL, as the engine's own copy does.
    std::memcpy(entry + Offsets::OFF_MACRO_BODY, text,
                strnlen(text, Offsets::MACRO_BODY_SIZE - 1));
    RunBody::Run(entry, button);
}

bool Saved(const char *macro, const char *button) {
    if (macro == nullptr || *macro == '\0')
        return false;
    using SlotToEntry_t = const uint8_t *(__fastcall *)(unsigned slot);
    using NameToSlot_t = uint32_t(__fastcall *)(const char *name);
    const char *digit = macro;
    while (*digit >= '0' && *digit <= '9')
        ++digit;
    const uint32_t slot =
        *digit == '\0'
            ? static_cast<uint32_t>(std::atoi(macro) - 1)
            : reinterpret_cast<NameToSlot_t>(Offsets::FUN_MACRO_NAME_TO_SLOT)(macro);
    const uint8_t *entry =
        reinterpret_cast<SlotToEntry_t>(Offsets::FUN_MACRO_SLOT_TO_ENTRY)(slot);
    if (entry == nullptr)
        return false;
    RunBody::Run(entry, button);
    return true;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("RunMacroText", &Script_RunMacroText, &kRunMacroText);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Macro::Execute
