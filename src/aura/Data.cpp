// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// Aura-table primitives. See `Data.h` for the contract; see `Api.cpp`
// for the Lua-binding surface that consumes these.

#include "Data.h"

#include "Game.h"
#include "Offsets.h"
#include "aura/Source.h"
#include "dbc/Lookup.h"
#include "group/MemberStats.h"
#include "guid/Guid.h"
#include "spell/CrowdControl.h"
#include "spell/IsSelfBuff.h"
#include "time/Clock.h"
#include "turtle/Detect.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstring>

namespace Aura::Data {

namespace {

const uint8_t *Descriptor(const uint8_t *unit) {
    if (unit == nullptr)
        return nullptr;
    return *reinterpret_cast<const uint8_t *const *>(
        unit + Offsets::OFF_CGUNIT_OBJECT_FIELDS);
}

// Keys into the `Aura::Source` caster/expiration cache. Thin alias for the
// shared `Unit::Identity::GuidForObject` so the lookup here and the
// OnAuraAdded hook in `Aura::Source` read the GUID the same way.
uint64_t UnitGuid(const uint8_t *unit) {
    return Unit::Identity::GuidForObject(unit);
}

const uint8_t *SpellRecord(uint32_t spellID) {
    return DBC::Record(Offsets::VAR_SPELL_RECORDS,
                       Offsets::VAR_SPELL_RECORD_COUNT, spellID);
}

int LocaleIndex() {
    return *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_LOCALE_INDEX));
}

const char *LocalizedSpellName(const uint8_t *spellRecord) {
    if (spellRecord == nullptr)
        return nullptr;
    const auto *names = reinterpret_cast<const char *const *>(
        spellRecord + Offsets::OFF_SPELL_NAMES);
    const char *s = names[LocaleIndex()];
    return (s == nullptr || *s == '\0') ? nullptr : s;
}

const char *SpellIconPath(const uint8_t *spellRecord) {
    if (spellRecord == nullptr)
        return nullptr;
    const int iconID = *reinterpret_cast<const int *>(
        spellRecord + Offsets::OFF_SPELL_RECORD_ICON_ID);
    if (iconID <= 0)
        return nullptr;
    return DBC::StringField(Offsets::VAR_SPELL_ICON_RECORDS,
                            Offsets::VAR_SPELL_ICON_COUNT,
                            static_cast<uint32_t>(iconID),
                            Offsets::OFF_SPELLICON_PATH);
}

// Returns the local player's CGUnit pointer, or nullptr pre-login.
const uint8_t *LocalPlayer() {
    return static_cast<const uint8_t *>(Game::ResolveUnitToken("player"));
}

// Looks up the player-buff-table entry for a given spellID. The table
// at `VAR_PLAYER_BUFF_TABLE` mirrors the local player's auras with
// timing data (which UNIT_FIELD_AURA lacks for all units). Returns
// nullptr when the spell isn't in any populated entry.
//
// Only meaningful when the caller is building data for the local
// player — every entry here belongs to "player". Caller is expected
// to gate on that.
const uint8_t *FindPlayerBuffEntry(uint32_t spellID) {
    auto *base = reinterpret_cast<const uint8_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PLAYER_BUFF_TABLE));
    for (int i = 0; i < Offsets::PLAYER_BUFF_TABLE_COUNT; ++i) {
        const uint8_t *entry = base + i * Offsets::PLAYER_BUFF_ENTRY_STRIDE;
        const int slotCode = *reinterpret_cast<const int *>(
            entry + Offsets::OFF_PLAYER_BUFF_SLOT_CODE);
        if (slotCode < 0)
            continue;
        const uint32_t entrySpell = *reinterpret_cast<const uint32_t *>(
            entry + Offsets::OFF_PLAYER_BUFF_SPELL_ID);
        if (entrySpell == spellID)
            return entry;
    }
    return nullptr;
}

// Reads the absolute expiration timestamp (in seconds, GetTime-epoch)
// for a player-buff entry, or 0 if the entry has expired / has no
// timing. The engine stores ms-since-tickcount-epoch which matches
// what Lua's `GetTime()` returns (engineMs * 0.001), so converting
// `expirationMs * 0.001` lands directly on the same timeline addons
// compare against on the Lua side. No epoch reconciliation needed.
//
// **Read as `uint32_t`**: GetTickCount overflows `INT_MAX` at ~24.86
// days of system uptime. A signed read flips negative past that
// point, which broke a `<= 0` early-bail that was supposed to catch
// only the "no timing" (0) sentinel. The engine's own arithmetic
// works on the unsigned tick because both sides of `expiration -
// currentMs` wrap together.
double PlayerBuffExpirationSeconds(const uint8_t *entry) {
    if (entry == nullptr)
        return 0.0;
    const int slotCode = *reinterpret_cast<const int *>(
        entry + Offsets::OFF_PLAYER_BUFF_SLOT_CODE);
    if (slotCode < 0)
        return 0.0;
    auto *expirationTable = reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PLAYER_BUFF_EXPIRATION_TABLE));
    const uint32_t expirationMs = expirationTable[slotCode];
    if (expirationMs == 0)
        return 0.0;
    // Report "no timer" once the stored expiration has elapsed — this is not a
    // heuristic, it's exactly what the engine's own GetPlayerBuffTimeLeft does.
    // Script_GetPlayerBuffTimeLeft (0x004e4930) → FUN_004e4450 reads this same
    // expiration table and returns:
    //     if ((int)(now - expiration[slot]) >= 0) return 0;  // elapsed -> 0
    //     return expiration[slot] - now;                      // else remaining
    // Permanent auras (paladin auras, passives, racials) carry an infinite
    // Spell.dbc duration (SpellDuration base -1); the server stores the fixed
    // cast-time in this slot for them, so it's always "already elapsed" and the
    // engine shows no countdown. `Time::Clock::Reached` takes the signed diff, so
    // it also absorbs the ~24.86-day GetTickCount wrap (both operands wrap
    // together).
    if (Time::Clock::Reached(expirationMs))
        return 0.0; // expiration reached/passed → permanent or no timer
    return static_cast<double>(expirationMs) * 0.001;
}

// Computes the spell's base duration in seconds via Spell.dbc →
// SpellDuration.dbc lookup with level scaling. Returns 0 for spells
// without a real duration (the sentinel `base < 0 && perLevel == 0`
// path the engine uses for infinite auras like passives, paladin
// auras, racials).
//
// Doesn't include talent / glyph / aura-extension modifiers — those
// are caster-side and the engine applies them when computing the
// expiration timestamp. So `expirationTime - GetTime()` reflects the
// *true* remaining time; this `duration` is the base value modern
// addons use for "X / Y" timer rendering.
double SpellBaseDurationSeconds(uint32_t spellID, int unitLevel) {
    const uint8_t *spell = SpellRecord(spellID);
    if (spell == nullptr)
        return 0.0;
    const int durIdx = *reinterpret_cast<const int *>(
        spell + Offsets::OFF_SPELL_DURATION_INDEX);
    if (durIdx <= 0)
        return 0.0;
    const uint8_t *durRec = DBC::Record(Offsets::VAR_SPELLDURATION_RECORDS,
                                        Offsets::VAR_SPELLDURATION_COUNT,
                                        static_cast<uint32_t>(durIdx));
    if (durRec == nullptr)
        return 0.0;
    const int base = *reinterpret_cast<const int *>(
        durRec + Offsets::OFF_SPELLDURATION_BASE_MS);
    const int perLevel = *reinterpret_cast<const int *>(
        durRec + Offsets::OFF_SPELLDURATION_PER_LEVEL_MS);
    const int maxMs = *reinterpret_cast<const int *>(
        durRec + Offsets::OFF_SPELLDURATION_MAX_MS);
    // Engine's "infinite duration" sentinel — matches the check in
    // `FUN_004E44B0`'s "no real duration" branch at `0x004e455c`.
    if (base < 0 && perLevel < 1)
        return 0.0;
    const int spellBaseLevel = *reinterpret_cast<const int *>(
        spell + Offsets::OFF_SPELL_BASE_LEVEL);
    int effLevel = unitLevel;
    if (effLevel < spellBaseLevel)
        effLevel = spellBaseLevel;
    int durationMs = (effLevel - spellBaseLevel) * perLevel + base;
    if (maxMs > 0 && durationMs > maxMs)
        durationMs = maxMs;
    if (durationMs <= 0)
        return 0.0;
    return static_cast<double>(durationMs) * 0.001;
}

