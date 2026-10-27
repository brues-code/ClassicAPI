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

#include <cstdint>

// Aura caster / duration cache, fed by a co-hook on `SMSG_SPELL_GO`
// (`FUN_SPELL_GO`). The vanilla unit aura array (`UNIT_FIELD_AURA`) stores
// only spell IDs — it has no record of *who* cast an aura, and no
// cast/expiration timing for any unit but the local player. The one place
// the client ever sees the caster + a server-authoritative duration is the
// `SMSG_SPELL_GO` packet at cast time. We parse it (the same packet
// nampower parses for its `AURA_CAST_ON_*` events) and remember, per
// `(targetGuid, spellId, casterGuid)`, who cast it and when it should expire.
//
// The caster belongs in that key because it belongs in the server's: two
// same-class raiders' Corruption on one boss are two auras in two descriptor
// slots, and a `(target, spell)` cache holds one entry for both — the second
// cast overwrites the first caster's timing, and either copy falling off
// evicts the survivor's entry too. The descriptor stores only spell IDs, so
// each entry additionally records the slot its aura was seated in
// (`OnAuraAdded`), which is what lets a per-slot query name the right instance.
//
// `Aura::Data::Push` consults this to fill `sourceUnit` (caster, resolved
// to a unit token) and `expirationTime` for non-player units. The cache is
// inherently best-effort: it only knows about casts observed since login,
// and an entry survives until it expires (combat-duration auras) or is
// evicted under pressure. Misses leave the modern-truthful defaults
// (`sourceUnit` nil, `expirationTime` 0) in place — same outcome as before
// this module existed.

