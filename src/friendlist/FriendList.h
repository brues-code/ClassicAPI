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

#pragma once

#include "Offsets.h"

#include <cstdint>
#include <cstring>

// Shared access to the client's friend list and ignore list in the social
// singleton; the layout is documented at VAR_SOCIAL_SYSTEM in Offsets.h.

namespace FriendList {


// The social singleton, or null before login.
inline const uint8_t *Social() {
    return *reinterpret_cast<const uint8_t *const *>(
        static_cast<uintptr_t>(Offsets::VAR_SOCIAL_SYSTEM));
}

// Number of populated friend slots (what GetNumFriends returns).
inline int Count() {
    const uint8_t *social = Social();
    if (social == nullptr)
        return 0;
    using CountFn = uint32_t(__fastcall *)(const void *social);
    auto fn = reinterpret_cast<CountFn>(
        static_cast<uintptr_t>(Offsets::FUN_FRIEND_LIST_COUNT));
    return static_cast<int>(fn(social));
}

// True if `guid` is on the friends list — the engine's own FriendList::IsFriend.
inline bool IsGuidFriend(uint64_t guid) {
    const uint8_t *social = Social();
    if (social == nullptr || guid == 0)
        return false;
    using IsFriendFn = bool(__fastcall *)(const void *social, void *edx, uint64_t guid);
    auto fn = reinterpret_cast<IsFriendFn>(
        static_cast<uintptr_t>(Offsets::FUN_FRIEND_LIST_IS_FRIEND));
    return fn(social, nullptr, guid);
}

// Friend entry at 0-based `index`, or null if there is no list or the index
// is out of the fixed slot range. Does not bounds-check against the live
// count — callers that iterate should use `Count()`.
inline const uint8_t *EntryByIndex(int index) {
    const uint8_t *social = Social();
    if (social == nullptr || index < 0 || index >= Offsets::MAX_FRIENDS)
        return nullptr;
    return social + index * Offsets::FRIEND_ENTRY_STRIDE;
}

// The inline name of the friend with GUID `guid`, or null if that GUID is
// not on the friends list. The list keeps name + GUID for online AND offline
// friends (SMSG_FRIEND_LIST), so this resolves a GUID to a name even when the
// unit is not synced in the object manager and not in the name cache.
inline const char *NameForGuid(uint64_t guid) {
    if (guid == 0)
        return nullptr;
    const int count = Count();
    for (int i = 0; i < count; ++i) {
        const uint8_t *entry = EntryByIndex(i);
        if (entry == nullptr)
            break;
        if (*reinterpret_cast<const uint64_t *>(entry + Offsets::OFF_FRIEND_GUID) == guid) {
            const char *name =
                *reinterpret_cast<const char *const *>(entry + Offsets::OFF_FRIEND_NAME);
            return (name != nullptr && *name != '\0') ? name : nullptr;
        }
    }
    return nullptr;
}

// The GUID of the friend named `name` (case-insensitive, like the engine's
// own name compares), or 0 if no friend has that name. The reverse of
// `NameForGuid` — offline friends resolve too.
inline uint64_t GuidForName(const char *name) {
    if (name == nullptr || *name == '\0')
        return 0;
    const int count = Count();
    for (int i = 0; i < count; ++i) {
        const uint8_t *entry = EntryByIndex(i);
        if (entry == nullptr)
            break;
        const char *fname =
            *reinterpret_cast<const char *const *>(entry + Offsets::OFF_FRIEND_NAME);
        if (fname != nullptr && _stricmp(fname, name) == 0)
            return *reinterpret_cast<const uint64_t *>(entry + Offsets::OFF_FRIEND_GUID);
    }
    return 0;
}

// The ignore-list GUID at 0-based `index`, or 0 if there is no list or the
// index is past the fixed cap. Mirrors FUN_005AE570.
inline uint64_t IgnoreGuidByIndex(int index) {
    const uint8_t *social = Social();
    if (social == nullptr || index < 0 || index >= Offsets::MAX_IGNORES)
        return 0;
    return *reinterpret_cast<const uint64_t *>(
        social + Offsets::OFF_IGNORE_LIST + index * Offsets::IGNORE_ENTRY_STRIDE);
}

// True if `guid` is on the ignore list — the engine's own FriendList::IsIgnored,
// which scans all 25 slots because the list can hold zero-GUID holes.
inline bool IsGuidIgnored(uint64_t guid) {
    const uint8_t *social = Social();
    if (social == nullptr || guid == 0)
        return false;
    using IsIgnoredFn = bool(__fastcall *)(const void *social, void *edx, uint64_t guid);
    auto fn = reinterpret_cast<IsIgnoredFn>(
        static_cast<uintptr_t>(Offsets::FUN_FRIEND_LIST_IS_IGNORED));
    return fn(social, nullptr, guid);
}

} // namespace FriendList