// Reads the player's level from UNIT_FIELD_LEVEL. Returns 0 when
// unresolved (pre-login, no descriptor) — caller should treat that
// as "skip level scaling".
// True if the engine-tick expiration `expMs` has already reached/passed now.
// Same signed-wrap-safe compare `PlayerBuffExpirationSeconds` uses (absorbs
// the ~24.86-day GetTickCount wrap). An already-elapsed cached expiration is
// not meaningful — the caster is kept regardless, but the time is reported as
// unknown rather than as a negative remaining.
bool ExpirationElapsed(uint32_t expMs) { return Time::Clock::Reached(expMs); }

int PlayerLevel(const uint8_t *player) {
    auto *desc = Descriptor(player);
    if (desc == nullptr)
        return 0;
    return *reinterpret_cast<const int *>(
        desc + Offsets::OFF_UNIT_FIELD_LEVEL);
}

// Caster + timing attributed to ONE aura instance — the aura `spellID` sitting
// in absolute descriptor `slot` on `guid` (`Aura::Source::SLOT_UNBOUND` when
// the caller has no slot: the out-of-range group array, the cache fallback).
// The slot is what tells two casters' copies of one spell apart; without it
// they are indistinguishable from a spell ID alone.
//
// The caster is the observed one from the Aura::Source cache, else — when we
// never saw the cast — the unit itself if the spell is a self-only-target buff
// (self-buffs can't be cross-cast), else 0. Single source of truth for both
// display (sourceGUID/sourceUnit) and the PLAYER caster filter, so a self-buff
// shows a source AND matches HELPFUL|PLAYER instead of the two paths
// disagreeing.
struct Attribution {
    uint64_t caster;
    uint32_t expirationMs;
    uint32_t durationMs;
};

Attribution Attribute(uint64_t guid, uint32_t spellID, int slot) {
    Attribution a = {0, 0, 0};
    if (guid == 0 || spellID == 0)
        return a;
    Aura::Source::Get(guid, spellID, slot, &a.caster, &a.expirationMs,
                      &a.durationMs);
    if (a.caster == 0 && Spell::IsSelfBuff::IsSelfBuff(spellID))
        a.caster = guid; // self-only-target aura → cast by the unit itself
    return a;
}

} // namespace

uint32_t ReadSpellID(const uint8_t *unit, int slot) {
    if (slot < 0 || slot >= Offsets::UNIT_AURA_TOTAL)
        return 0;
    auto *desc = Descriptor(unit);
    if (desc == nullptr)
        return 0;
    return *reinterpret_cast<const uint32_t *>(
        desc + Offsets::OFF_UNIT_FIELD_AURA + slot * 4);
}

bool IsSlotPopulated(const uint8_t *unit, int slot) {
    if (slot < 0 || slot >= Offsets::UNIT_AURA_TOTAL)
        return false;
    auto *desc = Descriptor(unit);
    if (desc == nullptr)
        return false;
    const uint32_t spellID = *reinterpret_cast<const uint32_t *>(
        desc + Offsets::OFF_UNIT_FIELD_AURA + slot * 4);
    if (spellID == 0)
        return false;
    const uint8_t byte = *(desc + Offsets::OFF_UNIT_FIELD_AURAFLAGS + slot / 2);
    const uint8_t nibble = (byte >> ((slot & 1) * 4)) & 0xF;
    if ((nibble & Offsets::UNIT_AURA_VISIBLE_MASK) == 0)
        return false;
    return IsVisible(SpellRecord(spellID));
}

// Every nibble Turtle's SetAuraFlag can write for an occupied slot: HELPFUL,
// HELPFUL|CANCELABLE, or HARMFUL alone. Nothing else is reachable there — a
// negative aura gets exactly HARMFUL, and a positive one always carries HELPFUL.
bool IsTurtlePolarityNibble(uint8_t nibble) {
    return nibble == Offsets::UNIT_AURA_FLAG_HELPFUL ||
           nibble == (Offsets::UNIT_AURA_FLAG_HELPFUL | Offsets::UNIT_AURA_FLAG_CANCELABLE) ||
           nibble == Offsets::UNIT_AURA_FLAG_HARMFUL;
}

// Latched for the process once a nibble proves the flags are not polarity here.
// A server does not change its encoding mid-session, so this never resets.
bool g_nibbleContradicted = false;

bool IsSlotHarmful(const uint8_t *unit, int slot) {
    if (slot < 0 || slot >= Offsets::UNIT_AURA_TOTAL)
        return false;

    // Normally the slot range IS the polarity: an aura is written to 0..31 when
    // helpful and 32..47 when harmful, and nothing ever crosses over. The flag
    // nibble cannot be used here, because in a stock descriptor those bits mean
    // something else entirely — one bit per spell effect index that carries an
    // aura, plus a cancelable bit. Reading bit 0x08 as "harmful" would then be
    // reading "effect index 0 has an aura", which is true of nearly every aura,
    // and would report almost every BUFF as a debuff.
    if (!Turtle::Detected())
        return slot >= Offsets::UNIT_AURA_BUFF_COUNT;

    // Turtle lets a debuff spill into a buff slot once the harmful range is
    // full, so the range stops being reliable — and it repurposes those same
    // nibble bits to carry the real polarity (0x04 helpful, 0x08 harmful) so
    // the information survives the spill. There, the nibble is the answer.
    if (g_nibbleContradicted)
        return slot >= Offsets::UNIT_AURA_BUFF_COUNT;

    auto *desc = Descriptor(unit);
    if (desc == nullptr)
        return false;
    const uint8_t byte = *(desc + Offsets::OFF_UNIT_FIELD_AURAFLAGS + slot / 2);
    const uint8_t nibble = (byte >> ((slot & 1) * 4)) & 0xF;

    // Fail safe. Detection says this server SHOULD be writing polarity here, so
    // a nibble it could not have produced means that assumption is stale — the
    // server changed how it writes the flags. Fall back to the slot range for
    // good rather than keep reading bits whose meaning we no longer know: the
    // range is merely incomplete for spilled debuffs, whereas a misread nibble
    // reports nearly every buff as a debuff. Revocation only, never promotion —
    // nothing here can turn nibble reading ON, because the agreeing case is not
    // provable (an all-0x08 run is equally consistent with single-effect auras
    // under the other encoding).
    if (nibble != 0 && !IsTurtlePolarityNibble(nibble)) {
        g_nibbleContradicted = true;
        return slot >= Offsets::UNIT_AURA_BUFF_COUNT;
    }
    return (nibble & Offsets::UNIT_AURA_FLAG_HARMFUL) != 0;
}

