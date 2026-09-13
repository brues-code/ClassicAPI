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

// `C_Glue` session helpers.
//
// `C_Glue.IsFirstLoadThisSession()` — glue-only. Returns true only while the
// FIRST glue screen since the process launched is showing, and false on
// every return to glue after a world login (a genuine world→glue logout;
// login↔character-select stays within the first glue session). Retail uses
// it so the login UI runs one-time startup (intro cinematic, news) only on
// the initial launch, not on each relog.
//
// Backed by a process-static boot counter, no engine state needed: the glue
// registration callback fires exactly once per glue boot
// (FUN_LOAD_GLUE_SCRIPT_FUNCTIONS — initial launch + each world→glue return),
// so the counter equals "how many glue screens this session" and the first
// one is boot #1. The glue Lua state is recreated per boot, so C_Glue is
// re-registered each time; the counter is process-static and survives that.
//
// `C_Glue.IsOnGlueScreen()` — true when a GlueXML screen is showing (no
// character logged in), false in the world. Registered on BOTH Lua states,
// and the answer is a compile-time constant per state: the glue and in-game
// Lua states are mutually exclusive (glue exists only when no character is in
// the world), so the glue registration returns true and the in-game one
// returns false — no runtime flag to consult. Available in both states so
// shared code that runs in either environment can branch on it.

#include "Game.h"

namespace Glue::Session {

namespace {

// Glue-screen boots since process start. 1 during the very first glue screen.
int g_glueBootCount = 0;

int __fastcall Script_IsFirstLoadThisSession(void *L) {
    Game::Lua::PushBool(L, g_glueBootCount <= 1);
    return 1;
}

int __fastcall Script_IsOnGlueScreen_True(void *L) {
    Game::Lua::PushBool(L, true);
    return 1;
}

int __fastcall Script_IsOnGlueScreen_False(void *L) {
    Game::Lua::PushBool(L, false);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kIsFirstLoadRets[] = {
    Game::Doc::Req("isFirstLoad", "bool",
                   "True only while the first login screen since launch is showing."),
};
const Game::Doc::Function kIsFirstLoadThisSession{
    "Whether this is the first login screen since the game started.",
    {}, kIsFirstLoadRets};

const Game::Doc::Field kIsOnGlueScreenRets[] = {
    Game::Doc::Req("isOnGlueScreen", "bool", "True on the login and character screens."),
};
const Game::Doc::Function kIsOnGlueScreen{
    "Whether a login or character screen is showing rather than the world.",
    {}, kIsOnGlueScreenRets};

void RegisterGlue() {
    ++g_glueBootCount; // one glue boot per callback invocation
    Game::Lua::RegisterTableFunction("C_Glue", "IsFirstLoadThisSession",
                                     &Script_IsFirstLoadThisSession,
                                     &kIsFirstLoadThisSession);
    Game::Lua::RegisterTableFunction("C_Glue", "IsOnGlueScreen",
                                     &Script_IsOnGlueScreen_True, &kIsOnGlueScreen);
}

void RegisterInGame() {
    // In-world: only IsOnGlueScreen makes sense (and it's always false).
    // IsFirstLoadThisSession is a glue concept, so it's not registered here.
    Game::Lua::RegisterTableFunction("C_Glue", "IsOnGlueScreen",
                                     &Script_IsOnGlueScreen_False, &kIsOnGlueScreen);
}

const Game::GlueModuleAutoRegister _autoregGlue{&RegisterGlue};
const Game::ModuleAutoRegister _autoreg{&RegisterInGame};

} // namespace

} // namespace Glue::Session
