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

// Co-hook on the engine's macro body runner (`FUN_MACRO_RUN_BODY`) so lines
// beginning with `#` — `#showtooltip`, `#show`, comments — are never
// dispatched.
//
// The vanilla runner tokenizes the body line by line and fires
// `EXECUTE_CHAT_LINE("%s", line)` for every non-empty line; FrameXML's hidden
// `MacroEditBox` then runs each line through `ChatEdit_SendText`, so a line
// that isn't a slash command is SENT TO CHAT. That is how `#showtooltip` ended
// up as a /say in stock 1.12 (and why macro addons hook `SendChatMessage` to
// swallow it). 3.3.5's runner skips `#` lines; this mirrors it.
//
// The loop below is the engine's own, minus the `#` check: same tokenizer,
// same delimiter set, same 0x400-byte buffer, same event dispatch. The
// original is not called.
//
// It also owns `StopMacro()`, the global behind `/stopmacro`. 3.3.5 splits it
// the same way — the command is FrameXML (`SecureCmdList["STOPMACRO"]` calls
// `StopMacro()` when its conditions pass) and the flag is the runner's, which
// is what makes "stop the rest of THIS macro" mean anything. Each line's
// `EXECUTE_CHAT_LINE` is dispatched synchronously, all the way through
// `ChatEdit_SendText` into the slash handler, so by the time the fire returns
// the flag is already set and the next line simply never runs.
//
// It also keeps the running macro, 4.3.4's CGUIMacros::m_macroButton: the
// entry of the body running now, which `GetRunningMacro()` turns into its
// index. RunMacroText's stack entry has ID 0, so it reports none, as in 4.3.4.
//
// And it owns the running macro's button, 4.3.4's CGUIMacros::m_macroRunning:
// `Run()` hands the runner a button for one body (RunMacroText, the secure
// button's `macro` verb, the MACRO binding), `GetRunningMacroButton()` reads it
// and the [button] condition tests it. A body the engine starts itself (an
// action-bar macro) runs with none, as each run sets its own and restores the
// outer one when it ends.

#include "macro/RunBody.h"

#include "action/Slot.h"

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"