bool IsSlotTooltipVisible(const uint8_t *unit, int slot) {
    if (slot < 0 || slot >= Offsets::UNIT_AURA_TOTAL)
        return false;
    auto *desc = Descriptor(unit);
    if (desc == nullptr)
        return false;
    const int spellID = *reinterpret_cast<const int *>(
        desc + Offsets::OFF_UNIT_FIELD_AURA + slot * 4);
    if (spellID <= 0) // the engine's own `0 < id` test (signed)
        return false;
    const uint8_t byte = *(desc + Offsets::OFF_UNIT_FIELD_AURAFLAGS + slot / 2);
    const uint8_t nibble = (byte >> ((slot & 1) * 4)) & 0xF;
    if ((nibble & Offsets::UNIT_AURA_VISIBLE_MASK) == 0)
        return false;
    const uint8_t *rec = SpellRecord(static_cast<uint32_t>(spellID));
    if (rec == nullptr)
        return false;
    using Fn = char(__fastcall *)(const uint8_t *);
    auto fn = reinterpret_cast<Fn>(
        static_cast<uintptr_t>(Offsets::FUN_GAMETOOLTIP_AURA_VISIBLE));
    return fn(rec) != 0;
}

int SlotInFilterOrder(Filter filter, int i) {
    // Harmful: 32..47 first, then 0..31 (where the server parks debuffs once
    // the 16 are full). Helpful: 0..31 then 32..47 — positive auras never
    // spill downward in practice, so that tail is a no-op safety net.
    if (filter == Filter::Harmful)
        return (i + Offsets::UNIT_AURA_BUFF_COUNT) % Offsets::UNIT_AURA_TOTAL;
    return i;
}

bool IsPlayerCast(const uint8_t *unit, int slot) {
    const uint32_t spellID = ReadSpellID(unit, slot);
    if (spellID == 0)
        return false;
    const uint64_t player = Unit::Identity::PlayerGuid();
    return player != 0 && Attribute(UnitGuid(unit), spellID, slot).caster == player;
}

// Applies the PLAYER / !PLAYER caster restriction. `isPlayerCast` is the
// per-aura "was this cast by the local player" answer (false on a cache miss).
bool CasterMatches(CasterMode caster, bool isPlayerCast) {
    switch (caster) {
        case CasterMode::PlayerOnly: return isPlayerCast;
        case CasterMode::NotPlayer:  return !isPlayerCast;
        default:                     return true; // Any
    }
}

bool IsDispellable(uint32_t spellID) {
    const uint8_t *rec = SpellRecord(spellID);
    if (rec == nullptr)
        return false;
    const uint32_t dispelType = *reinterpret_cast<const uint32_t *>(
        rec + Offsets::OFF_SPELL_DISPEL_TYPE);
    // Magic(1) / Curse(2) / Disease(3) / Poison(4) — the server's
    // DISPEL_ALL_MASK. None(0) / Stealth(5) / Invisibility(6) / Enrage(9)
    // etc. are not dispellable by any mechanism.
    return dispelType >= 1 && dispelType <= 4;
}

bool DispelMatches(DispelMode dispel, uint32_t spellID) {
    switch (dispel) {
        case DispelMode::DispellableOnly: return IsDispellable(spellID);
        case DispelMode::NotDispellable:  return !IsDispellable(spellID);
        default:                          return true; // Any
    }
}

bool CcMatches(CcMode cc, uint32_t spellID) {
    switch (cc) {
        case CcMode::CrowdControlOnly:
            return Spell::CrowdControl::IsCrowdControl(spellID);
        case CcMode::NotCrowdControl:
            return !Spell::CrowdControl::IsCrowdControl(spellID);
        default:
            return true; // Any
    }
}

bool MatchesAura(const Match &match, bool isPlayerCast, uint32_t spellID) {
    return CasterMatches(match.caster, isPlayerCast) &&
           DispelMatches(match.dispel, spellID) &&
           CcMatches(match.cc, spellID);
}

// `MatchesAura` for a descriptor slot the caller has ALREADY established is
// populated. The caster lookup is the expensive predicate (a scan of the
// Aura::Source cache per slot), and `CasterMatches` ignores its answer when the
// match has no caster restriction — so skip the lookup entirely in that case.
static bool PopulatedSlotMatches(const uint8_t *unit, int slot,
                                 const Match &match) {
    const bool castByPlayer =
        match.caster != CasterMode::Any && IsPlayerCast(unit, slot);
    return MatchesAura(match, castByPlayer, ReadSpellID(unit, slot));
}

bool SlotMatches(const uint8_t *unit, int slot, const Match &match) {
    return IsSlotPopulated(unit, slot) && PopulatedSlotMatches(unit, slot, match);
}

bool SlotMatchesFilter(const uint8_t *unit, int slot, Filter filter,
                       const Match &match) {
    if (!IsSlotPopulated(unit, slot))
        return false;
    if (IsSlotHarmful(unit, slot) != (filter == Filter::Harmful))
        return false;
    return PopulatedSlotMatches(unit, slot, match);
}

// Group-array analog of IsPlayerCast: the member has no descriptor, so there is
// no slot to attribute by and the cache is consulted by (guid, spellID) alone.
// A miss counts as "not the player" (same as IsPlayerCast).
bool GroupIsPlayerCast(uint64_t guid, uint32_t spellID) {
    const uint64_t player = Unit::Identity::PlayerGuid();
    return player != 0 &&
           Attribute(guid, spellID, Aura::Source::SLOT_UNBOUND).caster == player;
}

int FindNthSlot(const uint8_t *unit, int oneBasedIndex, Filter filter,
                Match match) {
    if (unit == nullptr || oneBasedIndex < 1)
        return -1;
    int matches = 0;
    for (int i = 0; i < Offsets::UNIT_AURA_TOTAL; ++i) {
        const int slot = SlotInFilterOrder(filter, i);
        if (!SlotMatchesFilter(unit, slot, filter, match))
            continue;
        if (++matches == oneBasedIndex)
            return slot;
    }
    return -1;
}

int FindSlotBySpellID(const uint8_t *unit, uint32_t spellID,
                      const Filter *filter, Match match) {
    if (unit == nullptr || spellID == 0)
        return -1;
    for (int i = 0; i < Offsets::UNIT_AURA_TOTAL; ++i) {
        const int slot = (filter != nullptr) ? SlotInFilterOrder(*filter, i) : i;
        if (!IsSlotPopulated(unit, slot))
            continue;
        if (ReadSpellID(unit, slot) != spellID)
            continue;
        if (filter != nullptr &&
            IsSlotHarmful(unit, slot) != (*filter == Filter::Harmful))
            continue;
        if (!PopulatedSlotMatches(unit, slot, match))
            continue;
        return slot;
    }
    return -1;
}

int FindSlotBySpellName(const uint8_t *unit, const char *spellName,
                        const Filter *filter, Match match) {
    if (unit == nullptr || spellName == nullptr || *spellName == '\0')
        return -1;
    for (int i = 0; i < Offsets::UNIT_AURA_TOTAL; ++i) {
        const int slot = (filter != nullptr) ? SlotInFilterOrder(*filter, i) : i;
        if (!IsSlotPopulated(unit, slot))
            continue;
        const char *name = LocalizedSpellName(SpellRecord(ReadSpellID(unit, slot)));
        if (name == nullptr || std::strcmp(name, spellName) != 0)
            continue;
        if (filter != nullptr &&
            IsSlotHarmful(unit, slot) != (*filter == Filter::Harmful))
            continue;
        if (!PopulatedSlotMatches(unit, slot, match))
            continue;
        return slot;
    }
    return -1;
}

int ReadStacks(const uint8_t *unit, int slot) {
    if (slot < 0 || slot >= Offsets::UNIT_AURA_TOTAL)
        return 0;
    auto *desc = Descriptor(unit);
    if (desc == nullptr)
        return 0;
    const uint8_t stored = *reinterpret_cast<const uint8_t *>(
        desc + Offsets::OFF_UNIT_FIELD_AURAAPPLICATIONS + slot);
    return static_cast<int>(stored) + 1;
}

