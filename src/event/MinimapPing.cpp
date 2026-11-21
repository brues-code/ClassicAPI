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

// MINIMAP_PING to event callbacks.
//
// 3.80.2 delivers MINIMAP_PING to callbacks only (FUN_140d3e3d0), as
// (owner, unitToken, x, y). In 1.12 it is a frame event that the stock
// Minimap.lua registers, so the frame event stays and the callbacks run after
// it, from the same signal (CALL_MINIMAP_PING_FIRE), with 1.12's token and
// numbers. 3.80.2 runs the plain callbacks once for each token that matches
// the pinger; here there is the one token 1.12 resolves. The callbacks
// registered for that token run after the plain ones.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "event/Callback.h"

namespace Event::MinimapPing {

namespace {

using FireEvent_t = void(__cdecl *)(int eventID, const char *format, ...);

void __cdecl FirePing(int eventID, const char *format, const char *token, double x, double y) {
    reinterpret_cast<FireEvent_t>(Offsets::FUN_FIRE_EVENT)(eventID, format, token, x, y);

    if (!Event::Callback::HasCallbacks(Event::Callback::MINIMAP_PING))
        return;
    void *L = Game::Lua::State();
    if (L == nullptr || Game::Lua::CheckStack(L, 3) == 0)
        return;
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, token);
    Game::Lua::PushNumber(L, x);
    Game::Lua::PushNumber(L, y);
    Event::Callback::Fire(Event::Callback::MINIMAP_PING, L, top + 1, 3, token);
    Game::Lua::SetTop(L, top);
}

void Register() {
    Common::RedirectCall(Offsets::CALL_MINIMAP_PING_FIRE, Offsets::FUN_FIRE_EVENT,
                         reinterpret_cast<const void *>(&FirePing));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Event::MinimapPing
