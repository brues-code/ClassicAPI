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

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"

namespace Macro::RunBody {

namespace {

using Tokenize_t = void(__stdcall *)(const char **cursor, char *out, unsigned outSize,
                                     const char *delims, int *outQuoted);

constexpr unsigned kLineBufferSize = Offsets::MACRO_LINE_BUFFER_SIZE;

bool IsBlank(char c) { return c == ' ' || c == '\t'; }

void __fastcall RunBody_h(int macroEntry) {
    if (macroEntry == 0)
        return;

    auto tokenize = reinterpret_cast<Tokenize_t>(Offsets::FUN_STORM_STR_TOKENIZE);
    const char *delims = reinterpret_cast<const char *>(Offsets::VAR_MACRO_LINE_DELIMS);
    const char *cursor = reinterpret_cast<const char *>(macroEntry + Offsets::OFF_MACRO_BODY);

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
    }
}

// No trampoline: the loop above replaces the original outright, so the hook
// never calls back into it.
const Game::HookAutoRegister _hookreg{
    Offsets::FUN_MACRO_RUN_BODY,
    reinterpret_cast<void *>(&RunBody_h),
    nullptr};

} // namespace

} // namespace Macro::RunBody