bool IsVisible(const uint8_t *spellRecord) {
    if (spellRecord == nullptr)
        return false;
    using Fn = char(__fastcall *)(const uint8_t *);
    auto fn = reinterpret_cast<Fn>(
        static_cast<uintptr_t>(Offsets::FUN_SPELL_IS_VISIBLE_AURA));
    return fn(spellRecord) != 0;
}

const char *DispelName(uint32_t dispelTypeID) {
    if (dispelTypeID == 0)
        return nullptr;
    const int count = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_SPELLDISPEL_COUNT));
    if (static_cast<int>(dispelTypeID) > count)
        return nullptr;
    auto *records = *reinterpret_cast<const uint8_t *const *const *>(
        static_cast<uintptr_t>(Offsets::VAR_SPELLDISPEL_RECORDS));
    if (records == nullptr)
        return nullptr;
    const uint8_t *record = records[dispelTypeID];
    if (record == nullptr)
        return nullptr;
    if (*reinterpret_cast<const int *>(
            record + Offsets::OFF_SPELLDISPEL_HAS_NAME) == 0)
        return nullptr;
    const char *name = *reinterpret_cast<const char *const *>(
        record + Offsets::OFF_SPELLDISPEL_NAME);
    return (name == nullptr || *name == '\0') ? nullptr : name;
}

// The display fields resolved from a spell record + caster, shared by the table
// emitter (`BuildTable`) and the positional emitter (`PushPositional`) so the
// name / icon / dispel-name lookups live in exactly one place. `castByPlayer`
// is the positional tuple's field #13 and the table's
// `isFromPlayerOrPlayerPet` — same value, retail's two names for the two aura
// shapes. Both mean "applied by ANY player or a player's pet" (Era
// semantics), not the local player — that's the `PLAYER` filter's job. The
// answer is the caster GUID's HIGHGUID prefix: player 0x0000 / pet 0xF140
// (server-confirmed in tortoise-wow ObjectGuid.h). Vanilla totems carry the
// creature prefix, so totem buffs read false. False on a cache miss (cast not
// observed), since a GUID of 0 classifies as neither.
struct Display {
    const char *name;
    const char *icon;
    const char *dispelName;
    bool castByPlayer;
};

static Display ResolveDisplay(uint32_t spellID, uint64_t casterGuid) {
    const uint8_t *spellRecord = SpellRecord(spellID);
    uint32_t dispelTypeID = 0;
    if (spellRecord != nullptr) {
        dispelTypeID = *reinterpret_cast<const uint32_t *>(
            spellRecord + Offsets::OFF_SPELL_DISPEL_TYPE);
    }
    Display d;
    d.name = LocalizedSpellName(spellRecord);
    d.icon = SpellIconPath(spellRecord);
    d.dispelName = DispelName(dispelTypeID);
    d.castByPlayer = Guid::IsPlayer(casterGuid) || Guid::IsPet(casterGuid);
    return d;
}

// Writes the full modern AuraData table on top of the Lua stack from
// already-resolved values. Shared by the descriptor path (`Push`) and the
// cache-fallback path (`PushFromCache`). Net stack effect: +1 (the table).
static void BuildTable(void *L, uint32_t spellID, int applications,
                       bool isHelpful, double duration, double expirationTime,
                       uint64_t casterGuid) {
    const Display disp = ResolveDisplay(spellID, casterGuid);
    const char *name = disp.name;
    const char *icon = disp.icon;
    const char *dispel = disp.dispelName;

    Game::Lua::NewTable(L);

    Game::Lua::SetFieldString(L, "name", name);
    Game::Lua::SetFieldString(L, "icon", icon);
    Game::Lua::SetFieldNumber(L, "applications",
                              static_cast<double>(applications));
    Game::Lua::SetFieldNumber(L, "spellId", static_cast<double>(spellID));
    Game::Lua::SetFieldString(L, "dispelName", dispel);
    Game::Lua::SetFieldBool(L, "isHelpful", isHelpful);
    Game::Lua::SetFieldBool(L, "isHarmful", !isHelpful);
    Game::Lua::SetFieldNumber(L, "duration", duration);
    Game::Lua::SetFieldNumber(L, "expirationTime", expirationTime);

    if (casterGuid != 0) {
        // `sourceUnit` is a token (nil when the caster maps to no current
        // token); `sourceGUID` is the raw "0x..." GUID and is set whenever we
        // have a caster — a ClassicAPI extension (not in retail AuraData) that
        // survives the caster leaving token range and doubles as a unit token
        // under SuperWoW.
        char tokenBuf[32];
        char guidBuf[Guid::STRING_SIZE];
        const char *sourceUnit = Unit::Identity::TokenFromGUID(
            casterGuid, tokenBuf, sizeof tokenBuf);
        const char *sourceGUID =
            Guid::FormatAsString(casterGuid, guidBuf, sizeof guidBuf);
        if (sourceUnit != nullptr)
            Game::Lua::SetFieldString(L, "sourceUnit", sourceUnit);
        if (sourceGUID != nullptr)
            Game::Lua::SetFieldString(L, "sourceGUID", sourceGUID);
    }

    Game::Lua::SetFieldNumber(L, "charges", 0);
    Game::Lua::SetFieldNumber(L, "maxCharges", 0);
    Game::Lua::SetFieldNumber(L, "timeMod", 1);

    Game::Lua::SetFieldBool(L, "isFromPlayerOrPlayerPet", disp.castByPlayer);

    // Boolean fields whose modern semantics don't apply in vanilla.
    Game::Lua::SetFieldBool(L, "isStealable", false);
    Game::Lua::SetFieldBool(L, "isBossAura", false);
    Game::Lua::SetFieldBool(L, "isNameplateOnly", false);
    Game::Lua::SetFieldBool(L, "nameplateShowAll", false);
    Game::Lua::SetFieldBool(L, "nameplateShowPersonal", false);
    Game::Lua::SetFieldBool(L, "canApplyAura", false);
    Game::Lua::SetFieldBool(L, "shouldConsolidate", false);
    Game::Lua::SetFieldBool(L, "isRaid", false);

    // `auraInstanceID` and `points` are deliberately omitted — modern returns
    // nil for those when they don't apply, and Lua reading a missing key
    // yields nil, so the table doesn't need an explicit entry.
}

// Emits the Classic-Era `UnitAura` 15-value tuple onto the Lua stack from the
// same already-resolved values `BuildTable` takes. Net stack effect: +15 — NO
// table allocation, the zero-GC path behind `C_UnitAuras.UnitAura`. Field order
// matches Classic-Era `UnitAura`: name, icon, count, dispelType(string),
// duration, expirationTime, source, isStealable, nameplateShowPersonal, spellId,
// canApplyAura, isBossDebuff, castByPlayer, nameplateShowAll, timeMod. Values
// mirror the AuraData table exactly so packed / non-packed callers agree: name /
// icon / dispelType push `""` when absent (as `SetFieldString` does), while
// `source` pushes nil when there's no caster (as the table omits `sourceUnit`).
// `isHelpful` is unused (the tuple has no isHelpful/isHarmful) — kept so the leaf
// dispatch calls either emitter with a uniform arg list.
static void PushPositional(void *L, uint32_t spellID, int applications,
                           bool /*isHelpful*/, double duration,
                           double expirationTime, uint64_t casterGuid) {
    const Display disp = ResolveDisplay(spellID, casterGuid);

    const char *source = nullptr;
    char tokenBuf[32];
    if (casterGuid != 0)
        source = Unit::Identity::TokenFromGUID(casterGuid, tokenBuf,
                                               sizeof tokenBuf);

    Game::Lua::PushString(L, disp.name ? disp.name : "");            // 1  name
    Game::Lua::PushString(L, disp.icon ? disp.icon : "");            // 2  icon
    Game::Lua::PushNumber(L, static_cast<double>(applications));     // 3  count
    Game::Lua::PushString(L, disp.dispelName ? disp.dispelName : ""); // 4  dispelType
    Game::Lua::PushNumber(L, duration);                              // 5  duration
    Game::Lua::PushNumber(L, expirationTime);                        // 6  expirationTime
    Game::Lua::PushString(L, source);                                // 7  source (nil if none)
    Game::Lua::PushBool(L, false);                                   // 8  isStealable
    Game::Lua::PushBool(L, false);                                   // 9  nameplateShowPersonal
    Game::Lua::PushNumber(L, static_cast<double>(spellID));          // 10 spellId
    Game::Lua::PushBool(L, false);                                   // 11 canApplyAura
    Game::Lua::PushBool(L, false);                                   // 12 isBossDebuff
    Game::Lua::PushBool(L, disp.castByPlayer);                       // 13 castByPlayer
    Game::Lua::PushBool(L, false);                                   // 14 nameplateShowAll
    Game::Lua::PushNumber(L, 1);                                     // 15 timeMod
}

