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

// `GetCallstackHeight()`, `GetErrorCallstackHeight()` and
// `SetErrorCallstackHeight([height])` — retail's call stack heights. Its
// error handler turns them into the debugstack level of the code that
// failed, however many functions sit between that code and the handler:
//     debugstack(GetCallstackHeight() - (GetErrorCallstackHeight() - 1))
// A height counts call levels as lua_getstack does, and so as debugstack
// does: C functions and lost tail calls count, and level 1 is the caller of
// the C function that asks.
//
// 1.12 records no error height, and needs none. Every error report runs the
// engine's error function (FUN_FRAMESCRIPT_CALL_ERROR_HANDLER) as a C
// closure: each script pcall passes it as the errfunc, so Lua calls it at
// the point of the error before the stack unwinds, and a chunk that fails
// to compile is reported by a pcall of it (FUN_LUA_RUN_STRING). It calls the
// seterrorhandler function with lua_call. So while any handler runs, that
// closure's level is on the stack directly above the code that failed, and
// GetErrorCallstackHeight answers the height below the innermost one.
// Nothing is recorded, so a handler that fails part way leaves nothing
// behind, and a nested error finds its own level.
//
// SetErrorCallstackHeight is for a handler called directly, with no error
// raised (retail's CallErrorHandler and assertsafe): with no engine level on
// the stack, GetErrorCallstackHeight answers the value set. An engine level
// wins over it, so a value a failed handler left set does not move the
// stack of a later error. Retail's order between the two is not known; no
// retail code sets the value around a raised error.

#include "Game.h"
#include "Offsets.h"
#include "baselib/ErrorCallstack.h"

#include <cstdint>
#include <optional>

namespace BaseLib::ErrorCallstack {

int ErrorFunctionLevel(void *L) {
    const auto errorFunction = reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_FRAMESCRIPT_CALL_ERROR_HANDLER);
    alignas(4) uint8_t ar[Offsets::SIZEOF_LUA_DEBUG];
    for (int level = 1; Game::Lua::GetStack(L, level, ar); ++level) {
        Game::Lua::GetInfo(L, "f", ar); // the level's function; nil for a lost tail call
        const bool found = Game::Lua::ToCFunction(L, -1) == errorFunction;
        Game::Lua::SetTop(L, -2);
        if (found)
            return level;
    }
    return 0;
}

namespace {

std::optional<int> g_setHeight;

// The number of call levels below the running C function.
int Height(void *L) {
    alignas(4) uint8_t ar[Offsets::SIZEOF_LUA_DEBUG];
    int height = 0;
    while (Game::Lua::GetStack(L, height + 1, ar))
        ++height;
    return height;
}

int __fastcall Script_GetCallstackHeight(void *L) {
    Game::Lua::PushNumber(L, Height(L));
    return 1;
}

int __fastcall Script_GetErrorCallstackHeight(void *L) {
    const int level = ErrorFunctionLevel(L);
    if (level > 0)
        Game::Lua::PushNumber(L, Height(L) - level);
    else if (g_setHeight)
        Game::Lua::PushNumber(L, *g_setHeight);
    else
        Game::Lua::PushNil(L);
    return 1;
}

int __fastcall Script_SetErrorCallstackHeight(void *L) {
    if (Game::Lua::Type(L, 1) <= Game::Lua::TYPE_NIL)
        g_setHeight.reset();
    else if (Game::Lua::IsNumber(L, 1))
        g_setHeight = static_cast<int>(Game::Lua::ToNumber(L, 1));
    else
        Game::Lua::Error(L, "Usage: SetErrorCallstackHeight([height])");
    return 0;
}

const Game::Doc::Field kHeightRets[] = {
    Game::Doc::Req("height", "number", "The calling function's level and every level below it."),
};
const Game::Doc::Field kErrorHeightRets[] = {
    Game::Doc::Opt("height", "number", nullptr, "The stack height of the code that caused the error."),
};
const Game::Doc::Field kSetArgs[] = {
    Game::Doc::Opt("height", "number", nullptr, "The height to return, or nil to clear it."),
};
const Game::Doc::Function kGetCallstackHeight{
    "Returns the number of call levels on the stack, counting the calling function as level 1.",
    {}, kHeightRets, "FrameScriptGlobals"};
const Game::Doc::Function kGetErrorCallstackHeight{
    "Returns the call stack height of the code that caused the error being handled, or nil when no error is being handled.",
    {}, kErrorHeightRets, "FrameScriptGlobals"};
const Game::Doc::Function kSetErrorCallstackHeight{
    "Sets the height that GetErrorCallstackHeight returns for an error handler called with no error raised.",
    kSetArgs, {}, "FrameScriptGlobals"};

// A new Lua state starts with no height set.
void RegisterInGame() {
    g_setHeight.reset();
    Game::Lua::RegisterGlobalFunction("GetCallstackHeight", &Script_GetCallstackHeight, &kGetCallstackHeight);
    Game::Lua::RegisterGlobalFunction("GetErrorCallstackHeight", &Script_GetErrorCallstackHeight,
                                      &kGetErrorCallstackHeight);
    Game::Lua::RegisterGlobalFunction("SetErrorCallstackHeight", &Script_SetErrorCallstackHeight,
                                      &kSetErrorCallstackHeight);
}

void RegisterGlue() {
    g_setHeight.reset();
    Game::Lua::RegisterGlueFunction("GetCallstackHeight", &Script_GetCallstackHeight, &kGetCallstackHeight);
    Game::Lua::RegisterGlueFunction("GetErrorCallstackHeight", &Script_GetErrorCallstackHeight,
                                    &kGetErrorCallstackHeight);
    Game::Lua::RegisterGlueFunction("SetErrorCallstackHeight", &Script_SetErrorCallstackHeight,
                                    &kSetErrorCallstackHeight);
}

const Game::ModuleAutoRegister _autoreg{&RegisterInGame};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterGlue};

} // namespace

} // namespace BaseLib::ErrorCallstack
