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

// `IsLoggedIn()` — exposed on both Lua states (glue and in-game).
// Retail's answer: true once PLAYER_LOGIN has fired for this UI instance.
// So it is false on glue and while the addons load, at a cold login and
// after a /reload, and true from PLAYER_LOGIN's own handlers on.
//
// 3.3.5's Script_IsLoggedIn is `!CGGameUI::m_loggingIn`: InitializeFrameXML
// sets m_loggingIn first thing, before FrameScript_Initialize and the addon
// load, and the enter cascade clears it just before it fires PLAYER_LOGIN.
// 1.12's arm byte 0x00B4E260 is the same flag but cannot be read the same
// way: the initializer sets it after the addons load (0x0049011D) and the
// cascade clears it after the fire (0x0049095E). So the flag here keeps
// 3.3.5's timing: the PLAYER_LOGIN call (CALL_ENTER_WORLD_FIRE_PLAYER_LOGIN)
// is redirected to set it before the event goes out, and the
// FrameScript_Initialize cleanup, which runs before every /reload and
// /logout, clears it. The player GUID is no answer: UnitGUID("player") serves
// the char-select GUID before the world loads, and on a /reload the live GUID
// is set while the addons load.
//
// `IsLoggingOut()` / `IsPlayerInWorld()` — in-game only, as in 4.3.4.
// IsLoggingOut is 4.3.4's ClientServices::Connection()->CharacterLoggingOut();
// 1.12 has the same pair (GetCurrent → IsLogoutPending), called exactly as the
// idle-logout check in OnRender does. IsPlayerInWorld is 4.3.4's
// CGGameUI::s_inWorld; 1.12's is VAR_IN_WORLD, with the same EnterWorld /
// LeaveWorld writers.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Player::LoginStatus {

namespace {

using FireEvent_t = void(__fastcall *)(int eventID);

bool g_loginFired = false; // PLAYER_LOGIN has fired for this UI instance

void __fastcall FirePlayerLogin(int eventID) {
    g_loginFired = true;
    reinterpret_cast<FireEvent_t>(Offsets::FUN_FIRE_EVENT_NO_ARGS)(eventID);
}

int __fastcall Script_IsLoggedIn(void *L) {
    Game::Lua::PushBool(L, g_loginFired);
    return 1;
}

int __fastcall Script_IsLoggingOut(void *L) {
    using GetCurrentFn = void *(__cdecl *)();
    using IsLogoutPendingFn = uint32_t(__fastcall *)(void *conn, void *edx);
    void *conn = reinterpret_cast<GetCurrentFn>(
        static_cast<uintptr_t>(Offsets::FUN_CLIENT_SERVICES_GET_CURRENT))();
    Game::Lua::PushBool(L, reinterpret_cast<IsLogoutPendingFn>(static_cast<uintptr_t>(
                               Offsets::FUN_CLIENT_CONNECTION_IS_LOGOUT_PENDING))(conn, nullptr) != 0);
    return 1;
}

int __fastcall Script_IsPlayerInWorld(void *L) {
    Game::Lua::PushBool(L, Game::Read<uint8_t>(Offsets::VAR_IN_WORLD) != 0);
    return 1;
}

const Game::Doc::Field kLoggingOutRets[] = {
    Game::Doc::Req("isLoggingOut", "boolean", "True while a logout or quit countdown runs."),
};
const Game::Doc::Function kIsLoggingOut{
    "Returns whether the player has asked to log out and the logout has not finished or been cancelled.",
    {}, kLoggingOutRets, "SystemGlobals"};

const Game::Doc::Field kInWorldRets[] = {
    Game::Doc::Req("isInWorld", "boolean", "False during a loading screen."),
};
const Game::Doc::Function kIsPlayerInWorld{
    "Returns whether the player is in the world, that is, not on a loading screen.", {}, kInWorldRets,
    "SystemGlobals"};

void RegisterInGame() {
    Common::RedirectCall(Offsets::CALL_ENTER_WORLD_FIRE_PLAYER_LOGIN, Offsets::FUN_FIRE_EVENT_NO_ARGS,
                         reinterpret_cast<const void *>(&FirePlayerLogin));
    Game::Lua::RegisterGlobalFunction("IsLoggedIn", &Script_IsLoggedIn);
    Game::Lua::RegisterGlobalFunction("IsLoggingOut", &Script_IsLoggingOut, &kIsLoggingOut);
    Game::Lua::RegisterGlobalFunction("IsPlayerInWorld", &Script_IsPlayerInWorld, &kIsPlayerInWorld);
}

void RegisterGlue() {
    Game::Lua::RegisterGlueFunction("IsLoggedIn", &Script_IsLoggedIn);
}

void PrepareForReload() {
    g_loginFired = false;
}

} // namespace

static const Game::ModuleAutoRegister _autoreg{&RegisterInGame};
static const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};
static const Game::GlueModuleAutoRegister _gluereg{&RegisterGlue};

} // namespace Player::LoginStatus