// Enriches a bare `(guid, spellID)` into a full AuraData table — the shared
// core behind every push path (descriptor, cache fallback, group-array). No
// dependence on a live descriptor: the descriptor path passes what it read
// (stacks, level, is-this-the-player), the others pass their known-or-default
// values.
//
// Timing rules (identical across paths): `duration` is the Spell.dbc →
// SpellDuration.dbc base scaled by `unitLevel` (0 skips scaling); talent/glyph
// duration modifiers are baked into the caster-side expiration, so
// `expirationTime - GetTime()` is the true remaining time even when `duration`
// omits the boost. `expirationTime` for the local player comes from the engine
// buff table (gated on `isPlayer`); for everyone else it, the caster, and the
// applied (caster-modified) duration come from the `Aura::Source` SMSG_SPELL_GO
// cache when it observed the cast — a miss leaves the modern-truthful defaults
// (expiration 0, no sourceUnit/GUID). `slot` is the absolute descriptor slot
// the aura occupies, which is what attributes it to the right caster when two
// of them hold the same spell on the unit; `Aura::Source::SLOT_UNBOUND` for the
// paths with no descriptor to read one from.
//
// `known` short-circuits the by-(guid,spell,slot) resolve: the cache-fallback
// paths already hold the exact entry's attribution (Enumerate copied it out),
// so they pass it straight through. Re-resolving there would call `Get` a second
// time and, for an unbound entry sharing its spell with another caster's entry,
// return a miss — dropping a caster Enumerate already knew. Null for the
// descriptor / group-array paths, which have no per-entry attribution in hand.
static void PushEnriched(void *L, uint64_t guid, uint32_t spellID,
                         bool isHelpful, int applications, int unitLevel,
                         bool isPlayer, int slot,
                         const Attribution *known = nullptr,
                         Emit emit = Emit::Table) {
    double duration = 0.0;
    double expirationTime = 0.0;
    uint64_t casterGuid = 0;
    if (spellID != 0)
        duration = SpellBaseDurationSeconds(spellID, unitLevel);
    if (isPlayer && spellID != 0) {
        const uint8_t *entry = FindPlayerBuffEntry(spellID);
        if (entry != nullptr)
            expirationTime = PlayerBuffExpirationSeconds(entry);
    }
    if (spellID != 0 && guid != 0) {
        // Caster (sourceGUID/sourceUnit) and timing resolved together, so a
        // self-buff both shows a source and matches HELPFUL|PLAYER instead of
        // the two paths disagreeing.
        Attribution a = (known != nullptr) ? *known : Attribute(guid, spellID, slot);
        // Self-buff inference for a pre-resolved entry whose caster the cache
        // never learned (idempotent on the Attribute() path, which already
        // applied it — the caster is nonzero there when it matched).
        if (a.caster == 0 && Spell::IsSelfBuff::IsSelfBuff(spellID))
            a.caster = guid;
        // A cached expiration that already elapsed while the aura is still
        // present (non-player casters get an underestimated base duration)
        // is not meaningful — report unknown (0) rather than a negative
        // remaining time.
        if (expirationTime == 0.0 && a.expirationMs != 0 &&
            !ExpirationElapsed(a.expirationMs))
            expirationTime = static_cast<double>(a.expirationMs) * 0.001;
        if (a.durationMs != 0)
            duration = static_cast<double>(a.durationMs) * 0.001;
        casterGuid = a.caster;
    }
    if (emit == Emit::Positional)
        PushPositional(L, spellID, applications, isHelpful, duration,
                       expirationTime, casterGuid);
    else
        BuildTable(L, spellID, applications, isHelpful, duration, expirationTime,
                   casterGuid);
}

// Attribution held directly by an `Aura::Source` cache entry (from `Enumerate`),
// for the cache-fallback push paths to hand to `PushEnriched` as `known`.
static Attribution CachedAttribution(const Aura::Source::CachedAura &c) {
    return {c.casterGuid, c.expirationMs, c.durationMs};
}

void Push(void *L, const uint8_t *unit, int slot, Emit emit) {
    const uint32_t spellID = ReadSpellID(unit, slot);
    const bool isHelpful = !IsSlotHarmful(unit, slot); // nibble, not slot range
    const int unitLevel = (unit != nullptr) ? PlayerLevel(unit) : 0;
    PushEnriched(L, UnitGuid(unit), spellID, isHelpful, ReadStacks(unit, slot),
                 unitLevel, unit != nullptr && unit == LocalPlayer(), slot,
                 /*known*/ nullptr, emit);
}

