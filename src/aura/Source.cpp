// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// See `Source.h` for the contract. This is the backend: a co-hook on
// `FUN_SPELL_GO` that mirrors nampower's `SpellGoHook` parse to capture the
// caster + duration of every aura-applying cast, and a small fixed-size
// cache `Aura::Data::Push` reads from.

#include "Source.h"

#include "ComboDuration.h"
#include "Data.h"
#include "Game.h"
#include "Offsets.h"
#include "net/PacketDispatch.h"
#include "net/PacketReader.h"
#include "object/Resolve.h"
#include "player/StatSignal.h"
#include "spell/CastEvents.h"
#include "spell/Lookup.h"
#include "tick/WorldTick.h"
#include "time/Clock.h"
#include "totem/Tracker.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstring>

namespace Aura::Source {

namespace {

using Net::CDataStore;

// A cast applies an aura iff any of its three effects names an aura. The
// EffectApplyAuraName[3] int32 array sits at `+0x16C` (already used by the
// shapeshift / mechanic-immunity code). Gating on this keeps pure-damage
// and utility casts out of the cache.
bool SpellAppliesAura(const uint8_t *spellRecord) {
    if (spellRecord == nullptr)
        return false;
    const auto *auraNames = reinterpret_cast<const int32_t *>(
        spellRecord + Offsets::OFF_SPELL_RECORD_EFFECT_APPLY_AURA_NAME);
    for (int i = 0; i < Offsets::SPELL_RECORD_EFFECT_COUNT; ++i) {
        if (auraNames[i] != 0)
            return true;
    }
    return false;
}

using Time::Clock::NowMs;

// Server-authoritative duration (ms). When the local player is the caster
// we let the engine fold in the player's duration modifiers (skipMod = 0);
// for any other caster we take the unmodified base (skipMod = 1) since we
// only have the local player's mod tables. `unit = 0` selects the player
// mod context per FUN_GET_SPELL_DURATION's signature.
uint32_t SpellDurationMs(const uint8_t *spellRecord, bool casterIsPlayer) {
    using GetDuration_t = int(__fastcall *)(const uint8_t *spellRecord,
                                            int unit, char skipMod);
    const int ms = reinterpret_cast<GetDuration_t>(
        static_cast<uintptr_t>(Offsets::FUN_GET_SPELL_DURATION))(
        spellRecord, 0, casterIsPlayer ? 0 : 1);
    return ms > 0 ? static_cast<uint32_t>(ms) : 0;
}

// ---- Recent local-player aura casts --------------------------------------

// The aura-application hooks (OnAuraAdded / OnAuraStacksChanged) get no caster,
// so they compute the UNMODIFIED base duration (`skipMod = 1`). That's correct
// for other units' auras, but wrong for one the LOCAL PLAYER just cast: it
// should carry the player-talented duration (Improved Shadow Word: Pain → 24s
// vs the 18s base). SpellGo normally owns that entry with the talented value,
// but when it can't attribute the cast to this target — an empty SMSG_SPELL_GO
// hit list files it under the caster instead, or a slot reshuffle evicts and
// refills the entry after SpellGo ran — the base value is what's left, so the
// duration flickers 24 → 18. SpellGo (which knows caster == player) records the
// spellId here so the application hooks can recover the player-modified
// duration for it.
constexpr int kRecentCastCount = 16;
constexpr uint32_t kRecentCastTtlMs = 1500;
struct RecentCast {
    uint32_t spellId;
    uint32_t tMs; // 0 = empty
};
RecentCast g_recentCasts[kRecentCastCount];
int g_recentCastCursor = 0;

void RememberPlayerCast(uint32_t spellId) {
    const uint32_t now = NowMs();
    for (auto &r : g_recentCasts)
        if (r.spellId == spellId) {
            r.tMs = now;
            return;
        }
    g_recentCasts[g_recentCastCursor] = {spellId, now};
    g_recentCastCursor = (g_recentCastCursor + 1) % kRecentCastCount;
}

bool WasRecentPlayerCast(uint32_t spellId) {
    if (spellId == 0)
        return false;
    const uint32_t now = NowMs();
    for (const auto &r : g_recentCasts)
        if (r.spellId == spellId && r.tMs != 0 && now - r.tMs <= kRecentCastTtlMs)
            return true;
    return false;
}

// ---- Cache ---------------------------------------------------------------

// Helpful/harmful classification, recorded from the aura's descriptor slot
// when it's applied (the only place the buff/debuff split is known). SpellGo
// gives no slot, so casts we only saw there stay Unknown until/unless an
// application hook fires for the same aura.
enum Kind : int8_t { KIND_UNKNOWN = -1, KIND_HELPFUL = 0, KIND_HARMFUL = 1 };

// An entry is one AURA INSTANCE, identified the way the server identifies one:
// `(target, spell, caster)`. Two same-class raiders' Corruption on one boss are
// two auras occupying two descriptor slots, so a `(target, spell)` identity
// collapses them into one entry — the later cast overwrites the earlier
// caster's timing, and `OnAuraRemoved` for either copy evicts both.
//
// The descriptor only stores spell IDs, so recovering WHICH instance a slot
// holds needs the slot bound to its entry. SpellGo knows the caster but no
// slot; `OnAuraAdded` knows the slot but no caster. They arrive in that order
// (SpellGo, then the SMSG_UPDATE_OBJECT that seats the aura), so the
// application hook binds the cast capture it belongs to — the newest entry for
// this `(target, spell)` still awaiting a slot.
struct Entry {
    uint64_t targetGuid;
    uint64_t casterGuid;
    uint32_t spellId;
    uint32_t expirationMs; // 0 = infinite / unknown duration
    uint32_t durationMs;   // applied duration (incl. caster mods); 0 = none
    uint32_t stampMs;      // last write time; EvictAbsent grace (see below)
    int16_t slot;          // absolute descriptor slot, SLOT_UNBOUND until bound
    int8_t kind;           // Kind; descriptor-slot-derived, KIND_UNKNOWN if only seen via SpellGo
    bool used;
};

// Sized for the realistic worst case: a fully raid-buffed 40-man plus its
// debuff load. We cache one entry per (target, spellId, caster) for EVERY
// aura-applying SMSG_SPELL_GO we observe — not just auras on the player, but
// every buff cast on every unit in view — so the live working set in a raid
// is ~40 members × ~30 persistent buffs ≈ 1200, plus debuffs. At the old 256
// the table was permanently full and the overflow path evicted live buffs,
// dropping their source (the "lost sourceGUID on a buff" report). 2048 gives
// a fully-buffed 40-man comfortable headroom; the tick sweep still reclaims
// expired/orphaned entries so it rarely approaches full outside a raid.
// (~40 bytes/entry → ~80 KB static.) Per-caster identity adds one entry per
// extra caster of the same spell on one target, which is bounded by the 16
// debuff slots that can hold them.
constexpr int kCacheSize = 2048;
Entry g_cache[kCacheSize];

// One past the highest slot ever claimed since the last flush — the active
// prefix `[0, g_usedHigh)`. Every used entry lives below it (Claim is the sole
// allocator and extends it; FlushAll resets it), so all lookups scan only this
// prefix instead of the full 2048. Outside a raid the working set is a handful
// of auras, so this is the difference between a ~30-entry scan and a 2048-entry
// one on the per-aura query path. Never lowered on eviction — an over-estimate
// only costs a few extra skipped (!used) slots, never a missed live entry.
int g_usedHigh = 0;

// SMSG_SPELL_GO arrives before the SMSG_UPDATE_OBJECT that adds the aura to
// the target's descriptor, so for a brief window a just-captured entry names
// an aura the descriptor doesn't list yet. `EvictAbsent` (run from a query's
// ReconcileCache) must not drop such a fresh entry — doing so discards the
// caster we just captured, which OnAuraAdded then recreates caster-less
// (verified: druid buff → sourceGUID lost). Entries younger than this window
// are exempt from EvictAbsent; a genuinely-removed aura is far older and still
// gets dropped (and OnAuraRemoved evicts real removals directly regardless).
constexpr uint32_t kEvictGraceMs = 2000;

// True if `guid`'s live unit descriptor still lists `spellId` in any aura
// slot. The descriptor is the presence authority while the unit is in view,
// so an entry backing a still-listed aura must never be timer-reclaimed: the
// caster (sourceGUID / sourceUnit) is immutable for the aura's lifetime, but
// our `expirationMs` is only a base-duration estimate for non-player casters
// (we lack their mod tables), which underestimates talent-extended auras and
// elapses while the aura is still up. Resolves the GUID through the object
// manager — non-throwing, returns null when the unit isn't loaded / is out of
// range — so an out-of-range or stealthed unit reports false and its
// fallback-only entry stays timer-reclaimable exactly as before.
bool DescriptorListsAura(uint64_t guid, uint32_t spellId) {
    if (guid == 0 || spellId == 0)
        return false;
    constexpr int kUnitOrPlayerMask =
        (1 << Offsets::OBJECT_TYPE_UNIT) | (1 << Offsets::OBJECT_TYPE_PLAYER);
    const auto *unit = static_cast<const uint8_t *>(
        Object::ByGuid(kUnitOrPlayerMask, guid, nullptr, 0));
    if (unit == nullptr)
        return false;
    const auto *desc = *reinterpret_cast<const uint8_t *const *>(
        unit + Offsets::OFF_CGUNIT_OBJECT_FIELDS);
    if (desc == nullptr)
        return false;
    for (int slot = 0; slot < Offsets::UNIT_AURA_TOTAL; ++slot) {
        if (*reinterpret_cast<const uint32_t *>(
                desc + Offsets::OFF_UNIT_FIELD_AURA + slot * 4) == spellId)
            return true;
    }
    return false;
}

// ---- Entry lookup --------------------------------------------------------

// The exact aura instance `caster` has on `targetGuid` — the server's own
// identity for one aura, and what a repeat cast refreshes.
Entry *FindByCaster(uint64_t targetGuid, uint32_t spellId, uint64_t casterGuid) {
    for (int i = 0; i < g_usedHigh; ++i) {
        Entry &e = g_cache[i];
        if (e.used && e.targetGuid == targetGuid && e.spellId == spellId &&
            e.casterGuid == casterGuid)
            return &e;
    }
    return nullptr;
}

// The instance seated in descriptor slot `slot`.
Entry *FindBySlot(uint64_t targetGuid, uint32_t spellId, int slot) {
    if (slot < 0)
        return nullptr;
    for (int i = 0; i < g_usedHigh; ++i) {
        Entry &e = g_cache[i];
        if (e.used && e.targetGuid == targetGuid && e.spellId == spellId &&
            e.slot == slot)
            return &e;
    }
    return nullptr;
}

// The newest cast capture for this aura still awaiting a descriptor slot —
// what an `OnAuraAdded` for `(target, spell)` is seating. Newest wins because
// SpellGo precedes the application by a packet, so the freshest unbound entry
// is the cast that just landed; an older one is a capture that never seated
// (fully resisted, immune) and is left to expire on its own. "Newest" is the
// smallest `Time::Clock::Elapsed(stamp, now)` — a wrap-safe signed difference;
// a raw `stampMs >` compare inverts across the 2^32 ms tick wrap.
Entry *FindFreshestUnbound(uint64_t targetGuid, uint32_t spellId, uint32_t now) {
    Entry *best = nullptr;
    uint32_t bestElapsed = 0;
    for (int i = 0; i < g_usedHigh; ++i) {
        Entry &e = g_cache[i];
        if (!e.used || e.targetGuid != targetGuid || e.spellId != spellId ||
            e.slot != SLOT_UNBOUND)
            continue;
        const uint32_t elapsed = Time::Clock::Elapsed(e.stampMs, now);
        if (best == nullptr || elapsed < bestElapsed) {
            best = &e;
            bestElapsed = elapsed;
        }
    }
    return best;
}

// The one entry for `(target, spell)`, or null when there are none or several.
// Several means two casters and no way to tell their instances apart from a
// spell ID alone, which is exactly when guessing is what produces a wrong
// caster and a wrong timer.
Entry *FindSole(uint64_t targetGuid, uint32_t spellId) {
    Entry *found = nullptr;
    for (int i = 0; i < g_usedHigh; ++i) {
        Entry &e = g_cache[i];
        if (!e.used || e.targetGuid != targetGuid || e.spellId != spellId)
            continue;
        if (found != nullptr)
            return nullptr;
        found = &e;
    }
    return found;
}

// The instance a slot-bearing query means: the entry seated in `slot`, else the
// unit's sole entry for the spell — every single-caster case and the slotless
// (out-of-range group array, cache fallback) paths. Two casters' entries with
// no slot binding resolve to null (see FindSole), never to a coin flip. This is
// the read-side identity rule; both `Get` and `Evict` resolve through it so they
// can't drift into naming different instances for the same query.
Entry *FindInstance(uint64_t targetGuid, uint32_t spellId, int slot) {
    Entry *e = FindBySlot(targetGuid, spellId, slot);
    return e != nullptr ? e : FindSole(targetGuid, spellId);
}

// Marks the slot `e` occupies as within the active prefix, so later lookups
// reach it. Every new entry passes through here (Claim is the only allocator),
// which is what keeps the `[0, g_usedHigh)` invariant true for all writers.
Entry *Register(Entry *e) {
    const int idx = static_cast<int>(e - g_cache);
    if (idx >= g_usedHigh)
        g_usedHigh = idx + 1;
    return e;
}

// Takes a free slot, else an expired one whose aura the descriptor no longer
// lists (a still-present aura keeps its slot so its caster isn't lost — see
// DescriptorListsAura), else evicts. Scans the full array — as the allocator it
// must be able to hand out slots beyond the current active prefix (that is how
// the prefix grows), so it can't bound itself by g_usedHigh.
Entry *Claim(uint32_t now) {
    for (auto &e : g_cache) {
        if (!e.used ||
            (e.expirationMs != 0 && now >= e.expirationMs &&
             !DescriptorListsAura(e.targetGuid, e.spellId)))
            return Register(&e);
    }
    // Saturated. Honor the same invariant as the tick sweep: an entry whose
    // aura is still present on a resolvable unit is NEVER evicted — its caster
    // can't be recovered once dropped (the "buff still on the player lost its
    // source" case). Prefer the least-recently-updated ORPHAN (no live
    // descriptor backing: out of range / despawned / genuinely gone); only if
    // every single slot is a present, live aura — true saturation we can't
    // avoid — fall back to the global LRU victim. With kCacheSize sized to the
    // raid working set, reaching here is already unlikely and the saturated
    // fallback is practically unreachable.
    Entry *orphan = nullptr;
    Entry *lru = &g_cache[0];
    for (auto &e : g_cache) {
        if (!e.used) { // defensive: a freed slot always wins outright
            orphan = &e;
            break;
        }
        if (e.stampMs < lru->stampMs)
            lru = &e;
        if (DescriptorListsAura(e.targetGuid, e.spellId))
            continue; // still present on a live descriptor — protect it
        if (orphan == nullptr || e.stampMs < orphan->stampMs)
            orphan = &e;
    }
    return Register((orphan != nullptr) ? orphan : lru);
}

// ---- Writes --------------------------------------------------------------

// The SpellGo hook: authoritative caster + caster-modified (talented) timing.
// Identity is `(target, spell, caster)`, so a second caster of the same spell
// opens its own entry instead of overwriting the first's, and a recast by the
// same caster refreshes theirs — keeping the descriptor slot already bound to
// it.
void StoreFromCast(uint64_t targetGuid, uint32_t spellId, uint64_t casterGuid,
                   uint32_t expirationMs, uint32_t durationMs) {
    if (targetGuid == 0 || spellId == 0)
        return;
    const uint32_t now = NowMs();
    Entry *e = FindByCaster(targetGuid, spellId, casterGuid);
    // A lone unattributed entry for this aura is the same instance seen without
    // a caster (seated by an application hook while we were out of view, or a
    // group-array guess), so claim it rather than opening a second entry for
    // one aura — the same adoption `ApplyDurationModifiers` does. Only when
    // it's the unit's only entry for the spell: with several, "the one this
    // cast refreshes" is a guess, and guessing wrong writes this caster's timer
    // onto another's aura.
    if (e == nullptr) {
        Entry *sole = FindSole(targetGuid, spellId);
        if (sole != nullptr && sole->casterGuid == 0)
            e = sole;
    }
    if (e == nullptr) {
        e = Claim(now);
        *e = {targetGuid, casterGuid, spellId, expirationMs, durationMs,
              now,        SLOT_UNBOUND, KIND_UNKNOWN, true};
        return;
    }
    e->stampMs = now; // refresh EvictAbsent grace on any touch
    e->casterGuid = casterGuid;
    e->expirationMs = expirationMs;
    e->durationMs = durationMs;
}

// The OnAuraAdded / OnAuraStacksChanged application hooks: a descriptor slot,
// a classification, and base timing — but no caster (except the local player's
// own recent cast, which `StampApplication` resolves). Seats the slot on the
// cast capture it belongs to, and must not clobber the caster + talented
// timing SpellGo already owns for that instance.
//
// `slot` is SLOT_UNBOUND for the out-of-range group-array path, which has no
// descriptor at all; there the only available identity is `(target, spell)`.
void StoreFromApplication(uint64_t targetGuid, uint32_t spellId,
                          uint64_t casterGuid, uint32_t expirationMs,
                          uint32_t durationMs, int slot, int8_t kind) {
    if (targetGuid == 0 || spellId == 0)
        return;
    const uint32_t now = NowMs();
    // Anything outside the descriptor's slot range can't index it, so it binds
    // nothing — `EvictAbsent` indexes the array by a bound entry's slot.
    if (slot < 0 || slot >= Offsets::UNIT_AURA_TOTAL)
        slot = SLOT_UNBOUND;

    Entry *e = FindBySlot(targetGuid, spellId, slot);
    if (e == nullptr)
        e = FindFreshestUnbound(targetGuid, spellId, now);
    if (e == nullptr && slot < 0)
        e = FindSole(targetGuid, spellId);
    if (e == nullptr) {
        e = Claim(now);
        *e = {targetGuid, casterGuid, spellId, expirationMs, durationMs, now,
              static_cast<int16_t>(slot), kind, true};
        return;
    }

    e->stampMs = now;
    if (slot >= 0)
        e->slot = static_cast<int16_t>(slot);
    // Learn the classification whenever a slot-derived kind arrives —
    // independent of caster/timing ownership, and never downgrade a known kind
    // back to unknown.
    if (kind != KIND_UNKNOWN)
        e->kind = kind;
    if (e->casterGuid != 0)
        return; // SpellGo owns this entry; keep its caster + talented timing
    if (casterGuid != 0)
        e->casterGuid = casterGuid;
    e->expirationMs = expirationMs;
    e->durationMs = durationMs;
}

// ---- Out-of-range group-member aura snapshots ---------------------------
//
// Transition state for `ObserveGroupAuras`: the last aura spell-ID set we saw
// for each out-of-range group member, so we can stamp a duration guess only
// when an aura genuinely appears (not for auras already present when we first
// started watching that member). See the header for the rationale.

struct GroupSnapshot {
    uint64_t guid;
    uint32_t touchMs;
    bool used;
    uint16_t ids[Offsets::UNIT_AURA_TOTAL];
};
constexpr int kGroupSnapshots = 44;              // MAX_RAID (40) + slack
constexpr uint32_t kGroupSnapshotTtlMs = 30000;  // forget members not polled for 30s
GroupSnapshot g_groupSnaps[kGroupSnapshots];

bool SnapshotHasId(const GroupSnapshot &s, uint16_t id) {
    for (uint16_t v : s.ids)
        if (v == id)
            return true;
    return false;
}

// Stamp a base-duration expiration guess for a group aura that just appeared.
// casterGuid stays 0 (unknown), so `Store`'s guard preserves any real SpellGo
// timing we already hold. Skips non-aura spells and auras with no finite base
// duration (nothing meaningful to guess — leave expiration unknown).
void StampGroupGuess(uint64_t guid, uint16_t spellId, int8_t kind, uint32_t now) {
    const uint8_t *rec = Spell::Lookup::RecordForID(static_cast<int>(spellId));
    if (!SpellAppliesAura(rec))
        return;
    const uint32_t base = SpellDurationMs(rec, /*casterIsPlayer*/ false);
    if (base == 0)
        return;
    StoreFromApplication(guid, spellId, /*casterGuid*/ 0, now + base, base,
                         SLOT_UNBOUND, kind);
}

// Evict the entry for an aura the engine reports gone, identified by the
// descriptor slot it vacated. Without this, the GetAuraDataByIndex fallback
// would keep surfacing a dropped aura until its computed expiry — e.g. a Rank
// 1 buff replaced by Rank 2 (engine drops Rank 1 from the descriptor) would
// show as a phantom second aura, or a dispelled buff would linger.
//
// The slot is what makes this safe when two casters hold the same spell on one
// target: only the instance that actually fell off goes. Falling back to a
// spell-ID match when nothing is bound to the slot keeps the pre-binding
// behaviour, but only while the match is unambiguous — dropping one of two
// casters' entries at random would take a live aura's caster with it.
void Evict(uint64_t targetGuid, uint32_t spellId, int slot) {
    if (targetGuid == 0 || spellId == 0)
        return;
    Entry *e = FindInstance(targetGuid, spellId, slot);
    if (e != nullptr)
        e->used = false;
}

// ---- Server-side duration modifiers (trigger-driven inference) -----------
//
// Some server mechanics change a DoT's remaining time on another unit with
// NO client-visible signal — verified in tortoise-wow: the change goes
// through SetAuraDuration / RefreshHolder, and UpdateAuraDuration only
// notifies the aura's target-if-a-player (self-scoped), never the observing
// caster; on a mob the packet isn't even built. We can't hear the *change*,
// but we DO see the TRIGGERING cast's SMSG_SPELL_GO, so we mirror the
// server's edit on the cached entry. Rules are registered two ways: the
// family/school-matched form from Lua (`RegisterAuraDurationModifierByTrigger`,
// e.g. Shadow Weaving), and the exact-spellID form from C++ (`AddDurationMod`),
// which src/turtle uses for the server's built-in mods. Turtle examples
// (registered in src/turtle/DurationMods.cpp):
//   Conflagrate  -> Immolate:    reduce 3s   (Immolate keeps ticking, -3s)
//   Molten Blast -> Flame Shock:  refresh    (RefreshHolder → reset to max)
// A trigger is matched either by exact spellID (from the server's script
// binding, stable across ranks) or by SpellFamilyName + school; the affected
// aura by SpellFamilyName + a family-flag overlap (+ optional icon) — rank-proof,
// exactly how the server's scripts find it. Conflagrate's *full*-consume path
// removes Immolate, which clears
// the descriptor slot → OnAuraRemoved already handles it; the reduce rule
// covers the keep-ticking case. Probabilistic refreshes (Carnage's roll) are
// deliberately NOT shipped as defaults — the client can't know the server's
// roll outcome, so inferring them would show wrong timers.

enum ModOp { MOD_REFRESH = 0, MOD_REDUCE = 1, MOD_SET = 2, MOD_REMOVE = 3 };

struct DurationMod {
    uint32_t triggerSpellId; // exact trigger; 0 = match by family+school below
    uint32_t triggerFamily;  // (triggerSpellId==0) trigger's SpellFamilyName
    int32_t triggerSchool;   // (triggerSpellId==0) school index; -1 = any school
    uint32_t affectedFamily; // SpellFamilyName of the affected aura
    uint64_t affectedMask;   // must overlap the affected aura's SpellFamilyFlags
    uint32_t affectedIcon;   // 0 = any; else affected aura's SpellIconID must equal
    int32_t op;
    int32_t valueMs; // REDUCE/SET amount; ignored by REFRESH/REMOVE
};
constexpr int kMaxMods = 128;
DurationMod g_mods[kMaxMods];
int g_modCount = 0;

// The affected-aura selector alone, shared with the Lua-facing by-family
// refresh so the two cannot disagree about what they match.
bool AffectedMatchesRaw(const uint8_t *rec, uint32_t family, uint64_t mask,
                        uint32_t icon) {
    if (!Spell::Lookup::IsFitToFamily(rec, family, mask))
        return false;
    if (icon != 0 &&
        *reinterpret_cast<const uint32_t *>(
            rec + Offsets::OFF_SPELL_RECORD_ICON_ID) != icon)
        return false;
    return true;
}

bool AffectedMatches(const uint8_t *rec, const DurationMod &m) {
    return AffectedMatchesRaw(rec, m.affectedFamily, m.affectedMask,
                              m.affectedIcon);
}

// A rule's trigger matches either by exact spellID, or (triggerSpellId == 0)
// by the cast spell's SpellFamilyName + school index — one rule then covers
// every rank / server-added spell of a class's school (e.g. any priest
// shadow-school cast, for Shadow Weaving). triggerSchool < 0 = any school.
bool TriggerMatches(const DurationMod &m, uint32_t triggerSpellId,
                    const uint8_t *triggerRec) {
    if (m.triggerSpellId != 0)
        return m.triggerSpellId == triggerSpellId;
    if (triggerRec == nullptr)
        return false;
    if (*reinterpret_cast<const uint32_t *>(
            triggerRec + Offsets::OFF_SPELL_RECORD_FAMILY_NAME) != m.triggerFamily)
        return false;
    if (m.triggerSchool >= 0 &&
        static_cast<int32_t>(*reinterpret_cast<const uint32_t *>(
            triggerRec + Offsets::OFF_SPELL_SCHOOL)) != m.triggerSchool)
        return false;
    return true;
}

// A cache duration edit has no engine packet behind it, so nothing re-fires
// UNIT_AURA and event-driven consumers (aura-bar addons that read the duration
// once and count down locally) never see the change. Re-broadcast UNIT_AURA for
// the affected unit so they re-read. Deferred to the world tick, not fired
// inline: the callers run inside the SMSG_SPELL_GO subscriber, and firing a
// unit event there would run addon Lua mid-packet (the re-entrancy class this
// codebase keeps getting bitten by). The tick flush is the context the engine's
// own UNIT_AURA fires are safe in. Repeats within a frame are coalesced.
constexpr int kPendingSignalMax = 32;
uint64_t g_pendingSignals[kPendingSignalMax];
int g_pendingSignalCount = 0;

void SignalAuraChanged(uint64_t guid) {
    if (guid == 0)
        return;
    for (int i = 0; i < g_pendingSignalCount; ++i)
        if (g_pendingSignals[i] == guid)
            return;
    if (g_pendingSignalCount < kPendingSignalMax)
        g_pendingSignals[g_pendingSignalCount++] = guid;
}

void FlushAuraSignals() {
    using Broadcast_t = void(__fastcall *)(uint64_t *guid, uint32_t eventCode);
    auto broadcast = reinterpret_cast<Broadcast_t>(
        static_cast<uintptr_t>(Offsets::FUN_UNIT_EVENT_BROADCAST));
    for (int i = 0; i < g_pendingSignalCount; ++i)
        broadcast(&g_pendingSignals[i], Offsets::UNIT_EVENT_UNIT_AURA);
    g_pendingSignalCount = 0;
}

void ApplyMod(Entry &e, const DurationMod &m, uint32_t now) {
    switch (m.op) {
    case MOD_REFRESH:
        if (e.durationMs > 0) {
            e.expirationMs = now + e.durationMs; // RefreshHolder → reset to max
            SignalAuraChanged(e.targetGuid);
        }
        break;
    case MOD_SET:
        e.durationMs = static_cast<uint32_t>(m.valueMs);
        e.expirationMs = now + static_cast<uint32_t>(m.valueMs);
        SignalAuraChanged(e.targetGuid);
        break;
    case MOD_REDUCE:
        if (e.expirationMs != 0) {
            if (e.expirationMs > now + static_cast<uint32_t>(m.valueMs)) {
                e.expirationMs -= static_cast<uint32_t>(m.valueMs);
                SignalAuraChanged(e.targetGuid);
            } else {
                e.used = false; // shaved to/past now → server removes it
            }
        }
        break;
    case MOD_REMOVE:
        e.used = false; // removal — OnAuraRemoved / descriptor path fires UNIT_AURA
        break;
    }
}

// On a trigger cast landing on its hit targets, mirror the server's duration
// edit on the caster's own matching cached aura. Called from SpellGo_h before
// the aura gate — triggers (Conflagrate, Molten Blast) apply no aura of their
// own, so they'd otherwise be dropped.
void ApplyDurationModifiers(uint32_t triggerSpellId, uint64_t caster,
                            const uint64_t *targets, int numTargets) {
    if (triggerSpellId == 0 || caster == 0 || numTargets <= 0)
        return;
    const uint8_t *triggerRec =
        Spell::Lookup::RecordForID(static_cast<int>(triggerSpellId));
    const uint32_t now = NowMs();
    for (int r = 0; r < g_modCount; ++r) {
        const DurationMod &m = g_mods[r];
        if (!TriggerMatches(m, triggerSpellId, triggerRec))
            continue;
        for (int t = 0; t < numTargets; ++t) {
            for (int i = 0; i < g_usedHigh; ++i) {
                Entry &e = g_cache[i];
                if (!e.used || e.targetGuid != targets[t])
                    continue;
                // The mechanic acts on the trigger-caster's own aura. Cast-applied
                // auras (Immolate, Flame Shock) carry caster == player from SpellGo;
                // but PROC-applied ones (Shadow Weaving) enter the cache via the
                // application hooks with casterGuid 0 (no SMSG_SPELL_GO ever named a
                // caster), so accept those too and attribute them to this trigger
                // caster (also gives them a sourceUnit).
                if (e.casterGuid != 0 && e.casterGuid != caster)
                    continue;
                if (!AffectedMatches(
                        Spell::Lookup::RecordForID(static_cast<int>(e.spellId)), m))
                    continue;
                if (e.casterGuid == 0)
                    e.casterGuid = caster;
                ApplyMod(e, m, now);
                break; // one matching aura per (rule, target), like the server
            }
        }
    }
}

bool RegisterDurationMod(uint32_t triggerSpellId, uint32_t triggerFamily,
                         int32_t triggerSchool, uint32_t affectedFamily,
                         uint64_t affectedMask, uint32_t affectedIcon, int op,
                         int32_t valueMs) {
    // Trigger must be identified one way or the other.
    if ((triggerSpellId == 0 && triggerFamily == 0) || affectedMask == 0 ||
        op < MOD_REFRESH || op > MOD_REMOVE)
        return false;
    for (int i = 0; i < g_modCount; ++i) { // replace an identical rule
        DurationMod &m = g_mods[i];
        if (m.triggerSpellId == triggerSpellId && m.triggerFamily == triggerFamily &&
            m.triggerSchool == triggerSchool && m.affectedFamily == affectedFamily &&
            m.affectedMask == affectedMask && m.affectedIcon == affectedIcon) {
            m.op = op;
            m.valueMs = valueMs;
            return true;
        }
    }
    if (g_modCount >= kMaxMods)
        return false;
    g_mods[g_modCount++] = {triggerSpellId, triggerFamily, triggerSchool,
                            affectedFamily, affectedMask, affectedIcon, op, valueMs};
    return true;
}

int OpFromString(const char *s) {
    if (s == nullptr)
        return -1;
    if (_stricmp(s, "refresh") == 0)
        return MOD_REFRESH;
    if (_stricmp(s, "reduce") == 0)
        return MOD_REDUCE;
    if (_stricmp(s, "set") == 0)
        return MOD_SET;
    if (_stricmp(s, "remove") == 0)
        return MOD_REMOVE;
    return -1;
}

// `C_UnitAuras.RegisterAuraDurationModifierByTrigger(triggerFamily,
//     triggerSchool, affectedFamily, affectedFamilyFlags, affectedIcon, op
//     [, valueSeconds])` -> bool. Like the above, but the trigger is matched
// by SpellFamilyName + school index instead of an exact spellID — one rule
// covers every rank / server-added spell of a class's school. triggerSchool
// < 0 = any school. E.g. Shadow Weaving: priest (6) shadow-school (5) casts
// refresh the target's Shadow Vulnerability.
int __fastcall Script_RegisterAuraDurationModifierByTrigger(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2) ||
        !Game::Lua::IsNumber(L, 3) || !Game::Lua::IsNumber(L, 4) ||
        !Game::Lua::IsNumber(L, 5) || !Game::Lua::IsString(L, 6)) {
        Game::Lua::Error(
            L, "Usage: C_UnitAuras.RegisterAuraDurationModifierByTrigger("
               "triggerFamily, triggerSchool, affectedFamily, affectedFamilyFlags, "
               "affectedIcon, op[, valueSeconds])");
        return 0;
    }
    const auto tfamily = static_cast<uint32_t>(Game::Lua::ToNumber(L, 1));
    const auto tschool = static_cast<int32_t>(Game::Lua::ToNumber(L, 2));
    const auto family = static_cast<uint32_t>(Game::Lua::ToNumber(L, 3));
    const auto mask = static_cast<uint64_t>(Game::Lua::ToNumber(L, 4));
    const auto icon = static_cast<uint32_t>(Game::Lua::ToNumber(L, 5));
    const int op = OpFromString(Game::Lua::ToString(L, 6));
    const int32_t valueMs =
        Game::Lua::IsNumber(L, 7)
            ? static_cast<int32_t>(Game::Lua::ToNumber(L, 7) * 1000.0)
            : 0;
    if (op < 0 || tfamily == 0) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    Game::Lua::PushBool(L, RegisterDurationMod(/*triggerSpellId*/ 0, tfamily, tschool,
                                               family, mask, icon, op, valueMs));
    return 1;
}