namespace Aura::Source {

// `slot` value for a query with no descriptor slot to go on (out-of-range
// group array, cache fallback) and for an entry not yet seated in one.
constexpr int SLOT_UNBOUND = -1;

// Looks up the cached caster + timing for the aura `spellId` occupying
// absolute descriptor `slot` on the unit identified by `unitGuid`. Returns
// true and fills the out params on a hit. `*outExpirationMs` is an absolute
// `GetTickCount`-epoch timestamp (0 = unknown / infinite-duration aura);
// `*outDurationMs` is the applied duration including the caster's modifiers
// (talents etc.; 0 = none) — use it for the `duration` field so it stays
// consistent with `expirationTime`; `*outCaster` is the caster's 64-bit GUID
// (0 when the aura was seen applied but its cast never observed). Returns
// false on a miss or for zero inputs.
//
// Pass `SLOT_UNBOUND` when the caller has no slot. Resolution is by slot
// first, then by spell ID when the unit carries only one entry for it; two
// casters' entries with no slot binding resolve to a miss rather than to a
// coin flip between them.
bool Get(uint64_t unitGuid, uint32_t spellId, int slot, uint64_t *outCaster,
         uint32_t *outExpirationMs, uint32_t *outDurationMs);

// `Get`'s caster for every descriptor slot at once, in one pass over the
// cache: `outCaster[slot]` is the caster `Get(unitGuid, slotSpellIds[slot],
// slot, ...)` would report, or 0. `slotSpellIds` and `outCaster` hold
// `Offsets::UNIT_AURA_TOTAL` entries. For walks that need every slot's caster
// (the own-auras-first sort), where 48 separate `Get` scans would cost 48
// passes.
void CastersForSlots(uint64_t unitGuid, const uint32_t *slotSpellIds,
                     uint64_t *outCaster);

// Refreshes `casterGuid`'s aura on `unitGuid` matching the same selector the
// duration rules use — SpellFamilyName plus a family-flag overlap and/or a
// SpellIconID, at least one of the two — to a full duration from now. Returns
// the spellID refreshed, or 0 if nothing matched. For a mechanic whose
// duration edit reaches the client too late to be attributed to a cast packet,
// so no rule can express it.
uint32_t RefreshDurationByFamily(uint64_t unitGuid, uint32_t family,
                                 uint64_t mask, uint32_t icon,
                                 uint64_t casterGuid);

// Mirrors the server's melee-swing judgement refresh (tortoise-wow
// Unit::DealMeleeDamage): a white swing from `attackerGuid` that dealt any
// damage refreshes every aura it cast on `unitGuid` whose spell is
// SPELLFAMILY_PALADIN with SPELL_ATTR_EX3_ALWAYS_HIT (the judgement-debuff
// marker — all Judgement of Light/Wisdom/Justice/Crusader ranks) back to its
// own applied duration. Caster-strict: entries with an unknown caster are
// left alone (a swing gives no proof of ownership). The caster comes from
// the Judgement-cast attribution inside the SpellGo path — the debuff itself
// is cast server-side with no client-visible SPELL_GO, so the Judgement cast
// (20271) is what names the owner: it refresh-with-adoption's existing
// entries (re-judge = in-place server refresh, no aura event) and arms the
// fresh application's attribution. Returns the number refreshed. Fed by
// Aura::JudgementRefresh's SMSG_ATTACKERSTATEUPDATE subscriber.
int RefreshJudgements(uint64_t unitGuid, uint64_t attackerGuid);

// Re-anchors the expiration of every cached aura the local player cast with
// `spellId` to `now + remainingMs`. Mirrors the server's channel pushback
// (Spell::DelayedChannel → Unit::DelaySpellAuraHolder): a hit shortens the
// channel AND every hit target's holder of the channel spell by the same
// amount, then tells the caster the new remaining time via MSG_CHANNEL_UPDATE
// — the packet Spell::Cast feeds this from. Without it a pushed-back channel's
// target-side aura (Dark Harvest, the drains) keeps its full-length SpellGo
// expiration and reads late by the accumulated pushback. `durationMs` (the
// applied total, the bar's full width) is deliberately untouched. Self-target
// holders are also corrected natively by the server (it re-sends
// SMSG_UPDATE_AURA_DURATION), so restamping those entries just keeps the two
// sources agreeing. Caster-only packet ⇒ only the player's channels are
// correctable; remote pushback stays invisible (see spell/Cast.cpp).
void RestampPlayerChannel(uint32_t spellId, uint32_t remainingMs);

// Op codes for AddDurationMod (mirror the Lua op strings).
enum DurationModOp {
    DURATION_MOD_REFRESH = 0,
    DURATION_MOD_REDUCE = 1,
    DURATION_MOD_SET = 2,
    DURATION_MOD_REMOVE = 3,
};

// Register a server duration-modifier rule from C++. The trigger is matched by
// exact `triggerSpellId`; the affected aura by SpellFamilyName plus an
// `affectedMask` family-flag overlap and/or an `affectedIcon`, at least one of
// the two (0 drops that half of the test; a custom spell with no family flags
// is named by its icon alone). `valueMs` is the reduce/set
// amount in milliseconds (ignored by refresh/remove). Used by src/turtle
// modules for the server's built-in mods (the family/school-matched variant is
// still Lua-registerable via C_UnitAuras.RegisterAuraDurationModifierByTrigger).
// Returns false on bad input or a full table.
bool AddDurationMod(uint32_t triggerSpellId, uint32_t affectedFamily,
                    uint64_t affectedMask, uint32_t affectedIcon, int op,
                    int32_t valueMs);

// Register a server "triggered application" rule from C++: `triggerSpellId`'s
// SMSG_SPELL_GO — local player casts only — arms its hit targets for a short
// window, and a caster-less aura application on an armed target matching
// `affectedFamily` + a family-flag overlap with `affectedMask` is attributed
// to the player at `durationPct` percent of the aura's own player-modified
// base duration (the server's `max(1, CalculateDuration(caster) * pct / 100)`).
// For server mechanics that AddAura an aura off another spell's hit with no
// cast packet of its own (Turtle's Stinging Nettle: Mongoose Bite / fire-trap
// effects apply the highest known Serpent Sting rank at 20/40% duration).
// A re-trigger over an EXISTING matching aura is also handled: the server's
// re-add reuses the descriptor slot (no application event), so the trigger
// additionally refreshes the player's existing cached entry in place,
// honoring the server's keep-the-longer-remaining guard.
// `gateSpellId` (0 = none) keeps the rule live only while the player knows
// that spell (a talent-granted passive), read from the spell-knowledge bitmap
// at trigger time so respecs are honored; rules matching the same trigger are
// tried in registration order and the first live one arms (register the
// higher talent rank first). Returns false on bad input or a full table.
bool AddTriggeredApplication(uint32_t triggerSpellId, uint32_t gateSpellId,
                             uint32_t affectedFamily, uint64_t affectedMask,
                             int32_t durationPct);

// Family-mask trigger form of the above: the trigger matches any cast whose
// spell is SpellFamilyName `triggerFamily` with a family-flag overlap of
// `triggerMask` — rank-proof when the bit selects exactly the trigger set
// (verify against the DBC that no unrelated spell shares it first: hunter
// 0x4 is exactly the fire-trap effects, but 0x2 is Mongoose Bite AND Raptor
// Strike, so that trigger needs the exact-ID form).
bool AddTriggeredApplicationByFamily(uint32_t triggerFamily,
                                     uint64_t triggerMask, uint32_t gateSpellId,
                                     uint32_t affectedFamily,
                                     uint64_t affectedMask, int32_t durationPct);

// Register a tick-speed compression rule from C++: while an aura matching
// (triggerFamily, triggerMask) is live on a target, every cached aura on that
// target CAST BY THE SAME CASTER matching (affectedFamily, affectedMask) — and
// whose record carries a periodic damage/leech effect, the server's aura-type
// gate — drains an extra `pct`% of real time: its expiration moves earlier by
// pct% of the elapsed overlap. Mirrors a server that preserves an accelerated
// dot's tick COUNT, so ticking pct% faster consumes it pct% faster with no
// client-visible aura change (Turtle's Dark Harvest; see
// src/turtle/DarkHarvest.cpp for the live-behavior derivation). Caster-paired
// like the server's own check, so another warlock's observed DH compresses
// THEIR dots' cached timing too; caster-less entries never compress. The
// trigger's cache-entry lifetime is the compression window — created by its
// SpellGo, shortened by channel pushback (RestampPlayerChannel), evicted by
// OnAuraRemoved on interrupt / early end — so the window tracks every edge the
// server's has. Returns false on bad input or a full table.
bool AddTickCompression(uint32_t triggerFamily, uint64_t triggerMask,
                        uint32_t affectedFamily, uint64_t affectedMask,
                        int32_t pct);

// One cached aura, as returned by `Enumerate`.
struct CachedAura {
    uint32_t spellId;
    uint64_t casterGuid;   // 0 = caster unknown (application hook, no SpellGo)
    uint32_t expirationMs; // 0 = infinite / unknown
    uint32_t durationMs;   // applied duration (incl. caster mods); 0 = none
    int slot;              // descriptor slot the aura was seated in; feed it
                           // back to `Get` to reach this exact entry again
};

// Evicts every cached entry for `unitGuid` the unit's descriptor contradicts.
// `slotSpellIds` is the raw `UNIT_FIELD_AURA` array — exactly
// `Offsets::UNIT_AURA_TOTAL` spell IDs indexed by absolute slot, 0 for empty.
// Used to reconcile the cache against a unit's authoritative descriptor when
// it is back in view (a populated aura array means the unit is fully synced):
// an entry the descriptor no longer backs was removed while we couldn't
// observe it — e.g. a buff the owner cancelled while out of our range, whose
// `OnAuraRemoved` we never received — so drop it before the descriptor-drop
// fallback resurfaces it as a phantom.
//
// An entry seated in a slot is checked against THAT slot, so one caster's
// copy going away retires its own entry even while another caster keeps the
// spell ID present on the unit. Entries never seated are checked against the
// whole array. An all-empty array is ignored: it can't distinguish "out of
// range" from "genuinely buffless", and reconciling then would wrongly wipe
// still-valid out-of-range entries.
void EvictAbsent(uint64_t unitGuid, const uint32_t *slotSpellIds);

// Fills `out` with up to `maxOut` cached, non-expired auras on `unitGuid`
// whose helpful/harmful classification matches `harmful`. Returns the count
// written. The classification is recorded from the aura's descriptor slot at
// application time (`OnAuraAdded`/`OnAuraStacksChanged`); entries we only ever
// saw via `SMSG_SPELL_GO` have no slot and so an unknown classification —
// those are excluded (we can't tell which range they belong to).
//
// This is the fallback source for `Aura::Data` when the unit descriptor has
// dropped an aura's slot (rogue stealth, party range fluctuation) even though
// the aura is still active server-side: the cache survives descriptor clears.
int Enumerate(uint64_t unitGuid, bool harmful, CachedAura *out, int maxOut);

// Feeds the cache from an out-of-range group member's aura spell-ID array
// (`Group::MemberStats::AuraArray` — 48 slots, buffs 0..BUFF_COUNT-1, debuffs
// after). Such a member has no CGUnit and never yields SMSG_SPELL_GO, so the
// SpellGo path never learns their auras' timing — but SMSG_PARTY_MEMBER_STATS
// carries the spell IDs and the server sends it promptly when an aura is
// added/removed. So a spell ID that newly *appears* here did so ~one server
// tick + latency after the real application: we stamp a best-effort
// `now + base duration` expiration (base only — no remote caster mods, no
// caster recorded). Auras already present the first time we see a member have
// unknown age and are NOT stamped; only genuine absent->present transitions
// are. Idempotent within a frame; call it before enumerating the array so a
// just-appeared aura carries its guess on the same poll. The Store guard keeps
// any real SpellGo timing we already hold, so this never downgrades better data.
void ObserveGroupAuras(uint64_t guid, const uint16_t *auraArray);

// An aura arriving on a unit, leaving it, or changing its stack count, as the
// engine's own handlers see it (`OnAuraAdded`, `OnAuraRemoved`,
// `OnAuraStacksChanged`), with the caster this cache binds to it — or its
// duration set again while it stays (Refreshed, see `SignalRefreshed`).
enum class ChangeType { Added, Removed, Stacks, Refreshed };

struct Change {
    ChangeType type;
    void *unit;
    int slot;
    uint32_t spellId;
    uint64_t caster; // 0 when unknown
    bool harmful;
    int oldStacks; // Stacks only: the count before the change
    int newStacks; // Stacks only: the count now
};

using ChangeCallback = void (*)(const Change &change);

// Declare a file-scope `static const Aura::Source::AutoObserve _obs{&fn};`.
// Added and Stacks reach the callback after the cache has stamped the aura,
// Removed before the cache evicts it, so `caster` is the cache's in all three.
struct AutoObserve {
    explicit AutoObserve(ChangeCallback cb);
    ChangeCallback callback;
    const AutoObserve *next;
};

// A duration set again for an aura already on `unitGuid`: the server's
// SMSG_UPDATE_AURA_DURATION for an occupied slot of the player, a recast this
// cache refreshed, a server refresh mechanic it mirrors. Observers get it as a
// Refreshed change at the world tick, once per aura per frame, and only if the
// descriptor then still holds the spell — a slot that changed hands in the
// same tick was an application, not a refresh. `slot` may be SLOT_UNBOUND.
void SignalRefreshed(uint64_t unitGuid, uint32_t spellId, int slot);

} // namespace Aura::Source
