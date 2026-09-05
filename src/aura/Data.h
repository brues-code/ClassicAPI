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

#include "Offsets.h"

#include <cstdint>

// Shared aura-table primitives consumed by `C_UnitAuras.*` (and any
// future module that needs to inspect a unit's aura array). All
// reads go through the unit's `m_objectFields` descriptor at
// `unit + OFF_CGUNIT_OBJECT_FIELDS`; the layout is documented in
// `Offsets.h` under `OFF_UNIT_FIELD_AURA*`.
//
// Slots are absolute 0..47, and how polarity is read out of them DEPENDS ON THE
// SERVER. `IsSlotHarmful` owns that decision; nothing else should look at either
// signal directly.
//
// Normally the slot range IS the polarity: positive auras are written to 0..31
// and negative ones to 32..47 by a strict either/or, so nothing crosses over.
// The flag nibble is no help — in a stock descriptor those bits record which
// spell effect indices carry an aura, not whether the aura is good or bad.
//
// Turtle changes both halves. Once the 16 harmful slots are full it spills
// further debuffs into 0..31 (setting UNIT_FLAG_AURAS_VISIBLE so the client
// still renders them), which makes the range unreliable — and it repurposes the
// nibble to carry the true polarity so the information survives the spill.
// There, the nibble is authoritative. See `Offsets::UNIT_AURA_FLAG_HARMFUL`.
//
// Either way `Filter` selects on the aura's REAL polarity, so a spilled debuff
// is reported as a debuff. The engine's own UnitBuff/UnitDebuff split purely by
// slot range and would call it a buff; that is a 1.12 UI limitation we
// deliberately do not mirror, since retail's contract is actual polarity. A
// polarity walk visits its home range first and then the other range, so
// indices are unchanged wherever no spill has happened — which is everywhere,
// on a server that does not spill.
//
// Callers that take a 1-based Lua index into "buffs" or "debuffs" translate
// to the absolute 0..47 slot before calling in.