void RegisterDurationModLua() {
    Game::Lua::RegisterTableFunction("C_UnitAuras",
                                     "RegisterAuraDurationModifierByTrigger",
                                     &Script_RegisterAuraDurationModifierByTrigger);
}

const Game::ModuleAutoRegister _autoregDurationMod{&RegisterDurationModLua};

// Wipe the whole cache. Used on a map transition (see OnWorldTick).
void FlushAll() {
    for (int i = 0; i < g_usedHigh; ++i)
        g_cache[i].used = false;
    g_usedHigh = 0; // active prefix is empty again
    // Reset transition baselines too: post-transition group auras re-sync and
    // should be treated as first-sight (unknown age), not diffed as new.
    for (auto &s : g_groupSnaps)
        s.used = false;
}

// -1 = no map seen yet (never a valid Map.dbc id), so the first tick just
// records the current map without flushing.
int g_lastMapId = -1;

// Drop entries whose timed aura has elapsed so the table doesn't fill with
// stale combat debuffs. Infinite-duration entries (expirationMs == 0) stay
// until overwritten.
//
// Also flush the entire cache on a map-ID change. A battleground/instance/
// continent transition (including the teleport out of a BG via /afk) bulk-
// clears the player's aura descriptor WITHOUT firing per-aura OnAuraRemoved,
// so entries for auras that are now gone would linger and the
// GetAuraDataByIndex fallback would keep surfacing them — the "buffs/debuffs
// stuck after leaving a battleground" report. Every other unit from the old
// map has despawned too, and the local player's real auras re-sync into the
// descriptor / PLAYER_BUFF tables on entering the new world, so a full flush
// loses nothing legitimate. Crucially, the descriptor drops the fallback
// must survive — rogue stealth and party-range fluctuation — do NOT change
// the map ID, so they're unaffected.
void OnWorldTick() {
    const int mapId = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_CURRENT_MAP_ID));
    if (mapId != g_lastMapId) {
        if (g_lastMapId != -1)
            FlushAll();
        g_lastMapId = mapId;
    }

    const uint32_t now = NowMs();
    for (int i = 0; i < g_usedHigh; ++i) {
        Entry &e = g_cache[i];
        if (!e.used || e.expirationMs == 0 || !Time::Clock::Reached(now, e.expirationMs))
            continue;
        // A timer elapse doesn't prove the aura is gone — expirationMs is only
        // a base-duration estimate for non-player casters. Keep any entry whose
        // aura the owning unit's descriptor still lists (descriptor = presence
        // authority in view; caster is immutable for the aura's life). Only
        // genuinely orphaned entries (unit out of range / stealthed / despawned
        // → no descriptor backing) are timer-reclaimed; the fallback path
        // already skips expired entries, so this can't resurface a phantom.
        if (DescriptorListsAura(e.targetGuid, e.spellId))
            continue;
        e.used = false;
    }
    // Forget snapshots for members we haven't polled recently (left the group,
    // or no longer displayed) so a later re-appearance re-baselines cleanly.
    for (auto &s : g_groupSnaps) {
        if (s.used && now - s.touchMs > kGroupSnapshotTtlMs)
            s.used = false;
    }
    // Re-fire UNIT_AURA for units whose cached duration a mod/refresh changed
    // this frame (see SignalAuraChanged).
    FlushAuraSignals();
}

