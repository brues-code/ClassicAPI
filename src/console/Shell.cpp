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

// The developer console, as Lua functions: running commands, writing lines,
// and reading the console's own state and appearance.
//
// `ConsoleExec` is the one with real reach. Nothing in this client's Lua could
// run a console command before, so every entry ConsoleGetAllCommands returns
// was visible and unreachable at once. It is the engine's own executor, the
// same one that replays each `SET` line of Config.wtf at boot.
//
// THAT INCLUDES `set`, AND `set` IS NOT SetCVar. The console's set handler
// looks a cvar up with the unfiltered lookup and performs no read-only check,
// while Script_SetCVar tests flag bit 0x4 first and refuses. So
// `ConsoleExec("set <name> <value>")` can write a cvar that
// `SetCVar` rejects, and can reach cvars no Lua getter can see (Offsets.h at
// CVAR_FLAG_READ_ONLY has the mechanism). That is inherent to exposing the
// console rather than something added here -- an addon already runs arbitrary
// Lua -- but it does mean the read-only guarantee C_CVar.GetCVarInfo reports
// stops being absolute once this exists. Deliberate: a console you cannot run
// commands through is not a console.
//
// Everything else is state the console module already keeps. The colours are
// its own nine-entry table, the font height its own global, and the toggle key
// its own byte, so each is read or written where the engine reads or writes it
// rather than being modelled again here.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Console::Shell {