namespace Macro::RunBody {

namespace {

using Tokenize_t = void(__stdcall *)(const char **cursor, char *out, unsigned outSize,
                                     const char *delims, int *outQuoted);

constexpr unsigned kLineBufferSize = Offsets::MACRO_LINE_BUFFER_SIZE;

bool IsBlank(char c) { return c == ' ' || c == '\t'; }

// Set by `StopMacro()`, read after each dispatched line. Scoped to the macro
// currently running: a line that runs ANOTHER macro (a `/click` on a button
// holding one) re-enters the runner, and that inner body's `/stopmacro` must
// end the inner body only. Saving and restoring around each run gives exactly
// that, and costs nothing on the common non-nested path.
bool g_stopRequested = false;

// The button of the body running now, null when none was given. `Run()` puts
// its button in `g_nextButton` for the runner to take, so a run the engine
// starts without `Run()` gets none.
const char *g_runningButton = nullptr;
// The entry of the body running now, null when none is.
const uint8_t *g_runningEntry = nullptr;
const char *g_nextButton = nullptr;

void __fastcall RunBody_h(int macroEntry) {
    if (macroEntry == 0)
        return;

    auto tokenize = reinterpret_cast<Tokenize_t>(Offsets::FUN_STORM_STR_TOKENIZE);
    const char *delims = reinterpret_cast<const char *>(Offsets::VAR_MACRO_LINE_DELIMS);
    const char *cursor = reinterpret_cast<const char *>(macroEntry + Offsets::OFF_MACRO_BODY);

    const bool outerStop = g_stopRequested;
    g_stopRequested = false;
    const char *outerButton = g_runningButton;
    g_runningButton = g_nextButton;
    g_nextButton = nullptr;
    const uint8_t *outerEntry = g_runningEntry;
    g_runningEntry = reinterpret_cast<const uint8_t *>(macroEntry);

    char line[kLineBufferSize];
    while (cursor != nullptr && *cursor != '\0') {
        tokenize(&cursor, line, kLineBufferSize, delims, nullptr);
        // The tokenizer splits on `\r\n` only, so a directive typed with a
        // leading space still arrives with it. Find the first real character
        // before deciding the line is a comment — otherwise ` #showtooltip`
        // goes to chat as a /say.
        const char *text = line;
        while (IsBlank(*text))
            ++text;
        if (*text == '\0' || *text == '#')
            continue;
        Event::Custom::Fire(static_cast<int>(Offsets::EVENT_EXECUTE_CHAT_LINE), "%s", text);
        if (g_stopRequested)
            break; // a `/stopmacro` on that line
    }

    g_stopRequested = outerStop;
    g_runningButton = outerButton;
    g_runningEntry = outerEntry;
}

// `StopMacro()` — stop running the macro body that is executing right now.
// Nothing happens when no macro is running: the flag is cleared at the start
// of every body and restored at the end, so a stray call from a script or a
// keybinding leaves nothing armed behind it.
int __fastcall Script_StopMacro(void * /*L*/) {
    g_stopRequested = true;
    return 0;
}

// `GetRunningMacroButton()` — the button the running macro was started with,
// or nil.
int __fastcall Script_GetRunningMacroButton(void *L) {
    if (g_runningButton != nullptr)
        Game::Lua::PushString(L, g_runningButton);
    else
        Game::Lua::PushNil(L);
    return 1;
}

// `GetRunningMacro()` — the 1-based index of the saved macro running now, or
// nil (no macro running, or a RunMacroText body, which has no saved macro).
int __fastcall Script_GetRunningMacro(void *L) {
    const int index =
        g_runningEntry != nullptr
            ? Action::Slot::MacroSlotForID(
                  *reinterpret_cast<const uint32_t *>(g_runningEntry + Offsets::OFF_MACRO_ID))
            : 0;
    if (index > 0)
        Game::Lua::PushNumber(L, index);
    else
        Game::Lua::PushNil(L);
    return 1;
}

const Game::Doc::Function kStopMacro{
    "Stop running the rest of the macro body that is executing now. Does "
    "nothing when no macro is running.",
    {}, {}, "MacroGlobals"};

const Game::Doc::Field kGetRunningMacroRets[] = {
    Game::Doc::Opt("macroIndex", "number", nullptr, "The index of the running macro."),
};
const Game::Doc::Function kGetRunningMacro{
    "Returns the index of the saved macro that is running now. Returns nil when no "
    "saved macro is running.",
    {}, kGetRunningMacroRets, "MacroGlobals"};

const Game::Doc::Field kGetRunningMacroButtonRets[] = {
    Game::Doc::Opt("button", "string", nullptr,
                   "The button that the running macro was started with."),
};
const Game::Doc::Function kGetRunningMacroButton{
    "Returns the button that the macro running now was started with. Returns nil "
    "when no macro is running or the macro was started without a button.",
    {}, kGetRunningMacroButtonRets, "MacroGlobals"};

// No trampoline: the loop above replaces the original outright, so the hook
// never calls back into it.
const Game::HookAutoRegister _hookreg{
    Offsets::FUN_MACRO_RUN_BODY,
    reinterpret_cast<void *>(&RunBody_h),
    nullptr};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("StopMacro", &Script_StopMacro, &kStopMacro);
    Game::Lua::RegisterGlobalFunction("GetRunningMacro", &Script_GetRunningMacro,
                                      &kGetRunningMacro);
    Game::Lua::RegisterGlobalFunction("GetRunningMacroButton", &Script_GetRunningMacroButton,
                                      &kGetRunningMacroButton);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void Run(const uint8_t *entry, const char *button) {
    g_nextButton = button;
    reinterpret_cast<void(__fastcall *)(const uint8_t *)>(Offsets::FUN_MACRO_RUN_BODY)(entry);
    g_nextButton = nullptr; // a null entry returns before taking it
}

} // namespace Macro::RunBody