const Tick::WorldTick::AutoSubscribe _tickSub{&OnWorldTick};

// ---- SMSG_SPELL_GO co-hook ----------------------------------------------

// The hit-target list is all we need: each entry is a unit the cast landed
// on, which is exactly where an aura gets applied. We deliberately stop
// before the missed-target list / target mask — the post-mask "intended"
// target is redundant with the hit list for aura purposes, and skipping it
// keeps the parse short and robust.
constexpr int kMaxTargets = 16;

// The self-contained SPELL_GO processing (succeeded events, server-side
// duration edits, aura-source caching). None of it reads engine descriptor
// state, so it's order-independent of the engine's own SPELL_GO handler —
// which is why it can run from the dispatch funnel (before the leaf handler)
// rather than after a co-hook's original call. The aura *application* hooks
// fire off a separate SMSG_UPDATE_OBJECT, so RememberPlayerCast's handoff to
// them is unaffected by the move.
void HandleSpellGo(uint64_t caster, uint32_t spellId, const uint64_t *targets,
                   int numTargets) {
    if (caster == 0 || spellId == 0)
        return;

    // Feed the totem tracker + fire UNIT_SPELLCAST_SUCCEEDED BEFORE the aura
    // gate below — a totem summon (and any non-aura spell) applies no aura,
    // so it would otherwise be dropped. SPELL_GO is "the spell went off", so
    // this is the succeeded signal for instants too. Player casts only.
    if (caster == Unit::Identity::PlayerGuid()) {
        Totem::Tracker::OnPlayerSpellGo(spellId);
        Spell::CastEvents::OnPlayerSucceeded(static_cast<int>(spellId));
    } else {
        Spell::CastEvents::OnRemoteSucceeded(caster, static_cast<int>(spellId));
    }

    // Mirror server-side duration edits the client is never told about
    // (Conflagrate -3s Immolate, Molten Blast refresh Flame Shock, …). Runs
    // before the aura gate below: the trigger spell applies no aura itself.
    ApplyDurationModifiers(spellId, caster, targets, numTargets);

    const uint8_t *rec = Spell::Lookup::RecordForID(static_cast<int>(spellId));
    if (!SpellAppliesAura(rec))
        return;

    const bool casterIsPlayer = (caster == Unit::Identity::PlayerGuid());
    // Combo-point finishers (Rupture, Kidney Shot, …) have their real
    // duration computed at cast time from the combo points spent — the
    // base-duration helper can't know it. ComboDuration mirrors the
    // server's computation from the CP snapshot its send hook captured;
    // 0 means "not combo-scaled", fall through to the base path.
    uint32_t durationMs =
        casterIsPlayer ? ComboDuration::TryComboScaledMs(rec, spellId) : 0;
    if (durationMs == 0)
        durationMs = SpellDurationMs(rec, casterIsPlayer);
    const uint32_t expirationMs = durationMs > 0 ? NowMs() + durationMs : 0;

    // Let the application hooks recover the player-talented duration for this
    // spell if they end up owning the target's entry (empty hit list / refill
    // race) — see WasRecentPlayerCast.
    if (casterIsPlayer)
        RememberPlayerCast(spellId);

    if (numTargets == 0) {
        // No explicit hit list (self-cast with caster-implicit target).
        StoreFromCast(caster, spellId, caster, expirationMs, durationMs);
        return;
    }
    for (int i = 0; i < numTargets; ++i)
        StoreFromCast(targets[i], spellId, caster, expirationMs, durationMs);
}

