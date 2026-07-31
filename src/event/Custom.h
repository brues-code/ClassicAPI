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

namespace Event::Custom {

// Reserve an event name to be claimed in the engine's event table at
// the next safe opportunity. Place a static instance at file scope:
//
//   static const Event::Custom::AutoReserve _r{"MY_EVENT"};
//
// Static-init chains the name onto an internal list *before* `DllMain`
// runs. After the engine and any other DLLs have finished writing to
// the event table (signaled by the first `Frame::RegisterEvent` call
// from Lua, which our hook intercepts to fire `RetryClaims`), we walk
// the table from the END looking for NULL-name slots and claim them
// for our reserved names — engine-owned `SStrDup` storage, so the
// engine's reload teardown frees them correctly.
//
// We don't hook `RebuildEventTable` directly: chaining with other DLLs
// that hook the same function (SuperWoWhook, nampower, transmogfix,
// VanillaMinimapTracking) led to count→buffer-size mismatches and
// crashes in the engine's fill loop. The slot-claim approach lets each
// DLL operate on the table independently of the others.
//
// The name pointer must outlive the engine (a string literal does).
// Same name reserved twice is deduped; reserving more than `MAX_RESERVED`
// names total (see Custom.cpp) silently drops the overflow — keep that cap
// above the codebase-wide `AutoReserve` count.
struct AutoReserve {
    explicit AutoReserve(const char *name);
};

// Returns the slot id currently assigned to `name`, or -1 if not yet
// claimed (e.g. before the first Lua-side `RegisterEvent` has
// triggered `RetryClaims`). Slot indices may change across `/reload`,
// so call this at fire time rather than caching the value.
//
// Only consults our reservation cache — for engine-defined events
// (UNIT_FACTION, BAG_UPDATE, etc.) use `LookupByName`.
int Lookup(const char *name);

// Walks the live engine event table at `[VAR_EVENT_TABLE_BASE_PTR]`
// looking for an entry whose name strcmps equal to `name`. Returns
// the slot index, or -1 if no entry matches.
//
// Works for both engine-defined events AND our `AutoReserve`-claimed
// custom events (both populate the same table). Slightly slower than
// `Lookup` (O(table size) vs O(reservations)) — prefer `Lookup` for
// names we've reserved. Use this for engine-defined events we want to
// fire ourselves (e.g., polyfilling missing event dispatches).
int LookupByName(const char *name);

// True iff at least one frame is currently registered for the event in
// `slot` (its subscriber chain is non-empty). Reads the entry's chain
// head at `+0x0C` — a couple of pointer derefs, no allocation. Use this
// to gate expensive per-fire work (arg synthesis, DBC lookups) so an
// event nobody listens to costs almost nothing: `if (HasListeners(slot))
// { …build args…; Fire(slot, …); }`. `false` for `slot < 0` or a slot
// past the live table.
bool HasListeners(int slot);

// Dispatches a custom event via the engine's printf-style event
// dispatcher at `FUN_FIRE_EVENT` (`0x00703F50`). `format` is a
// concatenation of `%d` (int), `%u` (uint), `%f` (double), `%s`
// (const char *) tokens — one per payload arg, no separators or
// literal text. The engine has no `%b` for booleans — pass `%d`
// with `0` / `1`. String args must outlive the call (engine doesn't
// copy them out of varargs); compile-time literals are fine.
//
// Examples:
//   Fire(slot, "");                       // EQUIPMENT_SETS_CHANGED (no payload)
//   Fire(slot, "%d", setID);              // EQUIPMENT_SWAP_PENDING(setID)
//   Fire(slot, "%d%d", itemID, success);  // ITEM_DATA_LOAD_RESULT(id, ok)
//   Fire(slot, "%s%d", keyName, down);    // MODIFIER_STATE_CHANGED(key, down)
//
// No-op for `eventID < 0`, which lets callers cheaply guard on the
// `Lookup()` result without an explicit if.
template <typename... Args>
inline void Fire(int eventID, const char *format, Args... args) {
    if (eventID < 0)
        return;
    using FireEventFn_t = void(__cdecl *)(int eventID, const char *format, ...);
    auto fn = reinterpret_cast<FireEventFn_t>(Offsets::FUN_FIRE_EVENT);
    fn(eventID, format, args...);
}

// Convenience for the modern WoW `(id: number, success: bool)` event
// shape (ITEM_DATA_LOAD_RESULT, GET_ITEM_INFO_RECEIVED,
// QUEST_DATA_LOAD_RESULT, …). The engine's printf-style dispatcher has
// no `%b`, so a naive `%d%d` with `0` for failure surfaces `arg2 = 0` —
// truthy in Lua, breaking the canonical `if success then` branch. We
// push `1` for success and `nil` for failure instead, leaning on the
// engine's `lua_pushstring(L, NULL) → lua_pushnil` tail-jump so the
// dispatcher itself emits the nil without any pre-staging. Lua handlers
// can then use the natural idiom: nil is falsy, `1` is truthy.
inline void FireIdSuccess(int eventID, int id, bool success) {
    if (success)
        Fire(eventID, "%d%d", id, 1);
    else
        Fire(eventID, "%d%s", id, static_cast<const char *>(nullptr));
}

// Internal: try to claim a slot for every reservation that's still
// unclaimed (`slot < 0`). Called from the `Frame::RegisterEvent` hook
// in DllMain — every time Lua calls `frame:RegisterEvent(...)`, we
// catch any reservations that couldn't claim earlier.
void RetryClaims();

// Internal: permit `TryClaim` to write to the event table. Held
// closed until `LoadScriptFunctions_h` returns, so the boot phase
// (during which the engine and SuperWoWhook fire many internal
// `RegisterEvent` calls) can't trigger our writes. Writing during
// that window crashes the engine in `SMemFree` on slots it expected
// to still be NULL.
void EnableWrites();

// Internal: invalidate cached slot indices before `/reload`. The
// engine rebuilds the event table at a fresh allocation; our cached
// slots point into the old layout and need to re-claim.
void PrepareForReload();

} // namespace Event::Custom
