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

// Glue-side CVar bindings. Vanilla 1.12 registers `GetCVar`,
// `SetCVar`, `RegisterCVar`, and `GetCVarDefault` only on the
// in-game FrameScript engine; on the glue Lua state they're absent.
// CVar storage itself is process-global, so the same C functions work
// fine from either state — we just need to register the function
// pointers on glue too.
//
// No new C code: we hand the engine's own `Script_*` pointers
// straight to `RegisterGlueFunction`. The functions follow the
// standard `int __fastcall(void *L)` ABI required of any Lua-bound
// C function, so the cast through `Game::Lua::CFunction` is sound.

#include "Game.h"
#include "Offsets.h"

namespace CVar::Glue {

namespace {

const Game::Doc::Field kRegisterArgs[] = {
    Game::Doc::Req("name", "cstring"),
    Game::Doc::Opt("value", "cstring", "\"0\"", "The default value."),
};
const Game::Doc::Function kRegisterCVar{
    "Registers a console variable for scripts, which is saved like any other.", kRegisterArgs, {},
    "CVarGlobals"};

const Game::Doc::Field kGetArgs[] = {
    Game::Doc::Req("name", "cstring"),
};
const Game::Doc::Field kGetRets[] = {
    Game::Doc::Opt("value", "string", nullptr, "Nothing when no cvar has that name."),
};
const Game::Doc::Function kGetCVar{"Returns the value of a console variable.", kGetArgs,
                                   kGetRets, "CVarGlobals"};

const Game::Doc::Field kSetArgs[] = {
    Game::Doc::Req("name", "cstring"),
    Game::Doc::Opt("value", "cstring", "\"0\"", "A number is set as its text."),
    Game::Doc::Opt("eventName", "string", nullptr,
                   "When given, CVAR_UPDATE fires with this name and the new value."),
};
const Game::Doc::Function kSetCVar{
    "Sets a console variable and saves it; raises an error for an unknown or read-only cvar.",
    kSetArgs, {}, "CVarGlobals"};

const Game::Doc::Field kGetDefaultArgs[] = {
    Game::Doc::Req("name", "cstring"),
};
const Game::Doc::Field kGetDefaultRets[] = {
    Game::Doc::Opt("defaultValue", "string", nullptr,
                   "Nil when the cvar has no default; nothing when no cvar has that name."),
};
const Game::Doc::Function kGetCVarDefault{"Returns the default value of a console variable.",
                                          kGetDefaultArgs, kGetDefaultRets, "CVarGlobals"};

void RegisterGlueFunctions() {
    Game::Lua::RegisterGlueFunction("RegisterCVar",
        reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_REGISTER_CVAR),
        &kRegisterCVar);
    Game::Lua::RegisterGlueFunction("GetCVar",
        reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_GET_CVAR), &kGetCVar);
    Game::Lua::RegisterGlueFunction("SetCVar",
        reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_SET_CVAR), &kSetCVar);
    Game::Lua::RegisterGlueFunction("GetCVarDefault",
        reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_GET_CVAR_DEFAULT),
        &kGetCVarDefault);
}

const Game::GlueModuleAutoRegister _autoreg{&RegisterGlueFunctions};

} // namespace

} // namespace CVar::Glue