// SMSG_SPELL_GO parse (funnel subscriber). At the leaf handler the engine has
// pre-decoded itemGuid/casterGuid/spellId; here we're at the raw body, so we
// decode them ourselves before the hit list:
//   itemGuid(packed), casterGuid(packed), spellId(u32), castFlags(i16),
//   numHit(u8), hitGuids(u64 × numHit).
void ParseSpellGo(CDataStore *packet) {
    Net::ReadPackedGuid(packet); // itemGuid (unused)
    const uint64_t caster = Net::ReadPackedGuid(packet);
    const uint32_t spellId = Net::Read<uint32_t>(packet);
    Net::Read<int16_t>(packet); // castFlags (unused)
    const uint8_t numHit = Net::Read<uint8_t>(packet);
    uint64_t targets[kMaxTargets];
    int numTargets = 0;
    for (int i = 0; i < numHit; ++i) {
        const uint64_t guid = Net::Read<uint64_t>(packet);
        if (numTargets < kMaxTargets)
            targets[numTargets++] = guid;
    }
    HandleSpellGo(caster, spellId, targets, numTargets);
}

void SpellGoSub(uint32_t opcode, CDataStore *packet) {
    if (opcode == Offsets::SMSG_SPELL_GO && packet != nullptr)
        ParseSpellGo(packet);
}