namespace {

// Builds an AuraData table from an `Aura::Source` cache entry — the fallback
// when the descriptor has dropped the aura's slot. Stack count isn't broadcast
// in SMSG_SPELL_GO, so `applications` defaults to 1. `duration` uses the
// applied (caster-modified) ms when known, else the Spell.dbc base.
void PushFromCache(void *L, const uint8_t *unit,
                   const Aura::Source::CachedAura &c, bool isHelpful,
                   Emit emit = Emit::Table) {
    // `c` already carries this exact instance's caster + timing (Enumerate
    // copied it out), so hand it to `PushEnriched` as `known` rather than
    // re-resolving by (guid, spell, slot) — an unbound entry sharing its spell
    // with another caster's would otherwise resolve to a miss and drop the
    // caster. Stacks aren't in SMSG_SPELL_GO, so `applications` is 1.
    const int unitLevel = (unit != nullptr) ? PlayerLevel(unit) : 0;
    const Attribution a = CachedAttribution(c);
    PushEnriched(L, UnitGuid(unit), c.spellId, isHelpful, 1, unitLevel, false,
                 c.slot, &a, emit);
}

// Reconciles the `Aura::Source` cache against `unit`'s descriptor: when the
// descriptor holds at least one raw aura the unit is in view and fully synced,
// so it's authoritative — any cached entry for the unit whose spell isn't in
// the array was removed while we couldn't see it (e.g. cancelled out of range,
// so no OnAuraRemoved reached us) and must be dropped before the fallback
// resurfaces it as a phantom (an enumerated aura whose SetUnitAura tooltip is
// empty). Reads the RAW slots (not IsSlotPopulated): a spell that's present
// but flag-hidden is still legitimately on the unit. Skips when the array is
// empty — that can't be told apart from an out-of-range descriptor drop, and
// reconciling then would wipe still-valid out-of-range entries.
void ReconcileCache(const uint8_t *unit) {
    if (unit == nullptr)
        return;
    uint32_t present[Offsets::UNIT_AURA_TOTAL];
    for (int slot = 0; slot < Offsets::UNIT_AURA_TOTAL; ++slot)
        present[slot] = ReadSpellID(unit, slot);
    Aura::Source::EvictAbsent(UnitGuid(unit), present);
}

// True if `unit`'s descriptor currently exposes any visible aura in either
// range. The cache fallback is only trustworthy when the descriptor has been
// wholesale-cleared: the two states it exists for — rogue stealth and party
// range fluctuation — clear the *entire* aura array at once. If the unit shows
// even one live aura, the descriptor is authoritative, so a cache entry the
// descriptor lacks is a genuinely-gone aura we never saw removed (e.g. a
// no-duration debuff — Ghost/Spirit — on a unit that died and resurrected out
// of our visibility, so no OnAuraRemoved fired and its infinite entry never
// expired). Surfacing it then produces a stuck phantom.
bool DescriptorHasVisibleAura(const uint8_t *unit) {
    if (unit == nullptr)
        return false;
    for (int slot = 0; slot < Offsets::UNIT_AURA_TOTAL; ++slot) {
        if (IsSlotPopulated(unit, slot))
            return true;
    }
    return false;
}

constexpr int kFallbackMax = Offsets::UNIT_AURA_TOTAL;

// Whether cache entry `c` may surface as a fallback for `unit` under `filter`:
// the spell must be a user-visible aura and must not already be in a populated
// descriptor slot (no double-listing the same live aura). The caller's
// caster/dispel/CC predicates are applied separately by `CachedMatches`.
// Superseded / dispelled auras are kept out of the cache by `Aura::Source`'s
// removal eviction, not filtered here.
bool FallbackEligible(const uint8_t *unit, const Aura::Source::CachedAura &c,
                      Filter filter) {
    if (!IsVisible(SpellRecord(c.spellId)))
        return false; // hidden/internal aura (e.g. SPELL_ATTR_HIDDEN_CLIENTSIDE):
                      // the descriptor path drops these via IsSlotPopulated, so
                      // the cache path must apply the same engine predicate or
                      // they leak through as tooltip-less phantom buffs.
    if (FindSlotBySpellID(unit, c.spellId, &filter, Match{}) >= 0)
        return false; // still in the descriptor — returned via the slot path
    return true;
}

// Applies `match` to a cache entry. The entry carries its caster, so the caster
// test is a plain GUID compare — no scan, nothing to defer.
bool CachedMatches(const Aura::Source::CachedAura &c, const Match &match) {
    return MatchesAura(match, c.casterGuid == Unit::Identity::PlayerGuid(),
                       c.spellId);
}

// The eligible cache-fallback entries for `unit` under `filter`, in `Enumerate`
// order, unfiltered by any `Match` (callers apply `CachedMatches`). Applies the
// fallback gates: reconcile the cache against the descriptor, then yield nothing
// while the descriptor shows any visible aura (it is authoritative then — see
// DescriptorHasVisibleAura). `buf` must hold kFallbackMax entries. Returns the
// count. An entry's ordinal here is what an opaque cache slot id encodes, which
// is why the sequence must not depend on the caller's match.
int EligibleFallbacks(const uint8_t *unit, Filter filter,
                      Aura::Source::CachedAura *buf) {
    if (unit == nullptr)
        return 0;
    ReconcileCache(unit); // drop entries the (synced) descriptor contradicts
    if (DescriptorHasVisibleAura(unit))
        return 0; // descriptor authoritative — see DescriptorHasVisibleAura
    const uint64_t guid = UnitGuid(unit);
    if (guid == 0)
        return 0;
    Aura::Source::CachedAura raw[kFallbackMax];
    const int n = Aura::Source::Enumerate(
        guid, filter == Filter::Harmful, raw, kFallbackMax);
    int m = 0;
    for (int i = 0; i < n; ++i) {
        if (FallbackEligible(unit, raw[i], filter))
            buf[m++] = raw[i];
    }
    return m;
}

} // namespace

bool PushNthCacheFallback(void *L, const uint8_t *unit, int oneBasedIndex,
                          Filter filter, Match match, Emit emit) {
    // The fallback yields only while the descriptor shows no visible aura, so
    // the descriptor held none of the caller's matches: the cache supplies index
    // `oneBasedIndex` (1-based) of the entries passing `match` directly.
    Aura::Source::CachedAura buf[kFallbackMax];
    const int n = EligibleFallbacks(unit, filter, buf);
    int seen = 0;
    for (int i = 0; i < n; ++i) {
        if (!CachedMatches(buf[i], match))
            continue;
        if (++seen == oneBasedIndex) {
            PushFromCache(L, unit, buf[i], filter == Filter::Helpful, emit);
            return true;
        }
    }
    return false;
}

void AppendCacheFallbacks(void *L, const uint8_t *unit, Filter filter,
                          Match match, int outerIdx, int &nextKey) {
    Aura::Source::CachedAura buf[kFallbackMax];
    const int n = EligibleFallbacks(unit, filter, buf);
    for (int i = 0; i < n; ++i) {
        if (!CachedMatches(buf[i], match))
            continue;
        Game::Lua::PushNumber(L, static_cast<double>(nextKey++));
        PushFromCache(L, unit, buf[i], filter == Filter::Helpful);
        Game::Lua::SetTable(L, outerIdx);
    }
}

// ── Out-of-range groupmate path ────────────────────────────────────────────

