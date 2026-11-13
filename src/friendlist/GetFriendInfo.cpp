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

// `C_FriendList.GetFriendInfo(name)` / `GetFriendInfoByIndex(index)` — the
// modern FriendInfo table, plus `C_FriendList.GetNumFriends()`.
//
// Reads the friend entries directly (see `FriendList.h`). The class and zone
// names come from the same DBC lookups the engine's own GetFriendInfo uses:
// ChrClasses.dbc by the entry's class index, and AreaTable.dbc by the area
// index — resolved to the parent zone so a sub-area reports its zone. Both
// are locale-applied through `DBC::LocalizedField`.
//
// vanilla 1.12 stores no friend notes, so `notes` is omitted (nil). `mobile`
// and `referAFriend` are always false, and `rafLinkType` is 0 — vanilla has
// none of those systems; the fields exist so modern consumers do not nil-out.

#include "FriendList.h"
#include "Notes.h"

#include "Game.h"
#include "dbc/Names.h"
#include "guid/Guid.h"

#include <cstdint>
#include <cstring>

namespace FriendList::Info {

namespace {

// Push the modern FriendInfo table for `entry` on top of the Lua stack.
void PushFriendInfo(void *L, const uint8_t *entry) {
    const bool connected = *(entry + OFF_FRIEND_CONNECTED) != 0;
    const char *name =
        *reinterpret_cast<const char *const *>(entry + OFF_FRIEND_NAME);
    const int level = *reinterpret_cast<const int *>(entry + OFF_FRIEND_LEVEL);
    const uint64_t guid =
        *reinterpret_cast<const uint64_t *>(entry + OFF_FRIEND_GUID);
    const uint8_t status = *(entry + OFF_FRIEND_STATUS);
    const uint32_t classIndex =
        *reinterpret_cast<const uint32_t *>(entry + OFF_FRIEND_CLASS_INDEX);
    const uint32_t areaIndex =
        *reinterpret_cast<const uint32_t *>(entry + OFF_FRIEND_AREA_INDEX);

    Game::Lua::NewTable(L);
    Game::Lua::SetFieldString(L, "name", name);
    Game::Lua::SetFieldBool(L, "connected", connected);
    Game::Lua::SetFieldNumber(L, "level", static_cast<double>(level));
    // Optional strings: leave the field nil (unset) when unresolved rather
    // than "" — SetFieldString coerces null to "", which is truthy in Lua and
    // would fool an `if info.notes then` presence check. Matches retail, where
    // these are nil when absent.
    if (const char *className = DBC::ClassName(classIndex))
        Game::Lua::SetFieldString(L, "className", className);
    // classFilename: the locale-independent class token ("MAGE", "WARRIOR"),
    // the key for RAID_CLASS_COLORS and other class tables. nil when unknown,
    // so `className` and `classFilename` appear together.
    if (const char *classToken = DBC::ClassToken(classIndex))
        Game::Lua::SetFieldString(L, "classFilename", classToken);
    // Resolve a sub-area to its parent zone for friends (e.g. "Goldshire" ->
    // "Elwynn Forest"), matching what the friends UI shows.
    if (const char *area = DBC::AreaName(areaIndex, /*resolveToParent=*/true))
        Game::Lua::SetFieldString(L, "area", area);
    if (guid != 0) {
        char buf[Guid::STRING_SIZE];
        Game::Lua::SetFieldString(L, "guid",
                                  Guid::FormatAsString(guid, buf, sizeof buf));
    }
    if (const char *note = Notes::Get(guid)) // client-side note (see Notes.h)
        Game::Lua::SetFieldString(L, "notes", note);
    // A logoff (SMSG_FRIEND_STATUS result 3 → SetFriendStatus, 0x005AEB30)
    // clears only the online byte and leaves the status byte, so gate on
    // `connected` — as the engine's GetFriendInfo does for its status text.
    Game::Lua::SetFieldBool(L, "afk", connected && (status & FRIEND_STATUS_AFK) != 0);
    Game::Lua::SetFieldBool(L, "dnd", connected && (status & FRIEND_STATUS_DND) != 0);
    Game::Lua::SetFieldBool(L, "mobile", false);
    Game::Lua::SetFieldBool(L, "referAFriend", false);
    Game::Lua::SetFieldNumber(L, "rafLinkType", 0);
}

const Game::Doc::Field kFriendInfoFields[] = {
    Game::Doc::Req("connected", "bool"),
    Game::Doc::Req("name", "string"),
    Game::Doc::Opt("className", "string"),
    Game::Doc::Opt("classFilename", "string", nullptr,
                   "The class token, such as \"MAGE\"; present whenever className is."),
    Game::Doc::Opt("area", "string", nullptr, "The zone, never a sub-area."),
    Game::Doc::Opt("notes", "string", nullptr, "Nil when the friend has no note."),
    Game::Doc::Opt("guid", "WOWGUID"),
    Game::Doc::Req("level", "number", "0 when not known."),
    Game::Doc::Req("dnd", "bool"),
    Game::Doc::Req("afk", "bool"),
    Game::Doc::Req("mobile", "bool", "Always false."),
    Game::Doc::Req("referAFriend", "bool", "Always false."),
    Game::Doc::Req("rafLinkType", "number", "Always 0."),
};
const Game::Doc::Structure kFriendInfo{"FriendInfo", "FriendList", kFriendInfoFields,
                                       "One player on the friends list."};

const Game::Doc::Field kGetNumFriendsRets[] = {
    Game::Doc::Req("numFriends", "number"),
};
const Game::Doc::Function kGetNumFriends{"Returns the number of players on the friends list.",
                                         {}, kGetNumFriendsRets};

int __fastcall Script_GetNumFriends(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(FriendList::Count()));
    return 1;
}

// `C_FriendList.GetNumOnlineFriends()` — how many friends are online. Vanilla
// stores no online count, so we count entries whose `connected` byte is set
// (the same flag GetFriendInfo reports as `connected`).
const Game::Doc::Field kGetNumOnlineFriendsRets[] = {
    Game::Doc::Req("numOnline", "number"),
};
const Game::Doc::Function kGetNumOnlineFriends{
    "Returns the number of players on the friends list who are online.", {},
    kGetNumOnlineFriendsRets};

int __fastcall Script_GetNumOnlineFriends(void *L) {
    int online = 0;
    const int count = FriendList::Count();
    for (int i = 0; i < count; ++i) {
        const uint8_t *entry = FriendList::EntryByIndex(i);
        if (entry == nullptr)
            break;
        if (*(entry + OFF_FRIEND_CONNECTED) != 0)
            ++online;
    }
    Game::Lua::PushNumber(L, static_cast<double>(online));
    return 1;
}

const Game::Doc::Field kGetFriendInfoByIndexArgs[] = {
    Game::Doc::Req("index", "luaIndex"),
};
const Game::Doc::Field kFriendInfoRets[] = {
    Game::Doc::Opt("info", "FriendInfo", nullptr, "Nil when there is no such friend."),
};
const Game::Doc::Function kGetFriendInfoByIndex{
    "Returns information about a friend by position in the friends list.",
    kGetFriendInfoByIndexArgs, kFriendInfoRets};

int __fastcall Script_GetFriendInfoByIndex(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const int index = static_cast<int>(Game::Lua::ToNumber(L, 1)); // 1-based
    if (index < 1 || index > FriendList::Count()) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const uint8_t *entry = FriendList::EntryByIndex(index - 1);
    if (entry == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    PushFriendInfo(L, entry);
    return 1;
}

const Game::Doc::Field kGetFriendInfoArgs[] = {
    Game::Doc::Req("name", "cstring", "The friend's name; case does not matter."),
};
const Game::Doc::Function kGetFriendInfo{"Returns information about a friend by name.",
                                         kGetFriendInfoArgs, kFriendInfoRets};

int __fastcall Script_GetFriendInfo(void *L) {
    const char *name = Game::Lua::IsString(L, 1) ? Game::Lua::ToString(L, 1) : nullptr;
    if (name == nullptr || *name == '\0') {
        Game::Lua::PushNil(L);
        return 1;
    }
    const int count = FriendList::Count();
    for (int i = 0; i < count; ++i) {
        const uint8_t *entry = FriendList::EntryByIndex(i);
        if (entry == nullptr)
            break;
        const char *fname =
            *reinterpret_cast<const char *const *>(entry + OFF_FRIEND_NAME);
        if (fname != nullptr && _stricmp(fname, name) == 0) {
            PushFriendInfo(L, entry);
            return 1;
        }
    }
    Game::Lua::PushNil(L);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_FriendList", "GetNumFriends",
                                     &Script_GetNumFriends, &kGetNumFriends);
    Game::Lua::RegisterTableFunction("C_FriendList", "GetNumOnlineFriends",
                                     &Script_GetNumOnlineFriends, &kGetNumOnlineFriends);
    Game::Lua::RegisterTableFunction("C_FriendList", "GetFriendInfoByIndex",
                                     &Script_GetFriendInfoByIndex, &kGetFriendInfoByIndex);
    Game::Lua::RegisterTableFunction("C_FriendList", "GetFriendInfo",
                                     &Script_GetFriendInfo, &kGetFriendInfo);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace FriendList::Info