const Net::PacketDispatch::AutoSubscribe _spellGoSub{&SpellGoSub};

// ---- Aura-application co-hooks (timing for proc / triggered auras) -------

// Classify by the absolute aura slot: 0..BUFF_COUNT-1 = buff (helpful),
// BUFF_COUNT..TOTAL-1 = debuff (harmful).
int8_t KindForSlot(int slot) {
    return slot >= Offsets::UNIT_AURA_BUFF_COUNT ? KIND_HARMFUL : KIND_HELPFUL;
}

// Stamp expiration for an aura that just landed/refreshed in `slot` on `unit`.
// Used by both the add and stack-change hooks. No caster is available from
// these paths, so it stamps timing only — StoreFromApplication keeps whatever
// SpellGo already owns, so a directly-cast aura keeps its talented timing.
// Base (unmodified) duration is the best estimate without a caster. The slot
// is what binds this application to the cast capture behind it.
void StampApplication(void *unit, uint32_t spellId, int slot) {
    if (spellId == 0)
        return;
    const uint8_t *rec = Spell::Lookup::RecordForID(static_cast<int>(spellId));
    if (!SpellAppliesAura(rec))
        return;
    const uint64_t unitGuid = Unit::Identity::GuidForObject(unit);
    if (unitGuid == 0)
        return;
    // If the local player just cast this aura, use its player-talented duration
    // (and attribute the caster) — SpellGo couldn't always land the talented
    // value on this target's entry. Otherwise base (we lack other casters'
    // mods). See WasRecentPlayerCast.
    const bool byPlayer = WasRecentPlayerCast(spellId);
    const uint32_t durationMs = SpellDurationMs(rec, byPlayer);
    const uint64_t caster = byPlayer ? Unit::Identity::PlayerGuid() : 0;
    const uint32_t expirationMs = durationMs > 0 ? NowMs() + durationMs : 0;
    StoreFromApplication(unitGuid, spellId, caster, expirationMs, durationMs,
                         slot, KindForSlot(slot));
}