namespace {

// Absolute slot range [start, end) for a filter in the GROUP ARRAY (helpful
// 0..31, harmful 32..47). The array carries spell IDs only — no flag nibble —
// so the seating range is the only polarity the out-of-range path has. (The
// descriptor path reads the nibble instead; see SlotInFilterOrder.)
void GroupRange(Filter filter, int &start, int &end) {
    start = (filter == Filter::Harmful) ? Offsets::UNIT_AURA_BUFF_COUNT : 0;
    end = (filter == Filter::Harmful) ? Offsets::UNIT_AURA_TOTAL
                                      : Offsets::UNIT_AURA_BUFF_COUNT;
}

// Spell ID at group-array slot (0..47), 0 if `arr` null / OOB / empty.
uint32_t GroupSpellID(const uint16_t *arr, int slot) {
    if (arr == nullptr || slot < 0 || slot >= Offsets::UNIT_AURA_TOTAL)
        return 0;
    return arr[slot];
}

// Whether the group-array slot holds a surfacable aura. Mirrors the engine's
// per-slot test in Script_UnitBuff/UnitDebuff — a non-zero spell ID with a real
// Spell.dbc record that passes the visibility predicate (the server only
// transmits *applied* auras, so there is no flag nibble to check).
bool GroupSlotVisible(const uint16_t *arr, int slot) {
    const uint32_t id = GroupSpellID(arr, slot);
    return id != 0 && IsVisible(SpellRecord(id));
}

// `GroupSlotVisible` plus the caller's `match` predicates. The caster answer
// (`GroupIsPlayerCast` — a cache scan; a miss counts as not-player, since the
// array alone can't confirm the caster) is computed only when `match` restricts
// on caster.
bool GroupSlotMatches(uint64_t guid, const uint16_t *arr, int slot,
                      const Match &match) {
    if (!GroupSlotVisible(arr, slot))
        return false;
    const uint32_t id = GroupSpellID(arr, slot);
    const bool castByPlayer =
        match.caster != CasterMode::Any && GroupIsPlayerCast(guid, id);
    return MatchesAura(match, castByPlayer, id);
}

// The groupmate's level, for Spell.dbc duration scaling (0 = skip). Cheap
// roster lookup; only hit on the out-of-range path.
int GroupMemberLevel(uint64_t guid) {
    return Group::MemberStats::Lookup(guid).level;
}

// True if the group array exposes ANY visible aura (either kind). When it
// does, the server's out-of-range aura data is present and authoritative, so
// we do NOT supplement from the Aura::Source cache — mirroring how the
// descriptor path treats a populated descriptor (DescriptorHasVisibleAura).
// Only when the array is entirely empty do we fall back to the cache: the
// server resends out-of-range stats as deltas only, so a member seen in range
// and then moved away again leaves an empty party-stats aura array (the array
// is cleared when they become visible and never re-sent), yet their auras
// still sit in the cache from when we observed them in range.
bool GroupArrayHasVisibleAura(const uint16_t *arr) {
    for (int slot = 0; slot < Offsets::UNIT_AURA_TOTAL; ++slot) {
        if (GroupSlotVisible(arr, slot))
            return true;
    }
    return false;
}

// Whether cache entry `c` may supplement the (empty) group array: a visible
// aura not already present in the array. The caller's `match` is applied
// separately by `CachedMatches`. See EligibleGroupFallbacks for when this path
// runs.
bool GroupFallbackEligible(const uint16_t *arr,
                           const Aura::Source::CachedAura &c) {
    if (!IsVisible(SpellRecord(c.spellId)))
        return false;
    for (int slot = 0; slot < Offsets::UNIT_AURA_TOTAL; ++slot)
        if (GroupSpellID(arr, slot) == c.spellId)
            return false;
    return true;
}

// The eligible group cache-fallback entries for out-of-range member `guid`, in
// `Enumerate` order, unfiltered by any `Match` (callers apply `CachedMatches`).
// Yields nothing while the group array exposes ANY visible aura: the server's
// out-of-range aura data is then present and authoritative, mirroring how the
// descriptor path treats a populated descriptor (DescriptorHasVisibleAura).
// Only an entirely empty array falls back to the cache — the server resends
// out-of-range stats as deltas only, so a member seen in range and then moved
// away again leaves an empty party-stats aura array (the array is cleared when
// they become visible and never re-sent), yet their auras still sit in the
// cache from when we observed them in range. `buf` must hold kFallbackMax
// entries. An entry's ordinal here is what an opaque group-cache slot id
// encodes, which is why the sequence must not depend on the caller's match.
int EligibleGroupFallbacks(const uint16_t *arr, uint64_t guid, bool harmful,
                           Aura::Source::CachedAura *buf) {
    if (arr == nullptr || GroupArrayHasVisibleAura(arr))
        return 0;
    Aura::Source::CachedAura raw[kFallbackMax];
    const int n = Aura::Source::Enumerate(guid, harmful, raw, kFallbackMax);
    int m = 0;
    for (int i = 0; i < n; ++i) {
        if (GroupFallbackEligible(arr, raw[i]))
            buf[m++] = raw[i];
    }
    return m;
}

// Nth (1-based) cache-fallback aura of `filter`'s kind for an out-of-range
// member whose group array is empty. Pushes the AuraData table on a hit.
bool PushNthGroupCacheFallback(void *L, uint64_t guid, const uint16_t *arr,
                               int oneBasedIndex, Filter filter, Match match,
                               Emit emit = Emit::Table) {
    Aura::Source::CachedAura buf[kFallbackMax];
    const int n =
        EligibleGroupFallbacks(arr, guid, filter == Filter::Harmful, buf);
    int seen = 0;
    for (int i = 0; i < n; ++i) {
        if (!CachedMatches(buf[i], match))
            continue;
        if (++seen == oneBasedIndex) {
            const Attribution a = CachedAttribution(buf[i]);
            PushEnriched(L, guid, buf[i].spellId, filter == Filter::Helpful, 1,
                         GroupMemberLevel(guid), false, buf[i].slot, &a, emit);
            return true;
        }
    }
    return false;
}

// Append every cache-fallback aura of `filter`'s kind into the array table.
void AppendGroupCacheFallbacks(void *L, uint64_t guid, const uint16_t *arr,
                               Filter filter, Match match, int outerIdx,
                               int &nextKey) {
    Aura::Source::CachedAura buf[kFallbackMax];
    const int n =
        EligibleGroupFallbacks(arr, guid, filter == Filter::Harmful, buf);
    const int level = GroupMemberLevel(guid);
    for (int i = 0; i < n; ++i) {
        if (!CachedMatches(buf[i], match))
            continue;
        Game::Lua::PushNumber(L, static_cast<double>(nextKey++));
        const Attribution a = CachedAttribution(buf[i]);
        PushEnriched(L, guid, buf[i].spellId, filter == Filter::Helpful, 1, level,
                     false, buf[i].slot, &a);
        Game::Lua::SetTable(L, outerIdx);
    }
}

// Single cache-fallback aura matching `spellID` (when nonzero) or `spellName`
// (when non-null), of the given kind. Backs the by-ID / by-name group queries.
bool PushGroupCacheFallbackMatch(void *L, uint64_t guid, const uint16_t *arr,
                                 bool harmful, uint32_t spellID,
                                 const char *spellName, Match match) {
    Aura::Source::CachedAura buf[kFallbackMax];
    const int n = EligibleGroupFallbacks(arr, guid, harmful, buf);
    for (int i = 0; i < n; ++i) {
        if (spellID != 0 && buf[i].spellId != spellID)
            continue;
        if (spellName != nullptr) {
            const char *nm = LocalizedSpellName(SpellRecord(buf[i].spellId));
            if (nm == nullptr || std::strcmp(nm, spellName) != 0)
                continue;
        }
        if (!CachedMatches(buf[i], match))
            continue;
        const Attribution a = CachedAttribution(buf[i]);
        PushEnriched(L, guid, buf[i].spellId, !harmful, 1, GroupMemberLevel(guid),
                     false, buf[i].slot, &a);
        return true;
    }
    return false;
}

} // namespace

bool PushNthGroupAura(void *L, uint64_t guid, int oneBasedIndex, Filter filter,
                      Match match, Emit emit) {
    if (guid == 0 || oneBasedIndex < 1)
        return false;
    const uint16_t *arr = Group::MemberStats::AuraArray(guid);
    if (arr == nullptr)
        return false;
    Aura::Source::ObserveGroupAuras(guid, arr);
    int start, end;
    GroupRange(filter, start, end);
    int matches = 0;
    for (int slot = start; slot < end; ++slot) {
        if (!GroupSlotMatches(guid, arr, slot, match))
            continue;
        if (++matches == oneBasedIndex) {
            PushEnriched(L, guid, GroupSpellID(arr, slot),
                         filter == Filter::Helpful, 1, GroupMemberLevel(guid),
                         false, Aura::Source::SLOT_UNBOUND, /*known*/ nullptr,
                         emit);
            return true;
        }
    }
    // Empty array (server delta-resend gap): supplement from the cache.
    return PushNthGroupCacheFallback(L, guid, arr, oneBasedIndex, filter, match,
                                     emit);
}

bool PushGroupAuraBySpellID(void *L, uint64_t guid, uint32_t spellID,
                            const Filter *filter, Match match) {
    if (guid == 0 || spellID == 0)
        return false;
    const uint16_t *arr = Group::MemberStats::AuraArray(guid);
    if (arr == nullptr)
        return false;
    Aura::Source::ObserveGroupAuras(guid, arr);
    const int start = (filter != nullptr && *filter == Filter::Harmful)
                          ? Offsets::UNIT_AURA_BUFF_COUNT
                          : 0;
    const int end = (filter != nullptr && *filter == Filter::Helpful)
                        ? Offsets::UNIT_AURA_BUFF_COUNT
                        : Offsets::UNIT_AURA_TOTAL;
    for (int slot = start; slot < end; ++slot) {
        if (GroupSpellID(arr, slot) != spellID)
            continue;
        if (!GroupSlotMatches(guid, arr, slot, match))
            continue;
        PushEnriched(L, guid, spellID, slot < Offsets::UNIT_AURA_BUFF_COUNT, 1,
                     GroupMemberLevel(guid), false, Aura::Source::SLOT_UNBOUND);
        return true;
    }
    // Empty array (server delta-resend gap): supplement from the cache.
    const bool both = (filter == nullptr);
    if ((both || *filter == Filter::Helpful) &&
        PushGroupCacheFallbackMatch(L, guid, arr, false, spellID, nullptr, match))
        return true;
    if ((both || *filter == Filter::Harmful) &&
        PushGroupCacheFallbackMatch(L, guid, arr, true, spellID, nullptr, match))
        return true;
    return false;
}

