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

// `GetCurrentChatGUID()` — call from inside an addon's OnEvent for
// any CHAT_MSG_* to get the sender's GUID. Vanilla 1.12 omits the
// GUID from chat event args (added as arg12 of CHAT_MSG_* only in
// 3.0+), so addons currently strcmp the localized sender name string
// against an internal cache to identify the player. With this we
// just read the GUID directly.
//
// The GUID is published for the duration of a chat dispatch by a
// `DispatchScope` (see the header), which Chat::Dispatch — the single
// owner of the FUN_0049A870 chat-dispatch hook — constructs around the
// original call. The engine fires the CHAT_MSG_* event synchronously
// inside that call, so `GetCurrentChatGUID` reads the right GUID from
// the addon's OnEvent. The dispatcher is also called for synthetic chat
// (system notifications, arena team events, etc.) with GUID = 0; for
// those `GetCurrentChatGUID()` returns nil — matching the modern API's
// behavior of a nil GUID for non-player-sourced chat.

#include "CurrentGUID.h"

#include "Game.h"
#include "guid/Guid.h"

#include <cstdint>

namespace Chat::CurrentGUID {

namespace {

uint32_t g_currentGuidLo = 0;
uint32_t g_currentGuidHi = 0;

int __fastcall Script_GetCurrentChatGUID(void *L) {
    if (g_currentGuidLo == 0 && g_currentGuidHi == 0)
        return 0;
    const uint64_t guid =
        (static_cast<uint64_t>(g_currentGuidHi) << 32) | g_currentGuidLo;
    char buf[Guid::STRING_SIZE];
    Game::Lua::PushString(L, Guid::FormatAsString(guid, buf, sizeof buf));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetCurrentChatGUID",
                                       &Script_GetCurrentChatGUID);
}

} // namespace

DispatchScope::DispatchScope(uint32_t guidLo, uint32_t guidHi)
    : prevLo_(g_currentGuidLo), prevHi_(g_currentGuidHi) {
    g_currentGuidLo = guidLo;
    g_currentGuidHi = guidHi;
}

DispatchScope::~DispatchScope() {
    g_currentGuidLo = prevLo_;
    g_currentGuidHi = prevHi_;
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Chat::CurrentGUID