// Bump the player-stat-inputs signal when an aura change hits the LOCAL
// player — buffs/debuffs move GetSpellBonusHealing's flat and Spirit/Armor
// terms, so its lazy cache must invalidate. Guarded on the player object so
// other units' aura churn (combat) doesn't needlessly invalidate it.
void NotifyIfPlayer(void *unit) {
    if (static_cast<const uint8_t *>(unit) == Unit::Identity::PlayerObject())
        Player::StatSignal::Notify();
}

// OnAuraAdded — a new aura occupies a slot (gives the spellId directly).
using OnAuraAdded_t = void(__fastcall *)(void *unit, void *edx, uint32_t slot,
                                         uint32_t spellId);
OnAuraAdded_t g_origOnAuraAdded = nullptr;

void __fastcall OnAuraAdded_h(void *unit, void *edx, uint32_t slot,
                              uint32_t spellId) {
    g_origOnAuraAdded(unit, edx, slot, spellId);
    StampApplication(unit, spellId, static_cast<int>(slot));
    NotifyIfPlayer(unit);
}

const Game::HookAutoRegister _hookAuraAdded{
    Offsets::FUN_ON_AURA_ADDED, reinterpret_cast<void *>(&OnAuraAdded_h),
    reinterpret_cast<void **>(&g_origOnAuraAdded)};