bool PushGroupAuraBySpellName(void *L, uint64_t guid, const char *spellName,
                              const Filter *filter, Match match) {
    if (guid == 0 || spellName == nullptr || *spellName == '\0')
        return false;
    const uint16_t *arr = Group::MemberStats::AuraArray(guid);
    if (arr == nullptr)
        return false;
    Aura::Source::ObserveGroupAuras(guid, arr);
    const int start = (filter != nullptr && *filter == Filter::Harmful)
                          ? Offsets::UNIT_AURA_BUFF_COUNT
                          : 0;
    const int end = (filter != nullptr && *filter == Filter::Helpful)
                        ? Offsets::UNIT_AURA_BUFF_COUNT
                        : Offsets::UNIT_AURA_TOTAL;
    for (int slot = start; slot < end; ++slot) {
        const uint32_t id = GroupSpellID(arr, slot);
        if (id == 0)
            continue;
        const char *name = LocalizedSpellName(SpellRecord(id));
        if (name == nullptr || std::strcmp(name, spellName) != 0)
            continue;
        if (!GroupSlotMatches(guid, arr, slot, match))
            continue;
        PushEnriched(L, guid, id, slot < Offsets::UNIT_AURA_BUFF_COUNT, 1,
                     GroupMemberLevel(guid), false, Aura::Source::SLOT_UNBOUND);
        return true;
    }
    // Empty array (server delta-resend gap): supplement from the cache.
    const bool both = (filter == nullptr);
    if ((both || *filter == Filter::Helpful) &&
        PushGroupCacheFallbackMatch(L, guid, arr, false, 0, spellName, match))
        return true;
    if ((both || *filter == Filter::Harmful) &&
        PushGroupCacheFallbackMatch(L, guid, arr, true, 0, spellName, match))
        return true;
    return false;
}

void AppendGroupAuras(void *L, uint64_t guid, Filter filter, Match match,
                      int outerIdx, int &nextKey) {
    if (guid == 0)
        return;
    const uint16_t *arr = Group::MemberStats::AuraArray(guid);
    if (arr == nullptr)
        return;
    Aura::Source::ObserveGroupAuras(guid, arr);
    int start, end;
    GroupRange(filter, start, end);
    const int level = GroupMemberLevel(guid);
    for (int slot = start; slot < end; ++slot) {
        if (!GroupSlotMatches(guid, arr, slot, match))
            continue;
        Game::Lua::PushNumber(L, static_cast<double>(nextKey++));
        PushEnriched(L, guid, GroupSpellID(arr, slot),
                     filter == Filter::Helpful, 1, level, false,
                     Aura::Source::SLOT_UNBOUND);
        Game::Lua::SetTable(L, outerIdx);
    }
    // Empty array (server delta-resend gap): supplement from the cache.
    AppendGroupCacheFallbacks(L, guid, arr, filter, match, outerIdx, nextKey);
}

// ── Opaque aura slots ──────────────────────────────────────────────────────

int CollectSlots(const uint8_t *unit, uint64_t guid, Filter filter, Match match,
                 int *out, int cap) {
    int n = 0;
    if (unit != nullptr) {
        for (int i = 0; i < Offsets::UNIT_AURA_TOTAL && n < cap; ++i) {
            const int slot = SlotInFilterOrder(filter, i);
            if (SlotMatchesFilter(unit, slot, filter, match))
                out[n++] = slot;
        }
        // The cache fallback yields only while the descriptor shows no visible
        // aura, so these never interleave with live slots — they follow them,
        // exactly as the by-index getters order them.
        Aura::Source::CachedAura buf[kFallbackMax];
        const int m = EligibleFallbacks(unit, filter, buf);
        const int base = (filter == Filter::Harmful) ? OPAQUE_CACHE_HARMFUL
                                                     : OPAQUE_CACHE_HELPFUL;
        for (int k = 0; k < m && n < cap; ++k) {
            if (CachedMatches(buf[k], match))
                out[n++] = base + k;
        }
        return n;
    }
    // No live CGUnit — the out-of-range group path.
    if (guid == 0)
        return 0;
    const uint16_t *arr = Group::MemberStats::AuraArray(guid);
    if (arr == nullptr)
        return 0;
    Aura::Source::ObserveGroupAuras(guid, arr);
    int start, end;
    GroupRange(filter, start, end);
    for (int slot = start; slot < end && n < cap; ++slot) {
        if (GroupSlotMatches(guid, arr, slot, match))
            out[n++] = OPAQUE_GROUP + slot;
    }
    Aura::Source::CachedAura buf[kFallbackMax];
    const int m = EligibleGroupFallbacks(arr, guid, filter == Filter::Harmful, buf);
    const int base = (filter == Filter::Harmful) ? OPAQUE_GROUP_CACHE_HARMFUL
                                                 : OPAQUE_GROUP_CACHE_HELPFUL;
    for (int k = 0; k < m && n < cap; ++k) {
        if (CachedMatches(buf[k], match))
            out[n++] = base + k;
    }
    return n;
}

bool PushBySlot(void *L, const uint8_t *unit, uint64_t guid, int slot, Emit emit) {
    if (slot < 0)
        return false;
    const int kind = slot / OPAQUE_STRIDE;
    const int k = slot % OPAQUE_STRIDE;
    switch (kind) {
    case 0: // live descriptor slot
        if (unit == nullptr || !IsSlotPopulated(unit, k))
            return false;
        Push(L, unit, k, emit);
        return true;
    case 1:   // helpful cache fallback
    case 2: { // harmful cache fallback
        if (unit == nullptr || k >= kFallbackMax)
            return false;
        const Filter filter = (kind == 2) ? Filter::Harmful : Filter::Helpful;
        Aura::Source::CachedAura buf[kFallbackMax];
        if (k >= EligibleFallbacks(unit, filter, buf))
            return false;
        PushFromCache(L, unit, buf[k], filter == Filter::Helpful, emit);
        return true;
    }
    case 3: { // out-of-range group-array slot
        if (guid == 0 || k >= Offsets::UNIT_AURA_TOTAL)
            return false;
        const uint16_t *arr = Group::MemberStats::AuraArray(guid);
        if (arr == nullptr)
            return false;
        Aura::Source::ObserveGroupAuras(guid, arr);
        if (!GroupSlotVisible(arr, k))
            return false;
        PushEnriched(L, guid, GroupSpellID(arr, k),
                     k < Offsets::UNIT_AURA_BUFF_COUNT, 1, GroupMemberLevel(guid),
                     false, Aura::Source::SLOT_UNBOUND, /*known*/ nullptr, emit);
        return true;
    }
    case 4:   // helpful group cache fallback
    case 5: { // harmful group cache fallback
        if (guid == 0 || k >= kFallbackMax)
            return false;
        const uint16_t *arr = Group::MemberStats::AuraArray(guid);
        if (arr == nullptr)
            return false;
        const bool harmful = (kind == 5);
        Aura::Source::CachedAura buf[kFallbackMax];
        if (k >= EligibleGroupFallbacks(arr, guid, harmful, buf))
            return false;
        const Attribution a = CachedAttribution(buf[k]);
        PushEnriched(L, guid, buf[k].spellId, !harmful, 1, GroupMemberLevel(guid),
                     false, buf[k].slot, &a, emit);
        return true;
    }
    default:
        return false;
    }
}

} // namespace Aura::Data
