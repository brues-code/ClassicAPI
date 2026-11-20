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
// `SetErrorCallstackHeight([height])` — the call stack heights the 3.80.2
// client keeps. Its error handler turns them into the debugstack level of the
// code that failed, however many functions sit between that code and the
// handler:
//     debugstack(GetCallstackHeight() - (GetErrorCallstackHeight() - 1))
//
// A height counts call records (CallInfo), as 3.80.2's do: GetCallstackHeight
// (0x140C69AF0) is `(ci - base_ci) - 1` of the main state, so the caller is 1
// and a lost tail call does not count. 1.12's lua_getstack reports each
// level's record index (lua_Debug i_ci), which gives the same numbers.
//
// 3.80.2's error function (0x1405B6400) records the height before it calls
// the handler and clears it after: unless a height is set, it walks up from
// itself to the first level whose `what` is "Lua" (so past error / assert, a
// main chunk and tail levels) and stores its own record index less that
// level's distance, or 0 when there is none. 1.12's error function
// (FUN_FRAMESCRIPT_CALL_ERROR_HANDLER) records nothing, and needs to record
// nothing: every error report runs it as a C closure — each script pcall
// passes it as the errfunc, so Lua calls it at the point of the error before
// the stack unwinds, and a chunk that fails to compile is reported by a pcall
// of it (FUN_LUA_RUN_STRING) — and it calls the seterrorhandler function with
// lua_call. So while a handler runs, that closure's level is on the stack and
// GetErrorCallstackHeight computes from it exactly what 3.80.2 stored.
//
// A height set with SetErrorCallstackHeight wins over a raised error, as in
// 3.80.2, whose error function leaves a set height alone; retail's
// CallErrorHandler and assertsafe set one around a handler they call
// directly. Nothing clears it but SetErrorCallstackHeight(nil).

#include "Game.h"
#include "Offsets.h"
#include "baselib/ErrorCallstack.h"

#include <cmath>
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

int RecordIndex(const uint8_t *ar) { return Game::Read<int>(ar, Offsets::OFF_LUA_DEBUG_I_CI); }

// GetCallstackHeight: the running C function's record index less one, on the
// main state.
int Height() {
    void *M = Game::Lua::State();
    alignas(4) uint8_t ar[Offsets::SIZEOF_LUA_DEBUG];
    if (M == nullptr || !Game::Lua::GetStack(M, 0, ar))
        return 0;
    return RecordIndex(ar) - 1;
}

// The height 3.80.2's error function would have stored for the error being
// handled: the error function's record index less the distance to the first
// "Lua" level above it, 0 without one. Nothing when no error is being handled.
std::optional<int> RaisedHeight(void *L) {
    const int errorLevel = ErrorFunctionLevel(L);
    if (errorLevel == 0)
        return std::nullopt;
    alignas(4) uint8_t ar[Offsets::SIZEOF_LUA_DEBUG];
    if (!Game::Lua::GetStack(L, errorLevel, ar))
        return std::nullopt;
    const int errorRecord = RecordIndex(ar);
    for (int k = 0; Game::Lua::GetStack(L, errorLevel + k, ar); ++k) {
        Game::Lua::GetInfo(L, "S", ar);
        const char *what = Game::Read<const char *>(ar, Offsets::OFF_LUA_DEBUG_WHAT);
        if (what != nullptr && what[0] == 'L') // "Lua", not "C", "main" or "tail"
            return errorRecord - k;
    }
    return 0;
}

int __fastcall Script_GetCallstackHeight(void *L) {
    Game::Lua::PushNumber(L, Height());
    return 1;
}

int __fastcall Script_GetErrorCallstackHeight(void *L) {
    const std::optional<int> height = g_setHeight ? g_setHeight : RaisedHeight(L);
    if (height)
        Game::Lua::PushNumber(L, *height);
    else
        Game::Lua::PushNil(L);
    return 1;
}

// 3.80.2's (0x140C6ABD0): nil or none clears; otherwise a finite number in
// int32 range, truncated.
int __fastcall Script_SetErrorCallstackHeight(void *L) {
    if (Game::Lua::Type(L, 1) <= Game::Lua::TYPE_NIL) {
        g_setHeight.reset();
        return 0;
    }
    const double v = Game::Lua::IsNumber(L, 1) ? Game::Lua::ToNumber(L, 1) : NAN;
    if (!(v >= -2147483648.0 && v <= 2147483647.0)) {
        Game::Lua::ArgError(L, 1,
                            "outside of expected range -2147483648 to 2147483647 - Usage: "
                            "SetErrorCallstackHeight([height])");
        return 0;
    }
    g_setHeight = static_cast<int>(v);
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
    "Sets the height that GetErrorCallstackHeight returns, also over a raised error, until it is cleared.",
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
