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

// `ADDONS_UNLOADING(closingClient)`.
//
// 3.80.2 fires it from the world UI unload (FUN_141692fd0) before the
// SavedVariables and AddOns.txt are written. A reload passes false; the world
// teardown passes false when it returns to the character screen and true when
// the client quits.
//
// 1.12's counterpart is CGGameUI::Shutdown (0x00490BD0), reached from a
// reload (CGGameUI::Update) and from the game teardown FUN_CLIENT_DESTROY_GAME.
// It signals PLAYER_LOGOUT and then writes Config.wtf, the SavedVariables and
// AddOns.txt, so the event fires right after PLAYER_LOGOUT, from that call
// (CALL_UI_SHUTDOWN_PLAYER_LOGOUT). The client is closing when:
//   - the stop handler runs the teardown (window close, ForceQuit, `quit`):
//     its call CALL_STOP_HANDLER_DESTROY_GAME, the only one that does not ask
//     for the character screen;
//   - or a /quit logs out first: SMSG_LOGOUT_COMPLETE tears down for the
//     character screen and then quits on the connection's quit byte
//     (OFF_CLIENT_CONNECTION_QUIT_REQUESTED), which is already set here.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"

#include <cstdint>

namespace AddOns::Unloading {

namespace {

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("closingClient", "bool",
                   "True when the client is quitting; false for a reload or a logout to the "
                   "character screen."),
};
const Game::Doc::Event kDoc{"AddOns",
                            "Fires when the interface unloads, after PLAYER_LOGOUT and before "
                            "the SavedVariables are written.",
                            kPayload};
const Event::Custom::AutoReserve kEvent{"ADDONS_UNLOADING", &kDoc};

bool g_stopping = false;

using DestroyGame_t = void(__fastcall *)(int a, int toCharacterScreen, int c);

void __fastcall StopDestroyGame(int a, int toCharacterScreen, int c) {
    g_stopping = true;
    reinterpret_cast<DestroyGame_t>(Offsets::FUN_CLIENT_DESTROY_GAME)(a, toCharacterScreen, c);
    g_stopping = false;
}

bool QuitRequested() {
    using GetCurrent_t = const uint8_t *(*)();
    const uint8_t *connection =
        reinterpret_cast<GetCurrent_t>(Offsets::FUN_CLIENT_SERVICES_GET_CURRENT)();
    return connection != nullptr &&
           connection[Offsets::OFF_CLIENT_CONNECTION_QUIT_REQUESTED] != 0;
}

using FireEvent_t = void(__fastcall *)(int eventID);

void __fastcall FireLogout(int eventID) {
    reinterpret_cast<FireEvent_t>(Offsets::FUN_FIRE_EVENT_NO_ARGS)(eventID);

    const int event = kEvent.Slot();
    void *L = Game::Lua::State();
    if (L == nullptr || !Event::Custom::HasListeners(event))
        return;
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushBool(L, g_stopping || QuitRequested());
    Event::Custom::FireValues(event, L, top + 1, 1);
    Game::Lua::SetTop(L, top);
}

void Register() {
    Common::RedirectCall(Offsets::CALL_UI_SHUTDOWN_PLAYER_LOGOUT, Offsets::FUN_FIRE_EVENT_NO_ARGS,
                         reinterpret_cast<const void *>(&FireLogout));
    Common::RedirectCall(Offsets::CALL_STOP_HANDLER_DESTROY_GAME, Offsets::FUN_CLIENT_DESTROY_GAME,
                         reinterpret_cast<const void *>(&StopDestroyGame));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace AddOns::Unloading