// OnAuraStacksChanged — an existing aura's stack count changed (e.g. Shadow
// Weaving climbing). Only the slot is given, so read the spellID back from
// the unit's aura array. Re-stamps expiration so stacking refreshes count.
using OnAuraStacksChanged_t = void(__fastcall *)(void *unit, void *edx,
                                                 int slot, uint8_t stackCount);
OnAuraStacksChanged_t g_origOnAuraStacksChanged = nullptr;

void __fastcall OnAuraStacksChanged_h(void *unit, void *edx, int slot,
                                      uint8_t stackCount) {
    g_origOnAuraStacksChanged(unit, edx, slot, stackCount);
    StampApplication(
        unit,
        Aura::Data::ReadSpellID(static_cast<const uint8_t *>(unit), slot), slot);
    NotifyIfPlayer(unit);
}

const Game::HookAutoRegister _hookAuraStacks{
    Offsets::FUN_ON_AURA_STACKS_CHANGED,
    reinterpret_cast<void *>(&OnAuraStacksChanged_h),
    reinterpret_cast<void **>(&g_origOnAuraStacksChanged)};

// OnAuraRemoved — a descriptor aura slot went empty: the aura fell off, was
// dispelled, was cancelled by its owner, or was replaced by a higher rank
// (the diff dispatcher fires Removed(old) + Added(new)). Evict the cache
// entry so the descriptor-drop fallback stops surfacing it. Same ABI as
// OnAuraAdded (unit in ecx, slot + spellId on the stack); `slot` is unused
// here — we evict by spellId.
//
// Always evict, even for a death-persistent aura (flask, world buff) on a
// dead unit: the server keeps those through death WITHOUT changing any
// field, so death itself fires no OnAuraRemoved. Any removal we do receive
// for one is therefore genuine — e.g. the owner cancelled the flask while
// dead — and must clear the cache, or the fallback keeps surfacing a phantom
// whose SetUnitAura tooltip is empty.
using OnAuraRemoved_t = void(__fastcall *)(void *unit, void *edx, uint32_t slot,
                                           uint32_t spellId);
OnAuraRemoved_t g_origOnAuraRemoved = nullptr;

void __fastcall OnAuraRemoved_h(void *unit, void *edx, uint32_t slot,
                                uint32_t spellId) {
    g_origOnAuraRemoved(unit, edx, slot, spellId);
    Evict(Unit::Identity::GuidForObject(unit), spellId, static_cast<int>(slot));
    NotifyIfPlayer(unit);
}

