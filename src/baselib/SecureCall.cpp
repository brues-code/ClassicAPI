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

// `securecall(func or name, ...)`, `securecallfunction(func, ...)` and
// `secureexecuterange(tbl, func, ...)` — the later clients' protected calls,
// less taint (1.12 has none). Retail, probed:
//   - securecall returns the function's results; a string names a global
//     function. An error goes to the error handler and the call returns
//     nothing.
//   - securecallfunction is securecall without the name lookup: a string is
//     a value that cannot be called, so it errors and returns nothing.
//   - secureexecuterange calls func(key, value, ...) for each entry of tbl in
//     `next` order; an error in one goes to the error handler and the walk
//     goes on. It returns nothing.
// The error handler is the one every engine script run passes
// (VAR_FRAMESCRIPT_ERROR_HANDLER_REF), as HookScript's wrapper uses.
//
// Registered on both Lua states, like the base-library helpers.

#include "Game.h"
#include "Offsets.h"

namespace BaseLib::SecureCall {

namespace {

// Pushes the engine's script error handler; false (nothing pushed) before
// the engine has set one.
bool PushErrorHandler(void *L) {
    const int ref = Game::Read<int>(static_cast<uintptr_t>(Offsets::VAR_FRAMESCRIPT_ERROR_HANDLER_REF));
    if (ref <= 0)
        return false;
    Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, ref);
    return true;
}

// Calls the function at stack index 1 with the values above it, under the
// error handler. Returns the result count, 0 after an error.
int CallProtected(void *L) {
    const int nargs = Game::Lua::GetTop(L) - 1;
    int errIdx = 0;
    if (PushErrorHandler(L)) {
        Game::Lua::Insert(L, 1); // [handler, func, args...]
        errIdx = 1;
    }
    if (Game::Lua::PCall(L, nargs, Game::Lua::MULTRET, errIdx) != 0)
        return 0;
    return Game::Lua::GetTop(L) - errIdx;
}

int __fastcall Script_securecall(void *L) {
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_STRING) {
        Game::Lua::PushValue(L, 1);
        Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
        Game::Lua::Remove(L, 1);
        Game::Lua::Insert(L, 1);
    }
    return CallProtected(L);
}

int __fastcall Script_securecallfunction(void *L) { return CallProtected(L); }

int __fastcall Script_secureexecuterange(void *L) {
    const int n = Game::Lua::GetTop(L); // tbl, func, extra args 3..n
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE)
        return 0;
    Game::Lua::PushNil(L);
    while (Game::Lua::Next(L, 1) != 0) { // [.., key, value]
        int errIdx = 0;
        if (PushErrorHandler(L))
            errIdx = n + 3;
        Game::Lua::PushValue(L, 2);
        Game::Lua::PushValue(L, n + 1);
        Game::Lua::PushValue(L, n + 2);
        for (int i = 3; i <= n; ++i)
            Game::Lua::PushValue(L, i);
        Game::Lua::PCall(L, n, 0, errIdx); // key, value and the n - 2 extras
        Game::Lua::SetTop(L, n + 1);       // the key, for the next step
    }
    return 0;
}

const Game::Doc::Field kCallArgs[] = {
    Game::Doc::Req("func", "function", "The function to call, or for securecall the name of a global function."),
    Game::Doc::Opt("...", "any", nullptr, "The arguments for the function."),
};
const Game::Doc::Field kCallRets[] = {
    Game::Doc::Opt("...", "any", nullptr, "The function's results, or nothing after an error."),
};
const Game::Doc::Field kRangeArgs[] = {
    Game::Doc::Req("tbl", "table", "The table whose entries are visited."),
    Game::Doc::Req("func", "function", "Called as func(key, value, ...) for each entry."),
    Game::Doc::Opt("...", "any", nullptr, "Extra arguments for each call."),
};
const Game::Doc::Function kSecureCall{
    "Calls a function and returns its results. An error goes to the error handler and the call returns nothing.",
    kCallArgs, kCallRets, "LuaGlobals"};
const Game::Doc::Function kSecureCallFunction{
    "Calls a function and returns its results. An error goes to the error handler and the call returns nothing.",
    kCallArgs, kCallRets, "LuaGlobals"};
const Game::Doc::Function kSecureExecuteRange{
    "Calls a function for each entry of a table. An error goes to the error handler and the other entries still run.",
    kRangeArgs, {}, "LuaGlobals"};

void RegisterInGame() {
    Game::Lua::RegisterGlobalFunction("securecall", &Script_securecall, &kSecureCall);
    Game::Lua::RegisterGlobalFunction("securecallfunction", &Script_securecallfunction, &kSecureCallFunction);
    Game::Lua::RegisterGlobalFunction("secureexecuterange", &Script_secureexecuterange, &kSecureExecuteRange);
}

void RegisterGlue() {
    Game::Lua::RegisterGlueFunction("securecall", &Script_securecall, &kSecureCall);
    Game::Lua::RegisterGlueFunction("securecallfunction", &Script_securecallfunction, &kSecureCallFunction);
    Game::Lua::RegisterGlueFunction("secureexecuterange", &Script_secureexecuterange, &kSecureExecuteRange);
}

const Game::ModuleAutoRegister _autoreg{&RegisterInGame};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterGlue};

} // namespace

} // namespace BaseLib::SecureCall
