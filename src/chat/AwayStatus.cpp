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

// `IsChatAFK()` / `IsChatDND()` — 4.3.4's Script_IsChatAFK / Script_IsChatDND
// push CGChat::m_afk / m_dnd, the chat module's own record of the player's
// state. 1.12 keeps the AFK one (VAR_CHAT_AFK) with the same writers as 4.3.4.
// It keeps no DND one: its /dnd branch tests the player's PLAYER_FLAGS DND bit
// directly, so IsChatDND reads that bit, the value 1.12's own chat code uses.

#include "Game.h"
#include "Offsets.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Chat::AwayStatus {

namespace {

int __fastcall Script_IsChatAFK(void *L) {
    Game::Lua::PushBool(L, Game::Read<uint32_t>(Offsets::VAR_CHAT_AFK) != 0);
    return 1;
}

int __fastcall Script_IsChatDND(void *L) {
    bool dnd = false;
    if (const uint8_t *player = Unit::Identity::PlayerObject()) {
        if (const auto *info = Game::Read<const uint8_t *>(player, Offsets::OFF_CGPLAYER_INFO))
            dnd = (Game::Read<uint32_t>(info, Offsets::OFF_PLAYER_INFO_FLAGS) & Offsets::PLAYER_FLAG_DND) != 0;
    }
    Game::Lua::PushBool(L, dnd);
    return 1;
}

const Game::Doc::Field kAfkRets[] = {
    Game::Doc::Req("isAFK", "boolean", nullptr),
};
const Game::Doc::Function kIsChatAFK{
    "Returns whether the player is marked AFK. Changes as soon as /afk is typed or AFK is cleared.",
    {}, kAfkRets, "ChatGlobals"};

const Game::Doc::Field kDndRets[] = {
    Game::Doc::Req("isDND", "boolean", nullptr),
};
const Game::Doc::Function kIsChatDND{
    "Returns whether the player is marked DND.", {}, kDndRets, "ChatGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsChatAFK", &Script_IsChatAFK, &kIsChatAFK);
    Game::Lua::RegisterGlobalFunction("IsChatDND", &Script_IsChatDND, &kIsChatDND);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Chat::AwayStatus
