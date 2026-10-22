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

// `C_FriendList.IsIgnored(token)` / `C_FriendList.IsIgnoredByGuid(guid)` — is
// the given player on the ignore list?
//
// The ignore list is a flat array of u64 GUIDs in the social singleton (see
// `FriendList.h`) — vanilla ignores are GUID-keyed, not name-keyed. So
// `IsIgnoredByGuid` is a direct GUID walk, and `IsIgnored` accepts either a
// GUID string or a character name: for a name it resolves each ignored GUID to
// its cached name through the NameCache and compares (the same GUID→name path
// GetIgnoreName uses), so it matches any ignored player the client has synced.
// An ignored player never seen this session has no cached name and so cannot
// match by name — identical to GetIgnoreName returning "Unknown" for them.

#include "FriendList.h"

#include "Game.h"
#include "guid/Guid.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstring>

namespace FriendList::IsIgnored {

namespace {

// True if any ignored GUID matches `guid`, or (when `name` is given) resolves
// to `name`. Single walk of all 25 slots, skipping zero holes (see
// `FriendList.h` — the array is not always contiguous).
bool IgnoredByGuidOrName(uint64_t guid, const char *name) {
    for (int i = 0; i < FriendList::MAX_IGNORES; ++i) {
        const uint64_t entry = FriendList::IgnoreGuidByIndex(i);
        if (entry == 0)
            continue;
        if (guid != 0 && entry == guid)
            return true;
        if (name != nullptr) {
            const char *ename = Unit::Identity::NameForGuid(entry);
            if (ename != nullptr && _stricmp(ename, name) == 0)
                return true;
        }
    }
    return false;
}

} // namespace

// `IsIgnored(token)` — token is a character name or a GUID string.
int __fastcall Script_IsIgnored(void *L) {
    const char *arg = Game::Lua::IsString(L, 1) ? Game::Lua::ToString(L, 1) : nullptr;
    if (arg == nullptr || *arg == '\0') {
        Game::Lua::PushBool(L, false);
        return 1;
    }

    uint64_t guid = 0;
    const char *name = nullptr;
    if (Guid::Parse(arg, &guid) && guid != 0)
        name = Unit::Identity::NameForGuid(guid); // GUID input → name for the fallback compare
    else
        name = arg; // not a GUID → treat the string as a character name

    Game::Lua::PushBool(L, IgnoredByGuidOrName(guid, name));
    return 1;
}

// `IsIgnoredByGuid(guid)` — GUID string only.
int __fastcall Script_IsIgnoredByGuid(void *L) {
    const char *arg = Game::Lua::IsString(L, 1) ? Game::Lua::ToString(L, 1) : nullptr;
    uint64_t guid = 0;
    if (arg == nullptr || !Guid::Parse(arg, &guid) || guid == 0) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    Game::Lua::PushBool(L, FriendList::IsGuidIgnored(guid));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_FriendList", "IsIgnored", &Script_IsIgnored);
    Game::Lua::RegisterTableFunction("C_FriendList", "IsIgnoredByGuid",
                                     &Script_IsIgnoredByGuid);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace FriendList::IsIgnored