namespace {

using ConsoleExec_t = void(__fastcall *)(const char *line, int addToHistory);
using ConsoleWrite_t = void(__fastcall *)(const char *line, int colorType);
using ConsoleSetKey_t = void(__fastcall *)(int keyCode);

// `ConsoleExec(command)` — run a console command line.
//
// Passes addToHistory = 1 so a command run from Lua appears in the console's
// own history, which is what someone stepping through the console afterwards
// expects to find.
int __fastcall Script_ConsoleExec(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return 0;
    reinterpret_cast<ConsoleExec_t>(Offsets::FUN_CONSOLE_EXEC)(Game::Lua::ToString(L, 1), 1);
    return 0;
}

// `ConsoleEcho(message [, colorType])` — write one line to the console.
//
// The engine no-ops cleanly when the graphics device is not up, so this is safe
// to call at any time, and the line is kept whether or not the console is
// currently open.
int __fastcall Script_ConsoleEcho(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return 0;
    int colorType = 0;
    if (Game::Lua::IsNumber(L, 2)) {
        colorType = static_cast<int>(Game::Lua::ToNumber(L, 2));
        if (colorType < 0 || colorType >= Offsets::CONSOLE_COLOR_COUNT)
            colorType = 0;
    }
    reinterpret_cast<ConsoleWrite_t>(Offsets::FUN_CONSOLE_WRITE)(Game::Lua::ToString(L, 1),
                                                                 colorType);
    return 0;
}

// `ConsoleIsActive()` — whether the console is open right now.
//
// False when the client was started without `-console`, since the toggle key
// is gated on that and the console can then never open.
int __fastcall Script_ConsoleIsActive(void *L) {
    const uint32_t enabled = Game::Read<uint32_t>(Offsets::VAR_CONSOLE_ENABLED);
    const uint32_t visible = Game::Read<uint32_t>(Offsets::VAR_CONSOLE_VISIBLE);
    Game::Lua::PushBool(L, enabled != 0 && visible != 0);
    return 1;
}

// `ConsoleGetColorFromType(colorType)` -> r, g, b, a
//
// The colour the console draws that type of line in, as four 0..1 components to
// match every other colour in this API. Nil for a type outside the table.
int __fastcall Script_ConsoleGetColorFromType(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const int type = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (type < 0 || type >= Offsets::CONSOLE_COLOR_COUNT) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const uint32_t argb = *reinterpret_cast<const uint32_t *>(
        Offsets::VAR_CONSOLE_COLORS + static_cast<uintptr_t>(type) * sizeof(uint32_t));
    Game::Lua::PushNumber(L, ((argb >> 16) & 0xFF) / 255.0);
    Game::Lua::PushNumber(L, ((argb >> 8) & 0xFF) / 255.0);
    Game::Lua::PushNumber(L, (argb & 0xFF) / 255.0);
    Game::Lua::PushNumber(L, ((argb >> 24) & 0xFF) / 255.0);
    return 4;
}

// `ConsoleGetFontHeight()` — the console's text height, as a fraction of the
// screen height.
int __fastcall Script_ConsoleGetFontHeight(void *L) {
    Game::Lua::PushNumber(L, *reinterpret_cast<const float *>(Offsets::VAR_CONSOLE_FONT_HEIGHT));
    return 1;
}

// There is deliberately no ConsoleSetFontHeight. Writing the height global
// alone changes nothing, since the font was already built at the old size, and
// the only thing that rebuilds it is the console's font init — which has no
// code callers at all, just one entry in a table at 0x0080E170. It runs once,
// during startup, before a single console line exists.
//
// Re-running it crashes, on a later frame rather than in the call: it frees the
// old font handle out from under the cached, font-dependent draw object every
// existing line holds at +0x20, and the next console render walks those. The
// console's own shutdown frees both together, which is what that pairing is
// for. Making a setter safe would mean replaying that teardown across every
// line, to change a text size nothing asks to change.

// `SetConsoleKey(keyCode)` — the key that opens and closes the console.
//
// Takes the engine's own key code, the value its key events carry, rather than
// a key name: the console module stores and compares exactly that, and nothing
// in it translates names.
int __fastcall Script_SetConsoleKey(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0;
    reinterpret_cast<ConsoleSetKey_t>(Offsets::FUN_CONSOLE_SET_KEY)(
        static_cast<int>(Game::Lua::ToNumber(L, 1)));
    return 0;
}

// `CalculateStringEditDistance(a, b)` — Levenshtein distance, the number of
// single-character insertions, deletions or substitutions between two strings.
// The console uses it to suggest a command when one is mistyped.
//
// Two rolling rows rather than a full matrix, so the cost is bounded by the
// shorter string; the longer one only ever drives the loop.
int __fastcall Script_CalculateStringEditDistance(void *L) {
    if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsString(L, 2)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const char *a = Game::Lua::ToString(L, 1);
    const char *b = Game::Lua::ToString(L, 2);

    size_t lenA = 0, lenB = 0;
    while (a[lenA] != '\0')
        ++lenA;
    while (b[lenB] != '\0')
        ++lenB;
    if (lenA == 0 || lenB == 0) {
        Game::Lua::PushNumber(L, static_cast<double>(lenA + lenB));
        return 1;
    }
    // Index the rows by the shorter string so the buffers stay small whichever
    // way round the arguments came in.
    if (lenB > lenA) {
        const char *t = a;
        a = b;
        b = t;
        const size_t n = lenA;
        lenA = lenB;
        lenB = n;
    }
    constexpr size_t kMaxRow = 256; // beyond this the answer is of no practical use
    if (lenB >= kMaxRow) {
        Game::Lua::PushNil(L);
        return 1;
    }

    size_t prev[kMaxRow], curr[kMaxRow];
    for (size_t j = 0; j <= lenB; ++j)
        prev[j] = j;
    for (size_t i = 1; i <= lenA; ++i) {
        curr[0] = i;
        for (size_t j = 1; j <= lenB; ++j) {
            const size_t cost = (a[i - 1] == b[j - 1]) ? 0u : 1u;
            size_t best = prev[j] + 1;            // deletion
            if (curr[j - 1] + 1 < best)
                best = curr[j - 1] + 1;           // insertion
            if (prev[j - 1] + cost < best)
                best = prev[j - 1] + cost;        // substitution
            curr[j] = best;
        }
        for (size_t j = 0; j <= lenB; ++j)
            prev[j] = curr[j];
    }
    Game::Lua::PushNumber(L, static_cast<double>(prev[lenB]));
    return 1;
}

void Register() {
    Game::Lua::RegisterGlobalFunction("ConsoleExec", &Script_ConsoleExec);
    Game::Lua::RegisterGlobalFunction("ConsoleEcho", &Script_ConsoleEcho);
    Game::Lua::RegisterGlobalFunction("ConsoleIsActive", &Script_ConsoleIsActive);
    Game::Lua::RegisterGlobalFunction("ConsoleGetColorFromType",
                                      &Script_ConsoleGetColorFromType);
    Game::Lua::RegisterGlobalFunction("ConsoleGetFontHeight", &Script_ConsoleGetFontHeight);
    Game::Lua::RegisterGlobalFunction("SetConsoleKey", &Script_SetConsoleKey);
    Game::Lua::RegisterGlobalFunction("CalculateStringEditDistance",
                                      &Script_CalculateStringEditDistance);
}

// The console exists before login, so the same surface is registered on the
// glue state.
void RegisterGlue() {
    Game::Lua::RegisterGlueFunction("ConsoleExec", &Script_ConsoleExec);
    Game::Lua::RegisterGlueFunction("ConsoleEcho", &Script_ConsoleEcho);
    Game::Lua::RegisterGlueFunction("ConsoleIsActive", &Script_ConsoleIsActive);
    Game::Lua::RegisterGlueFunction("ConsoleGetColorFromType",
                                    &Script_ConsoleGetColorFromType);
    Game::Lua::RegisterGlueFunction("ConsoleGetFontHeight", &Script_ConsoleGetFontHeight);
    Game::Lua::RegisterGlueFunction("SetConsoleKey", &Script_SetConsoleKey);
    Game::Lua::RegisterGlueFunction("CalculateStringEditDistance",
                                    &Script_CalculateStringEditDistance);
}

const Game::ModuleAutoRegister _autoreg{&Register};
const Game::GlueModuleAutoRegister _glueAutoreg{&RegisterGlue};

} // namespace

} // namespace Console::Shell