namespace Aura::Data {

// Filter for slot iteration: the aura's polarity per its flag nibble (see the
// file header). The engine's `UnitBuff` / `UnitDebuff` pick one direction;
// modern `C_UnitAuras.GetAuraDataByIndex` defaults to helpful when no filter
// is specified.
enum class Filter { Helpful, Harmful };

// How a resolved aura is emitted onto the Lua stack by the single-aura push
// paths. `Table` builds the modern `AuraData` table (net stack +1); `Positional`
// pushes the Classic-Era `UnitAura` 15-value tuple (net stack +15) with NO table
// allocation, for the `C_UnitAuras.UnitAura/UnitBuff/UnitDebuff` accessors that
// avoid the per-call GC churn. Selection + timing resolution are identical for
// both — only the terminal leaf differs.
enum class Emit { Table, Positional };

// Caster restriction from the `PLAYER` / `!PLAYER` aura filter tokens.
// `Any` = no restriction; `PlayerOnly` = only auras the local player cast
// (`PLAYER`); `NotPlayer` = only auras NOT cast by the local player
// (`!PLAYER`). Caster attribution comes from the `Aura::Source` cache; a
// cache miss counts as "not the player" (so `PlayerOnly` excludes it and
// `NotPlayer` includes it — matching `IsPlayerCast`'s miss semantics).
enum class CasterMode { Any, PlayerOnly, NotPlayer };

// Restriction from the `DISPELLABLE` / `!DISPELLABLE` aura filter tokens.
// `Any` = no restriction; `DispellableOnly` = only auras that can be
// dispelled / purged / stolen by SOME mechanism (dispel type Magic, Curse,
// Disease, or Poison — the server's DISPEL_ALL_MASK); `NotDispellable` = the
// complement. "Regardless of whether the local player can dispel it" —
// matching the modern AuraFilters semantics.
enum class DispelMode { Any, DispellableOnly, NotDispellable };

// Restriction from the `CROWD_CONTROL` / `!CROWD_CONTROL` aura filter tokens.
// `Any` = no restriction; `CrowdControlOnly` = only auras that are a crowd-
// control effect (stun / fear / silence / root / etc., per
// `Spell::CrowdControl`); `NotCrowdControl` = the complement.
enum class CcMode { Any, CrowdControlOnly, NotCrowdControl };

// A parsed aura-filter's per-aura predicates (beyond the helpful/harmful
// range, which is the separate `Filter`). Extensible: new orthogonal filter
// dimensions add a field here rather than another function parameter.
struct Match {
    CasterMode caster = CasterMode::Any;
    DispelMode dispel = DispelMode::Any;
    CcMode cc = CcMode::Any;
};

// Reads the spell ID at the given absolute slot (0..47). Returns 0
// if the slot is empty or the unit pointer is null.
uint32_t ReadSpellID(const uint8_t *unit, int slot);

// True iff the slot is occupied AND the engine considers the aura
// applied (descriptor flag nibble & UNIT_AURA_VISIBLE_MASK != 0)
// AND the spell record passes the engine's visibility predicate.
// This is the same gate `Script_UnitBuff`/`Script_UnitDebuff` use
// to decide whether to surface an aura through Lua.
bool IsSlotPopulated(const uint8_t *unit, int slot);

// The aura's real polarity, read the way this server encodes it: the slot range
// normally, the flag nibble on Turtle (see the file header). False for an empty
// slot or a null unit. This is what `Filter` selects on, and the ONE place the
// two encodings are chosen between.
bool IsSlotHarmful(const uint8_t *unit, int slot);

// The engine's TOOLTIP visibility gate for a slot: occupied, visible nibble,
// and the spell passes `FUN_GAMETOOLTIP_AURA_VISIBLE` — exactly what
// `SetUnitBuff` / `SetUnitDebuff` count while turning an index into a slot.
// Differs from `IsSlotPopulated` (the UnitBuff gate), so use THIS to compute
// the index the engine's tooltip methods expect for a given slot.
bool IsSlotTooltipVisible(const uint8_t *unit, int slot);

// The i-th slot (0..UNIT_AURA_TOTAL-1) in the order a `filter` walk visits the
// descriptor: the polarity's home range first (helpful 0..31, harmful 32..47),
// then the other range, where spilled debuffs live. Every enumeration that
// hands out indices or slot lists uses this so they agree on order.
int SlotInFilterOrder(Filter filter, int i);

// `SlotMatches` plus the polarity test: populated, of `filter`'s polarity, and
// passing `match`.
bool SlotMatchesFilter(const uint8_t *unit, int slot, Filter filter,
                       const Match &match);

// Finds the absolute slot of the `oneBasedIndex`-th populated aura of
// `filter`'s polarity that passes `match`. Returns -1 if no such aura. Walks
// all 48 slots in `SlotInFilterOrder`.
int FindNthSlot(const uint8_t *unit, int oneBasedIndex, Filter filter,
                Match match = {});

// Finds the absolute slot of a populated aura with the given spell
// ID, optionally restricted to one filter range. `filter` of
// nullptr searches both ranges (helpful first, then harmful).
// `playerOnly` restricts to player-cast auras. Returns -1 if not found.
int FindSlotBySpellID(const uint8_t *unit, uint32_t spellID,
                      const Filter *filter, Match match = {});

// Finds the absolute slot of a populated aura whose locale-resolved
// spell name exactly matches `spellName`. Case-sensitive. Otherwise
// same semantics as `FindSlotBySpellID`. Returns -1 if not found
// or if `spellName` is null/empty.
int FindSlotBySpellName(const uint8_t *unit, const char *spellName,
                        const Filter *filter, Match match = {});

// True iff the aura at `slot` was cast by the local player, per the
// `Aura::Source` cache. False on a cache miss (caster unknown) — so a
// PLAYER-filtered query excludes auras whose cast we didn't observe.
bool IsPlayerCast(const uint8_t *unit, int slot);

// Applies a `CasterMode` to a per-aura "was cast by the local player" answer.
// `Any` → always true; `PlayerOnly` → the answer; `NotPlayer` → its negation.
bool CasterMatches(CasterMode caster, bool isPlayerCast);

// True iff the spell's dispel type is one a dispel/purge/steal can remove —
// Magic, Curse, Disease, or Poison (`Spell.dbc` Dispel ∈ 1..4). None,
// Stealth, Invisibility, Enrage, etc. are not dispellable. Verified against
// the server's DISPEL_ALL_MASK.
bool IsDispellable(uint32_t spellID);

// Applies a full `Match` (caster + dispel predicates) to one aura, given the
// per-aura "was cast by the local player" answer and its spell ID.
bool MatchesAura(const Match &match, bool isPlayerCast, uint32_t spellID);

// True iff descriptor `slot` on `unit` is populated (per `IsSlotPopulated`) AND
// passes `match`. The caster answer (`IsPlayerCast` — a scan of the
// `Aura::Source` cache, ~1000 entries in a raid) is computed ONLY when `match`
// restricts on caster, so the common filter-less walk never pays for it.
bool SlotMatches(const uint8_t *unit, int slot, const Match &match);

// Display stack count for the given slot (engine stores stacks-1,
// we add 1). Returns 0 if the unit is null.
int ReadStacks(const uint8_t *unit, int slot);

// Returns true if the engine considers `spellRecord` a user-visible
// aura. Thin wrapper around `FUN_SPELL_IS_VISIBLE_AURA`. The record
// pointer must come from `Spell.dbc[spellID]`.
bool IsVisible(const uint8_t *spellRecord);

// Looks up `SpellDispelType.dbc[dispelTypeID]` and returns the
// locale-applied name (e.g. "Magic", "Curse", "Disease", "Poison"),
// or nullptr if the ID is 0, out of range, or has no name.
const char *DispelName(uint32_t dispelTypeID);

// Builds a modern-style `AuraData` table on top of the Lua stack
// from the aura currently in `unit`'s descriptor at `slot`. Caller
// is responsible for having validated that the slot's spell ID is
// non-zero and visible (or just wants the data anyway).
//
// Populates these fields with real data:
//   name, icon, applications, spellId, dispelName,
//   isHelpful, isHarmful, timeMod, duration
//
// And these from the `Aura::Source` SMSG_SPELL_GO cache when available
// (else their inapplicable default):
//   expirationTime (player: engine buff table; others: cache; 0 on miss),
//   sourceUnit     (caster token from the cache; nil on miss),
//   sourceGUID     (caster "0x..." GUID string; nil on miss — a ClassicAPI
//                   extension, not in retail AuraData)
//
// And these with vanilla-truthful defaults (matches modern's
// "field present but inapplicable" semantics):
//   charges=0, maxCharges=0, auraInstanceID=nil, points=nil,
//   isStealable=false, isBossAura=false, isFromPlayerOrPlayerPet=false,
//   isNameplateOnly=false, nameplateShowAll=false,
//   nameplateShowPersonal=false, canApplyAura=false,
//   shouldConsolidate=false, isRaid=false
//
// Net stack effect: +1 for `Emit::Table` (the table), +15 for `Emit::Positional`
// (the Classic-Era `UnitAura` tuple — see `Emit`).
void Push(void *L, const uint8_t *unit, int slot, Emit emit = Emit::Table);

// Fallback for the index path when the descriptor has dropped an aura's slot
// (rogue stealth, party range fluctuation) but it's still active server-side.
// `oneBasedIndex` is the same index the caller passed to `FindNthSlot`; this
// resolves it against the `Aura::Source` cache *after* the descriptor matches
// (so cache entries append after descriptor ones, preserving existing index
// order). On a hit it pushes the AuraData table and returns true; on a miss it
// pushes nothing and returns false (caller pushes nil). Only entries whose
// helpful/harmful classification matches `filter` and that aren't already in a
// populated descriptor slot are considered. `emit` selects the table vs the
// positional tuple leaf (see `Emit`); the pushed count follows `Push`.
bool PushNthCacheFallback(void *L, const uint8_t *unit, int oneBasedIndex,
                          Filter filter, Match match = {},
                          Emit emit = Emit::Table);

// Appends every eligible `Aura::Source` cache fallback for `unit` matching
// `filter` into the array table at `outerIdx`, continuing from `nextKey`
// (updated in place). The bulk-enumeration analog of `PushNthCacheFallback`,
// for `GetUnitAuras`. Skips entries already present in a populated descriptor
// slot so it never double-lists.
void AppendCacheFallbacks(void *L, const uint8_t *unit, Filter filter,
                          Match match, int outerIdx, int &nextKey);

// ── Out-of-range groupmate path ────────────────────────────────────────────
//
// When a party/raid member has no live CGUnit at all (different map, far out
// of range) the token resolves to a GUID but no object, so there is no
// descriptor to read. The server still transmits that member's current aura
// spell IDs via SMSG_PARTY_MEMBER_STATS, which the client keeps in the
// group-member stats structs (`Group::MemberStats::AuraArray`). These functions
// enumerate that array — exactly what the built-in `UnitBuff`/`UnitDebuff` do
// out of range — and build the same AuraData table.
//
// Spell IDs only come off the wire: `applications` is 1 (stacks aren't sent)
// and IDs are u16-truncated (custom IDs > 65535 are wrong). Any caster / real
// expirationTime we observed for the member via SMSG_SPELL_GO is still merged
// in from the `Aura::Source` cache. `guid` of 0 (not a rostered member) yields
// no results. These are only meaningful when the unit has NO descriptor —
// callers use them in the `unit == nullptr` branch, so there is no descriptor
// to dedup against.

// Pushes AuraData for the `oneBasedIndex`-th group-array aura on `guid`
// matching `filter`. On a hit pushes the aura and returns true; otherwise
// pushes nothing and returns false (caller pushes nil). `emit` selects the
// table vs the positional tuple leaf (see `Emit`).
bool PushNthGroupAura(void *L, uint64_t guid, int oneBasedIndex, Filter filter,
                      Match match = {}, Emit emit = Emit::Table);

// Pushes AuraData for the group-array aura with `spellID` on `guid`, optionally
// restricted to one filter range (nullptr = both, helpful first). Returns true
// on a hit (table pushed), false otherwise (nothing pushed).
bool PushGroupAuraBySpellID(void *L, uint64_t guid, uint32_t spellID,
                            const Filter *filter, Match match = {});

// As `PushGroupAuraBySpellID` but matched by locale-resolved spell name
// (case-sensitive exact match), mirroring `FindSlotBySpellName`.
bool PushGroupAuraBySpellName(void *L, uint64_t guid, const char *spellName,
                              const Filter *filter, Match match = {});

// Appends every group-array aura on `guid` matching `filter` into the array
// table at `outerIdx`, continuing from `nextKey` (updated in place). The
// bulk-enumeration analog of `PushNthGroupAura`, for `GetUnitAuras`.
void AppendGroupAuras(void *L, uint64_t guid, Filter filter, Match match,
                      int outerIdx, int &nextKey);

// ── Opaque aura slots (GetAuraSlots / GetAuraDataBySlot) ────────────────────
//
// The by-index getters re-walk the descriptor from slot 0 on every call, so a
// full scan through them is quadratic in the aura count. `CollectSlots`
// enumerates once into opaque slot ids and `PushBySlot` fetches one aura by id
// with no walk — the modern `C_UnitAuras.GetAuraSlots` / `GetAuraDataBySlot`
// batching contract. An id is valid for the frame it was enumerated in.
//
// Encoding (kind = id / OPAQUE_STRIDE, k = id % OPAQUE_STRIDE):
//   kind 0  live descriptor slot k (0..UNIT_AURA_TOTAL-1)
//   kind 1  k-th eligible helpful cache-fallback entry   (OPAQUE_CACHE_HELPFUL)
//   kind 2  k-th eligible harmful cache-fallback entry   (OPAQUE_CACHE_HARMFUL)
//   kind 3  out-of-range group-array slot k (0..47)      (OPAQUE_GROUP)
//   kind 4  k-th helpful group cache-fallback entry      (OPAQUE_GROUP_CACHE_HELPFUL)
//   kind 5  k-th harmful group cache-fallback entry      (OPAQUE_GROUP_CACHE_HARMFUL)
// Fallback ordinals count the UNFILTERED eligible sequence (visible, not
// already in the live array), so `PushBySlot` needs no filter; `CollectSlots`
// applies the caller's `match` only when choosing which ids to emit.
constexpr int OPAQUE_STRIDE = 0x100;
constexpr int OPAQUE_CACHE_HELPFUL = 1 * OPAQUE_STRIDE;
constexpr int OPAQUE_CACHE_HARMFUL = 2 * OPAQUE_STRIDE;
constexpr int OPAQUE_GROUP = 3 * OPAQUE_STRIDE;
constexpr int OPAQUE_GROUP_CACHE_HELPFUL = 4 * OPAQUE_STRIDE;
constexpr int OPAQUE_GROUP_CACHE_HARMFUL = 5 * OPAQUE_STRIDE;

// Upper bound on ids one `CollectSlots` call can yield: the live array (or the
// group array) plus its cache fallback, each at most UNIT_AURA_TOTAL entries.
constexpr int OPAQUE_SLOTS_MAX = 2 * Offsets::UNIT_AURA_TOTAL;

// Writes the opaque ids of every aura on the unit matching `filter` + `match`
// into `out` (at most `cap`), in the order the by-index getters visit them:
// live slots (or, with no live CGUnit, the group array) first, then the cache
// fallback. `unit` null selects the out-of-range group path for `guid`. Returns
// the count written.
int CollectSlots(const uint8_t *unit, uint64_t guid, Filter filter, Match match,
                 int *out, int cap);

// Pushes the aura an opaque id names — table or positional per `emit` — and
// returns true; pushes nothing and returns false for an id that names nothing
// (stale, malformed, or the wrong path for the unit's current state).
bool PushBySlot(void *L, const uint8_t *unit, uint64_t guid, int slot, Emit emit);

} // namespace Aura::Data
