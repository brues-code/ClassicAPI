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

// Glue-side `RunScript(code)`. Vanilla 1.12 registers `RunScript`
// only on the in-game FrameScript engine; the glue Lua state has no
// way to compile and run an arbitrary string. We hand the engine's
// own `Script_RunScript` pointer to `RegisterGlueFunction` — same C
// function, just made reachable from glue too.
//
// `RunScript` compiles its argument as a Lua chunk and runs it in
// the current state's globals. Both states use the same Lua engine,
// so the compile/run path works identically.

#include "Game.h"
#include "Offsets.h"

namespace Script::Run {

namespace {

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("text", "cstring", "Lua source code."),
};
const Game::Doc::Function kRunScript{"Compiles and runs a string of Lua code.", kArgs, {},
                                     "FrameScriptGlobals"};

void RegisterGlueFunctions() {
    Game::Lua::RegisterGlueFunction("RunScript",
        reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_RUN_SCRIPT), &kRunScript);
}

const Game::GlueModuleAutoRegister _autoreg{&RegisterGlueFunctions};

} // namespace

} // namespace Script::Run
