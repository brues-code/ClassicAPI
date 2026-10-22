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

// `C_FriendList.IsFriend(guid)` — is the given player on the friends list?
//
// A GUID goes to the engine's own FriendList::IsFriend (`IsGuidFriend`). The
// friend list keeps the GUID at +0x08 for online AND offline friends, so no
// name fallback is needed for GUID input. As a ClassicAPI convenience the arg
// may also be a bare character name, since vanilla's friends list is
// fundamentally name-keyed (AddFriend(name)). See `FriendList.h`.

#include "FriendList.h"

#include "Game.h"
#include "guid/Guid.h"

#include <cstdint>

namespace FriendList::IsFriend {

int __fastcall Script_IsFriend(void *L) {
    const char *arg = Game::Lua::IsString(L, 1) ? Game::Lua::ToString(L, 1) : nullptr;
    if (arg == nullptr || *arg == '\0') {
        Game::Lua::PushBool(L, false);
        return 1;
    }

    uint64_t guid = 0;
    if (!Guid::Parse(arg, &guid) || guid == 0)
        guid = FriendList::GuidForName(arg); // not a GUID → treat it as a character name

    Game::Lua::PushBool(L, FriendList::IsGuidFriend(guid));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_FriendList", "IsFriend", &Script_IsFriend);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace FriendList::IsFriend
