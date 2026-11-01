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

#include <cstdint>
#include <string>

// Opt-in persistent name cache. When enabled, every player the engine's name
// cache learns, plus anything fed in via `C_PlayerCache.RememberPlayer` from
// Lua, is persisted to one file per realm,
// `WTF\ClassicAPI\NameCache-<realm>@<realmList>.txt`, and survives across
// sessions. Every account and character on that realm shares it.
//
// Use case: addons like pfUI's libunitscan keep their own per-character SVPC
// name DB to color chat names by class. With this enabled,
// `GetPlayerInfoByGUID` / `GetPlayerInfoByName` fall back to the persistent
// cache on an engine miss, so chat coloring works after a /reload without the
// data being duplicated per character.
//
// Storage model: one entry per GUID, the engine name cache's own key, holding
// the name and the realm the server reported for the player. A name index on
// the full name (`Player::FullName`) answers the reverse lookup. Names are
// unique per realm, so a new GUID under a full name means the character was
// deleted and the name reused: the old entry goes. The file is per realm
// because player GUIDs are a per-realm counter.
//
// One copy per player: this store holds only the players the engine's name
// cache doesn't (see NameCache.cpp). Ask the engine first.

namespace Player::NameCache {

struct Entry {
    std::string name;     // as the server sent it
    std::string realm;    // the record's server name: "" for a player on this realm
    uint32_t classID;     // ChrClasses.dbc record ID, 0 if unknown
    uint32_t raceID;      // ChrRaces.dbc record ID, 0 if unknown
    uint32_t sex;         // 0 = male, 1 = female (wire convention), 0 if unknown
};

// The entry for `guid`, or nullptr. nullptr too for a player the engine's name
// cache holds, so read the engine's record first. The pointer and its strings
// are stable until the next `Remember` or engine name response; treat them as
// transient.
const Entry *Lookup(uint64_t guid);

// The GUID of the player known by `fullName` (`Player::FullName` form: "Name"
// on this realm, "Name-Realm" from another; ASCII case ignored), or 0. Like
// `Lookup`, it doesn't know the players the engine's name cache holds.
//
// Used by `C_PlayerCache.GetPlayerInfoByName` for the case where an addon has a
// player's name (e.g., from a `|Hplayer:Name|h` chat link or `CHAT_MSG_SYSTEM`
// string) but no GUID — a CHAT_MSG_* event's arg12 is "" whenever the name sits
// in the message text rather than being the sender.
uint64_t GuidForName(const std::string &fullName);

// Adds or updates an entry. `realm` is "" for a player on this realm. Zero
// values for classID/raceID/sex are treated as "caller doesn't know" and don't
// overwrite existing real data — only present (non-zero) fields replace prior
// values. Does nothing for a player the engine's name cache holds: its record
// is the server's answer, and it is saved. Marks the cache dirty for the next
// persistence flush.
void Remember(uint64_t guid, const char *name, const char *realm, uint32_t classID,
              uint32_t raceID, uint32_t sex);

// Opt-in toggle. State persists to `WTF\Account\<acct>\ClassicAPI.txt`.
// Enabling loads the file and leaves every player the engine's name cache
// already holds to the engine; disabling stops further writes but leaves
// the file in place (re-enabling restores prior state).
bool IsEnabled();
void SetEnabled(bool enabled);

// Writes any dirty state, ours and the engine's records, to disk. No-op if
// the cache is disabled, isn't dirty, or the file path can't be resolved yet
// (pre-realm). Called from DllMain's reload hook; safe to call any time
// before process exit.
void Flush();

// `Flush` for process exit: saves what this store holds and never reads the
// engine's name cache again, which the engine frees at exit. Our atexit
// handler copies the engine's records in first; DllMain's detach calls it
// for an exit that skipped the atexit handlers.
void Shutdown();

} // namespace Player::NameCache
