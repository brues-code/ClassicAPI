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
// Returns `true` once a character has been loaded *and identified*
// (GUID populated), `false` on glue, pre-load, and the brief window
// between struct allocation and character-data unpack.
//
// Check is two-step:
//   1. `[VAR_LOCAL_PLAYER_PTR]` non-NULL — the player aggregator
//      struct has been allocated
//   2. `*(uint64*)(player + OFF_LOCAL_PLAYER_GUID)` non-zero — the
//      character's GUID has been populated into the struct
//
// The pointer alone is too lax: the engine allocates the struct
// shortly before the character data lands, so there's a window where
// the pointer is non-NULL but the GUID is still zero. Most addons
// that check `IsLoggedIn` go straight to `UnitGUID("player")` next,
// and we don't want them to see GUID==nil right after IsLoggedIn
// reported true.
//
// 3.3.5 uses a single byte flag at `[0x00BD0793]` cleared on world
// entry — we couldn't pin down the analogous 1.12 flag (the engine
// path that signals event 253 wires through CVar-init machinery and
// doesn't map cleanly across the binary). The pointer+GUID check is
// the same effective signal: both flip together at the moment the
// player object is ready, and stay stable across zone transitions.
//
// `IsLoggingOut()` / `IsPlayerInWorld()` — in-game only, as in 4.3.4.
// IsLoggingOut is 4.3.4's ClientServices::Connection()->CharacterLoggingOut();
// 1.12 has the same pair (GetCurrent → IsLogoutPending), called exactly as the
// idle-logout check in OnRender does. IsPlayerInWorld is 4.3.4's
// CGGameUI::s_inWorld; 1.12's is VAR_IN_WORLD, with the same EnterWorld /
// LeaveWorld writers.

#include "Game.h"
#include "Offsets.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Player::LoginStatus {

namespace {

int __fastcall Script_IsLoggedIn(void *L) {
    Game::Lua::PushBool(L, Unit::Identity::PlayerGuid() != 0);
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
    Game::Lua::RegisterGlobalFunction("IsLoggedIn", &Script_IsLoggedIn);
    Game::Lua::RegisterGlobalFunction("IsLoggingOut", &Script_IsLoggingOut, &kIsLoggingOut);
    Game::Lua::RegisterGlobalFunction("IsPlayerInWorld", &Script_IsPlayerInWorld, &kIsPlayerInWorld);
}

void RegisterGlue() {
    Game::Lua::RegisterGlueFunction("IsLoggedIn", &Script_IsLoggedIn);
}

} // namespace

static const Game::ModuleAutoRegister _autoreg{&RegisterInGame};
static const Game::GlueModuleAutoRegister _gluereg{&RegisterGlue};

} // namespace Player::LoginStatus
