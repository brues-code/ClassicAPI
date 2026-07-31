// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

#pragma once

#include <cstddef>
#include <cstdint>

// Unit identity helpers shared across modules. The Lua surface
// (`UnitGUID` / `UnitTokenFromGUID`) lives in `Identity.cpp`; these are the
// reusable C++ cores other modules call directly (e.g. `Aura::Data`
// resolving an aura's caster GUID to a `sourceUnit` token).

namespace Unit::Identity {

// The local player's 64-bit GUID, read from the canonical local-player
// pointer (`VAR_LOCAL_PLAYER_PTR + OFF_LOCAL_PLAYER_GUID`). Independent of
// object-manager state, so it stays valid across zone transitions / brief
// CGPlayer rebuilds. Returns 0 before login (pointer null) or before the
// GUID is populated. This is the canonical "who am I" read — prefer it over
// re-dereferencing the globals inline.
uint64_t PlayerGuid();

// The local player's in-world object pointer, resolved straight from the
// object manager by GUID (`FUN_OBJECT_RESOLVE_BY_GUID`) — the fastest path we
// have (one hash lookup, no token-string work) and naturally non-throwing:
// null before the player object exists (pre-world) or before char-select,
// never a raise. Safe from any context — Lua callback, world tick, packet
// hook. Deliberately NOT `VAR_LOCAL_PLAYER_PTR`: that global carries the
// player GUID at +0xC0 but is NOT the CGPlayer whose descriptor and
// CGPlayer-local fields are readable (see Offsets.h).
const uint8_t *PlayerObject();

// The local player's `m_objectFields` descriptor (`PlayerObject() +
// OFF_UNIT_DESCRIPTOR`), or null. This is the broadcast UpdateField block
// (health / power / flags / UNIT_FIELD_CHANNEL_SPELL / …). Prefer this over
// re-deriving the player object + descriptor offset in each module.
const uint8_t *PlayerDescriptor();

// The local player's inventory manager (`PlayerObject() +
// OFF_PLAYER_INVENTORY_MANAGER`), or null. Layout: slot count at `+0x00`,
// flat GUID array at `+OFF_INVMGR_GUID_ARRAY`; it's the `this` that
// `GetItemBySlot` expects. This is the SAME pointer the throwing token
// resolver (`FUN_RESOLVE_UNIT_TOKEN("player")`) yields — both resolve the
// local player's GUID through the object manager (verified: the token
// resolver is `TokenToGUID → FUN_OBJECT_RESOLVE_BY_GUID`, differing only in
// the type-mask, which both the player object passes) — but non-throwing
// (null pre-world). Prefer this over re-deriving `player + 0x1D38` per module.
const uint8_t *PlayerInventoryManager();

// The 64-bit GUID of a resolved CGUnit/CGObject pointer, read through the
// pointer at `+0x08` to an instance block whose first 8 bytes are the GUID
// (verified in `Script_GetInventoryItemLink`; see `OFF_UNIT_GUID_PTR`).
// Returns 0 if `unitObject` or the block pointer is null. Use this for any
// "GUID of this object pointer" read so callers stay consistent (the aura
// caster cache keys on it from both the lookup and the OnAuraAdded hook).
uint64_t GuidForObject(const void *unitObject);

// Case-insensitive match against the literal `"player"` token, mirroring
// the engine's `SStrCmpI(token, "player")` fast-path tests (e.g.
// `Script_UnitClass`/`Script_UnitRace`, which special-case `"player"` to
// read login-time globals available before the in-world descriptor spawns).
// Returns false for a null pointer.
bool IsPlayerToken(const char *token);

// Resolves a unit token (`"player"`, `"target"`, `"party3"`, …) to its
// 64-bit GUID, or 0 if the token currently maps to nothing. `"player"` is
// read from the canonical local-player pointer so it works regardless of
// object-manager state; everything else goes through the engine's
// token→GUID resolver (so out-of-range party/raid members resolve too).
// NOTE: raises a Lua error for tokens the engine doesn't recognize — only
// pass known unit tokens.
uint64_t GuidForToken(const char *token);

// Best-effort reverse lookup: given a unit GUID, writes the first unit
// token currently mapped to it into `buf` and returns `buf`, or returns
// nullptr if no known token points at it (or `guid` is 0). The mapping is
// unstable across frames — callers should treat the result as a snapshot.
// `buf` should be at least 32 bytes.
const char *TokenFromGUID(uint64_t guid, char *buf, size_t bufSize);

// Enumerate EVERY unit token currently mapping to `guid` (not just the
// first, unlike `TokenFromGUID`). Writes each token string into `out[i]`
// (each row must hold >= 32 bytes) and returns the count, capped at
// `maxTokens`. Engine-native tokens (player/pet/target/party/partypet/raid/
// raidpet/npc/mouseover) come from the engine's reverse map
// (`FUN_UNIT_TOKENS_FROM_GUID`); ClassicAPI's synthetic `focus` /
// `nameplateN` are appended on top. The result is snapshotted into `out`
// (safe to fire re-entrant Lua events while iterating). Returns 0 pre-world
// or when no token maps to `guid`. Used for the per-token `UNIT_SPELLCAST_*`
// remote-unit fan-out.
int TokensForGUID(uint64_t guid, char (*out)[32], int maxTokens);

// The engine's cached player-info record for `guid` — the NameCache data block
// (name @ `OFF_PLAYER_INFO_NAME`, realm, race @ `_RACE`, sex, class @ `_CLASS`),
// or null if the GUID isn't cached. Object-independent: it resolves players who
// are out of range / on another map / have left the group, since the cache is
// populated on sync (SMSG_NAME_QUERY_RESPONSE). The single primitive for "what
// do we know about this player GUID" — read fields via the `OFF_PLAYER_INFO_*`
// offsets. Called with a NULL callback, so it only peeks (never fires a query).
const uint8_t *PlayerInfoRecord(uint64_t guid);

} // namespace Unit::Identity
