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

// Opt-in persistent name cache, keyed by **name**. When enabled,
// every SMSG_NAME_QUERY_RESPONSE the engine processes — plus
// anything fed in via `C_PlayerCache.RememberPlayer` from Lua — is
// persisted to `WTF\Account\<acct>\<realm>\ClassicAPI_NameCache.txt`
// and survives across sessions on the same realm. Shared across all
// characters on that account+realm, so a 50-character bank alt's
// cache is available to the player's main without duplication.
//
// Use case: addons like pfUI's libunitscan currently keep their own
// per-character SVPC name DB to color chat names by class. With
// this enabled, `GetPlayerInfoByGUID` / `GetPlayerInfoByName` falls
// back to the persistent cache on engine miss, so chat coloring
// Just Works after a /reload — and the data isn't duplicated 50
// times.
//
// Storage model: **name is the primary key**. Vanilla 1.12 enforces
// per-realm name uniqueness server-side, so each name maps to at
// most one live character. GUIDs are unstable — character deletion
// + recreation produces a new GUID for the same name — so keying by
// GUID would mean storing dead identities and needing a name-based
// eviction pass to clean them up. Keying by name makes dedup
// natural (insert-replaces) and eliminates the stale-GUID problem.
// A `guid → name` reverse index gives O(1) lookups in both
// directions. GUIDs are still persisted so addons can recover a
// name from a stored GUID (combat-log replay, etc.).

namespace Player::NameCache {

struct Entry {
    uint64_t guid;       // full 64-bit GUID (hi << 32 | lo)
    uint32_t classID;    // ChrClasses.dbc record ID (1..9), 0 if unknown
    uint32_t raceID;     // ChrRaces.dbc record ID (1..8), 0 if unknown
    uint32_t sex;        // 0 = male, 1 = female (wire convention), 0 if unknown
};

// O(1) lookup by GUID via the reverse index. On hit, returns a
// pointer to the entry and writes the player's name to `*outName`.
// On miss, returns nullptr and sets `*outName` to nullptr. The
// returned pointer and `*outName` are stable until the next
// `Remember` / eviction; treat as transient.
//
// The name out-param is the entry's key in the primary map —
// passing it by pointer keeps it cheap (no string copy) and lets
// the caller read both halves from one lookup.
const Entry *Lookup(uint64_t guid, const std::string **outName);

// O(1) lookup by whole name, ignoring ASCII case like the engine's own
// name compares, so `"gedwyr"` finds `"Gedwyr"`. Returns nullptr if no
// entry has that name.
//
// Used by `C_PlayerCache.GetPlayerInfoByName` for the case where
// an addon has a player's name (e.g., from a `|Hplayer:Name|h`
// chat link or `CHAT_MSG_SYSTEM` string) but no GUID —
// a CHAT_MSG_* event's arg12 is "" whenever the name sits in the
// message text rather than being the sender.
const Entry *LookupByName(const char *name);

// Adds or updates an entry. Zero values for classID/raceID/sex are
// treated as "caller doesn't know" and don't overwrite existing
// real data — only present (non-zero) fields replace prior values.
// Marks the cache dirty for the next persistence flush.
void Remember(uint64_t guid, const char *name, uint32_t classID,
              uint32_t raceID, uint32_t sex);

// Opt-in toggle. State persists to `WTF\Account\<acct>\ClassicAPI.txt`.
// Enabling triggers a load from disk; disabling stops further writes
// but leaves the file in place (re-enabling restores prior state).
bool IsEnabled();
void SetEnabled(bool enabled);

// Writes any dirty in-memory state to disk. No-op if the cache is
// disabled, isn't dirty, or the file path can't be resolved yet
// (pre-realm). Called from DllMain's reload + detach hooks; safe to
// call any time. Bounded by GetTickCount-based throttling internally,
// so calling repeatedly in tight succession is cheap.
void Flush();

// Opportunistic per-event tick. Called from the engine's
// `Frame::RegisterEvent` hook (which fires whenever any addon
// registers a Lua event handler). Internally throttled — only
// actually runs the visible-object scan once per `kScanIntervalMs`,
// and only when both the cache and the visible-scan toggle are
// enabled. Safe to call any time; cheap on the no-op path.
void Tick();

} // namespace Player::NameCache