const Game::HookAutoRegister _hookAuraRemoved{
    Offsets::FUN_ON_AURA_REMOVED, reinterpret_cast<void *>(&OnAuraRemoved_h),
    reinterpret_cast<void **>(&g_origOnAuraRemoved)};

} // namespace

bool Get(uint64_t unitGuid, uint32_t spellId, int slot, uint64_t *outCaster,
         uint32_t *outExpirationMs, uint32_t *outDurationMs) {
    if (unitGuid == 0 || spellId == 0)
        return false;
    // Slot first, then the sole entry (see FindInstance): several entries and no
    // slot binding is a genuine ambiguity, so reporting the miss leaves the
    // caller its unknown-caster defaults instead of one caster's timer on the
    // other's aura.
    const Entry *e = FindInstance(unitGuid, spellId, slot);
    if (e == nullptr)
        return false;
    *outCaster = e->casterGuid;
    *outExpirationMs = e->expirationMs;
    *outDurationMs = e->durationMs;
    return true;
}

bool AddDurationMod(uint32_t triggerSpellId, uint32_t affectedFamily,
                    uint64_t affectedMask, uint32_t affectedIcon, int op,
                    int32_t valueMs) {
    // Exact-spellID trigger (triggerFamily 0, triggerSchool -1).
    return RegisterDurationMod(triggerSpellId, /*triggerFamily*/ 0,
                               /*triggerSchool*/ -1, affectedFamily, affectedMask,
                               affectedIcon, op, valueMs);
}

uint32_t RefreshDurationByFamily(uint64_t unitGuid, uint32_t family,
                                 uint64_t mask, uint32_t icon,
                                 uint64_t casterGuid) {
    if (unitGuid == 0 || mask == 0 || casterGuid == 0)
        return 0;
    const uint32_t now = NowMs();
    for (int i = 0; i < g_usedHigh; ++i) {
        Entry &e = g_cache[i];
        if (!e.used || e.targetGuid != unitGuid)
            continue;
        // Caller's own aura only, scoped as ApplyDurationModifiers scopes a
        // rule; a caster-less entry is claimed rather than skipped, same as
        // there.
        if (e.casterGuid != 0 && e.casterGuid != casterGuid)
            continue;
        if (!AffectedMatchesRaw(
                Spell::Lookup::RecordForID(static_cast<int>(e.spellId)), family,
                mask, icon))
            continue;
        if (e.durationMs == 0)
            continue; // never invent a duration
        if (e.casterGuid == 0)
            e.casterGuid = casterGuid;
        // The applied duration, not a recomputed one (the server's
        // RefreshHolder): a Rip cast at 3 combo points returns to its own
        // duration, not a five-point one.
        e.expirationMs = now + e.durationMs;
        e.stampMs = now;
        SignalAuraChanged(e.targetGuid);
        return e.spellId;
    }
    return 0;
}

void EvictAbsent(uint64_t unitGuid, const uint32_t *slotSpellIds) {
    if (unitGuid == 0 || slotSpellIds == nullptr)
        return;
    bool anyPresent = false;
    for (int s = 0; s < Offsets::UNIT_AURA_TOTAL; ++s)
        if (slotSpellIds[s] != 0) {
            anyPresent = true;
            break;
        }
    if (!anyPresent)
        return; // out of range vs genuinely buffless — see the header
    const uint32_t now = NowMs();
    for (int i = 0; i < g_usedHigh; ++i) {
        Entry &e = g_cache[i];
        if (!e.used || e.targetGuid != unitGuid)
            continue;
        // Fresh SpellGo capture the descriptor hasn't synced yet — see
        // kEvictGraceMs. Don't evict it; that's the sourceGUID-loss race.
        if (now - e.stampMs < kEvictGraceMs)
            continue;
        bool present = false;
        if (e.slot != SLOT_UNBOUND) {
            // A bound entry is only backed by the slot it was seated in, so a
            // slot now holding a different spell (or nothing) leaves it stale
            // even when another caster's copy keeps the spell ID on the unit.
            present = slotSpellIds[e.slot] == e.spellId;
        } else {
            for (int s = 0; s < Offsets::UNIT_AURA_TOTAL; ++s)
                if (slotSpellIds[s] == e.spellId) {
                    present = true;
                    break;
                }
        }
        if (!present)
            e.used = false;
    }
}

int Enumerate(uint64_t unitGuid, bool harmful, CachedAura *out, int maxOut) {
    if (unitGuid == 0 || out == nullptr || maxOut <= 0)
        return 0;
    const int8_t want = harmful ? KIND_HARMFUL : KIND_HELPFUL;
    const uint32_t now = NowMs();
    int n = 0;
    for (int i = 0; i < g_usedHigh && n < maxOut; ++i) {
        const Entry &e = g_cache[i];
        if (!e.used || e.targetGuid != unitGuid || e.kind != want)
            continue;
        if (e.expirationMs != 0 && now >= e.expirationMs)
            continue; // expired (infinite-duration entries pass)
        out[n++] = {e.spellId, e.casterGuid, e.expirationMs, e.durationMs,
                    e.slot};
    }
    return n;
}

void ObserveGroupAuras(uint64_t guid, const uint16_t *auraArray) {
    if (guid == 0 || auraArray == nullptr)
        return;
    const int total = Offsets::UNIT_AURA_TOTAL;
    const int buffCount = Offsets::UNIT_AURA_BUFF_COUNT;
    const uint32_t now = NowMs();

    GroupSnapshot *snap = nullptr;
    for (auto &s : g_groupSnaps)
        if (s.used && s.guid == guid) {
            snap = &s;
            break;
        }

    if (snap == nullptr) {
        // First sight of this member — record a baseline without stamping;
        // their current auras have unknown age.
        GroupSnapshot *dst = nullptr;
        for (auto &s : g_groupSnaps)
            if (!s.used) {
                dst = &s;
                break;
            }
        if (dst == nullptr) { // full — evict the least-recently-polled
            dst = &g_groupSnaps[0];
            for (auto &s : g_groupSnaps)
                if (s.touchMs < dst->touchMs)
                    dst = &s;
        }
        dst->guid = guid;
        dst->used = true;
        dst->touchMs = now;
        for (int i = 0; i < total; ++i)
            dst->ids[i] = auraArray[i];
        return;
    }

    // Stamp a guess for every spell ID present now but absent from the prior
    // snapshot (a genuine appearance), then refresh the snapshot.
    for (int slot = 0; slot < total; ++slot) {
        const uint16_t id = auraArray[slot];
        if (id == 0 || SnapshotHasId(*snap, id))
            continue;
        StampGroupGuess(guid, id, slot < buffCount ? KIND_HELPFUL : KIND_HARMFUL,
                        now);
    }
    for (int i = 0; i < total; ++i)
        snap->ids[i] = auraArray[i];
    snap->touchMs = now;
}

} // namespace Aura::Source
