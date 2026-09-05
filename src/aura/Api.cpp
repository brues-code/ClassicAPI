// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// `C_UnitAuras.*` — modern aura-data namespace. Returns AuraData
// tables built by `Aura::Data::Push` (see `Data.h`). The fields the
// vanilla descriptor exposes (`name`, `icon`, `applications`,
// `spellId`, `dispelName`, `isHelpful`, `isHarmful`, `timeMod`)
// match modern; the fields that depend on systems vanilla doesn't
// have (`duration`, `expirationTime`, `auraInstanceID`, charges,
// nameplate flags, etc.) are populated with vanilla-truthful
// defaults so consumers reading those keys get sensible values.
//
// Filter parsing (`ParseFilters`) tokenizes the modern AuraFilters string:
// tokens separated by `|` and/or whitespace, each optionally negated with a
// leading `!`. Whole-token matching, so `RAID_PLAYER_DISPELLABLE` is not
// mistaken for `PLAYER` and `!PLAYER` is a negation rather than a match.
// Honored: `HELPFUL` / `HARMFUL` (the aura's real polarity, read however this
// server encodes it — see `Aura::Data::IsSlotHarmful`), `PLAYER` /
// `!PLAYER` (caster == / != the local player, from the Aura::Source cache),
// `DISPELLABLE` / `!DISPELLABLE` (dispel type is / isn't one a
// dispel/purge/steal can remove — Spell.dbc Dispel ∈ Magic/Curse/Disease/
// Poison, matching the server's DISPEL_ALL_MASK), and `CROWD_CONTROL` /
// `!CROWD_CONTROL` (is / isn't a hard control effect, via the shared
// `Spell::CrowdControl` classifier that also backs C_LossOfControl). Every
// other modern token (`RAID`, `CANCELABLE`, `INCLUDE_NAME_PLATE_ONLY`, `MAW`,
// …) is accepted and ignored — they need a class-dispel matrix or systems
// vanilla has no data for.

#include "Data.h"

#include "Game.h"
#include "Offsets.h"
#include "ui/ColorData.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstring>

namespace Aura::Api {

namespace {

const uint8_t *ResolveUnit(const char *token) {
    if (token == nullptr)
        return nullptr;
    return static_cast<const uint8_t *>(Game::ResolveUnitToken(token));
}

// Resolves a token to its GUID for the out-of-range (no live CGUnit) path.
// Only valid to call AFTER `ResolveUnit(token)` returned null *without raising*:
// `ResolveUnit` runs the engine's token→GUID resolver first, so reaching a null
// return means the token was well-formed (the resolver never raised). This runs
// the same non-raising resolver and returns 0 for a token that maps to no
// rostered member — which the group-array push paths treat as "no auras".
uint64_t GuidForOutOfRange(const char *token) {
    if (token == nullptr)
        return 0;
    return Unit::Identity::GuidForToken(token);
}

// Parsed AuraFilters string. `helpful`/`harmful` record which range tokens
// were present (neither → both, matching modern's default); `caster` carries
// the PLAYER / !PLAYER restriction.
struct ParsedFilter {
    bool helpful = false;
    bool harmful = false;
    Data::CasterMode caster = Data::CasterMode::Any;
    Data::DispelMode dispel = Data::DispelMode::Any;
    Data::CcMode cc = Data::CcMode::Any;

    Data::Match ToMatch() const { return Data::Match{caster, dispel, cc}; }
};

// Reduces the parsed range tokens to the single `Data::Filter` the indexed /
// by-id / by-name getters take: Harmful when HARMFUL was given, else Helpful
// (the modern default). HELPFUL wins ties are irrelevant here — an indexed
// getter reads one range.
Data::Filter RangeFilter(const ParsedFilter &pf) {
    return pf.harmful && !pf.helpful ? Data::Filter::Harmful
                                     : Data::Filter::Helpful;
}

// Tokenizes the filter string. Case-sensitive (modern documents the tokens as
// upper-case constants). `HELPFUL`/`HARMFUL` are positive range selectors;
// negating them is meaningless for our slot-based ranges, so a leading `!` is
// ignored on those. `PLAYER` honors negation (`!PLAYER` → NotPlayer). Any
// unrecognized token is accepted and skipped.
ParsedFilter ParseFilters(const char *filter) {
    ParsedFilter out;
    if (filter == nullptr)
        return out;
    for (const char *p = filter; *p != '\0';) {
        while (*p == '|' || *p == ' ' || *p == '\t')
            ++p;
        if (*p == '\0')
            break;
        bool negate = false;
        if (*p == '!') {
            negate = true;
            ++p;
        }
        char tok[64];
        size_t n = 0;
        while (*p != '\0' && *p != '|' && *p != ' ' && *p != '\t') {
            if (n + 1 < sizeof(tok))
                tok[n++] = *p;
            ++p;
        }
        tok[n] = '\0';

        if (strcmp(tok, "HELPFUL") == 0)
            out.helpful = true;
        else if (strcmp(tok, "HARMFUL") == 0)
            out.harmful = true;
        else if (strcmp(tok, "PLAYER") == 0)
            out.caster = negate ? Data::CasterMode::NotPlayer
                                : Data::CasterMode::PlayerOnly;
        else if (strcmp(tok, "DISPELLABLE") == 0)
            out.dispel = negate ? Data::DispelMode::NotDispellable
                                : Data::DispelMode::DispellableOnly;
        else if (strcmp(tok, "CROWD_CONTROL") == 0)
            out.cc = negate ? Data::CcMode::NotCrowdControl
                            : Data::CcMode::CrowdControlOnly;
        // else: accepted and ignored (retail-only / unimplemented token).
    }
    return out;
}

const char *ArgUnit(void *L, int idx) {
    if (!Game::Lua::IsString(L, idx))
        return nullptr;
    return Game::Lua::ToString(L, idx);
}

int ArgInt(void *L, int idx) {
    if (!Game::Lua::IsNumber(L, idx))
        return 0;
    return static_cast<int>(Game::Lua::ToNumber(L, idx));
}

const char *ArgOptString(void *L, int idx) {
    if (!Game::Lua::IsString(L, idx))
        return nullptr;
    return Game::Lua::ToString(L, idx);
}

// Pushes `AuraData` for the n-th aura on `unit` matching `filter`,
// or nil if no such aura. Used by `GetAuraDataByIndex` and the
// filter-locked aliases.
int PushAuraByIndex(void *L, const char *unitToken, int index,
                    Data::Filter filter, Data::Match match = {}) {
    const uint8_t *unit = ResolveUnit(unitToken);
    if (unit != nullptr) {
        const int slot = Data::FindNthSlot(unit, index, filter, match);
        if (slot >= 0) {
            Data::Push(L, unit, slot);
            return 1;
        }
        // Descriptor exhausted — an aura the engine dropped from the slot array
        // (rogue stealth, nearby party range fluctuation) may still be live in
        // the Aura::Source cache. Surface it after the descriptor entries.
        if (Data::PushNthCacheFallback(L, unit, index, filter, match))
            return 1;
    } else {
        // No live CGUnit — an out-of-range / cross-map groupmate. The server
        // still sends their aura spell IDs via SMSG_PARTY_MEMBER_STATS; read
        // them the same way the built-in UnitBuff/UnitDebuff do.
        if (Data::PushNthGroupAura(L, GuidForOutOfRange(unitToken), index,
                                   filter, match))
            return 1;
    }
    Game::Lua::PushNil(L);
    return 1;
}

// Positional (multi-return) sibling of `PushAuraByIndex` for the
// `C_UnitAuras.UnitAura` family: pushes the Classic-Era `UnitAura` 15-tuple with
// NO table allocation (the zero-GC path). Returns 15 on a hit, or 1 (a single
// nil) on a miss. The bool-returning push helpers leave exactly the 15-tuple on a
// hit and nothing on a miss, so the miss `PushNil` + `return 1` needs no cleanup.
int PushUnitAuraPositional(void *L, const char *unitToken, int index,
                           Data::Filter filter, Data::Match match = {}) {
    const uint8_t *unit = ResolveUnit(unitToken);
    if (unit != nullptr) {
        const int slot = Data::FindNthSlot(unit, index, filter, match);
        if (slot >= 0) {
            Data::Push(L, unit, slot, Data::Emit::Positional);
            return 15;
        }
        if (Data::PushNthCacheFallback(L, unit, index, filter, match,
                                       Data::Emit::Positional))
            return 15;
    } else if (Data::PushNthGroupAura(L, GuidForOutOfRange(unitToken), index,
                                      filter, match, Data::Emit::Positional)) {
        return 15;
    }
    Game::Lua::PushNil(L);
    return 1;
}

int __fastcall Script_GetAuraDataByIndex(void *L) {
    const char *unit = ArgUnit(L, 1);
    const int index = ArgInt(L, 2);
    const char *filterStr = ArgOptString(L, 3);
    if (unit == nullptr || index < 1) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const ParsedFilter pf = ParseFilters(filterStr);
    return PushAuraByIndex(L, unit, index, RangeFilter(pf), pf.ToMatch());
}

int __fastcall Script_GetBuffDataByIndex(void *L) {
    const char *unit = ArgUnit(L, 1);
    const int index = ArgInt(L, 2);
    if (unit == nullptr || index < 1) {
        Game::Lua::PushNil(L);
        return 1;
    }
    return PushAuraByIndex(L, unit, index, Data::Filter::Helpful);
}

int __fastcall Script_GetDebuffDataByIndex(void *L) {
    const char *unit = ArgUnit(L, 1);
    const int index = ArgInt(L, 2);
    if (unit == nullptr || index < 1) {
        Game::Lua::PushNil(L);
        return 1;
    }
    return PushAuraByIndex(L, unit, index, Data::Filter::Harmful);
}

// Zero-allocation positional accessors — Classic-Era `UnitAura`/`UnitBuff`/
// `UnitDebuff` shape, namespaced under `C_UnitAuras` to avoid clashing with the
// native `UnitBuff`/`UnitDebuff` globals (which keep their vanilla short return).
// `UnitAura` reads the range from the filter (helpful by default); `UnitBuff` /
// `UnitDebuff` lock the range and still honor the filter's PLAYER/DISPELLABLE/
// CROWD_CONTROL predicates.
int __fastcall Script_UnitAura(void *L) {
    const char *unit = ArgUnit(L, 1);
    const int index = ArgInt(L, 2);
    const char *filterStr = ArgOptString(L, 3);
    if (unit == nullptr || index < 1) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const ParsedFilter pf = ParseFilters(filterStr);
    return PushUnitAuraPositional(L, unit, index, RangeFilter(pf), pf.ToMatch());
}

int __fastcall Script_UnitBuff(void *L) {
    const char *unit = ArgUnit(L, 1);
    const int index = ArgInt(L, 2);
    const char *filterStr = ArgOptString(L, 3);
    if (unit == nullptr || index < 1) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const ParsedFilter pf = ParseFilters(filterStr);
    return PushUnitAuraPositional(L, unit, index, Data::Filter::Helpful,
                                  pf.ToMatch());
}

int __fastcall Script_UnitDebuff(void *L) {
    const char *unit = ArgUnit(L, 1);
    const int index = ArgInt(L, 2);
    const char *filterStr = ArgOptString(L, 3);
    if (unit == nullptr || index < 1) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const ParsedFilter pf = ParseFilters(filterStr);
    return PushUnitAuraPositional(L, unit, index, Data::Filter::Harmful,
                                  pf.ToMatch());
}

int __fastcall Script_GetUnitAuraBySpellID(void *L) {
    const char *unitToken = ArgUnit(L, 1);
    const int spellID = ArgInt(L, 2);
    const char *filterStr = ArgOptString(L, 3);
    if (unitToken == nullptr || spellID <= 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const uint8_t *unit = ResolveUnit(unitToken);
    const ParsedFilter pf = ParseFilters(filterStr);
    Data::Filter f = RangeFilter(pf);
    const Data::Filter *fp = (pf.helpful || pf.harmful) ? &f : nullptr;
    if (unit != nullptr) {
        const int slot = Data::FindSlotBySpellID(
            unit, static_cast<uint32_t>(spellID), fp, pf.ToMatch());
        if (slot >= 0) {
            Data::Push(L, unit, slot);
            return 1;
        }
    } else if (Data::PushGroupAuraBySpellID(L, GuidForOutOfRange(unitToken),
                                            static_cast<uint32_t>(spellID), fp,
                                            pf.ToMatch())) {
        // Out-of-range groupmate — read from the group-member aura array.
        return 1;
    }
    Game::Lua::PushNil(L);
    return 1;
}

int __fastcall Script_GetAuraDataBySpellName(void *L) {
    const char *unitToken = ArgUnit(L, 1);
    const char *spellName = ArgUnit(L, 2);
    const char *filterStr = ArgOptString(L, 3);
    if (unitToken == nullptr || spellName == nullptr || *spellName == '\0') {
        Game::Lua::PushNil(L);
        return 1;
    }
    const uint8_t *unit = ResolveUnit(unitToken);
    const ParsedFilter pf = ParseFilters(filterStr);
    Data::Filter f = RangeFilter(pf);
    const Data::Filter *fp = (pf.helpful || pf.harmful) ? &f : nullptr;
    if (unit != nullptr) {
        const int slot = Data::FindSlotBySpellName(unit, spellName, fp, pf.ToMatch());
        if (slot >= 0) {
            Data::Push(L, unit, slot);
            return 1;
        }
    } else if (Data::PushGroupAuraBySpellName(L, GuidForOutOfRange(unitToken),
                                              spellName, fp, pf.ToMatch())) {
        // Out-of-range groupmate — read from the group-member aura array.
        return 1;
    }
    Game::Lua::PushNil(L);
    return 1;
}

int __fastcall Script_GetPlayerAuraBySpellID(void *L) {
    const int spellID = ArgInt(L, 1);
    if (spellID <= 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const uint8_t *unit = ResolveUnit("player");
    const int slot = Data::FindSlotBySpellID(unit, static_cast<uint32_t>(spellID), nullptr);
    if (slot < 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Data::Push(L, unit, slot);
    return 1;
}

// C_UnitAuras.GetAuraSlots(unit [, filter [, maxSlots [, continuationToken]]])
//   -> continuationToken, slot1, slot2, ...
//
// Enumerates the opaque slot ids of every aura on `unit` matching `filter`, in
// the order the by-index getters visit them, `maxSlots` at a time (nil / 0 =
// all). The first return is the token to pass back for the next batch, or nil
// when this batch reached the end — retail's batching contract, and what makes
// AuraUtil.ForEachAura linear: one enumeration per batch plus a direct by-slot
// fetch per aura, instead of a fresh from-slot-0 walk per index. The token is
// the 1-based ordinal to resume at; opaque to callers. Encoding of the slot ids
// is documented at `Data::OPAQUE_STRIDE`.
int __fastcall Script_GetAuraSlots(void *L) {
    const char *unitToken = ArgUnit(L, 1);
    const char *filterStr = ArgOptString(L, 2);
    const int maxSlots = ArgInt(L, 3);
    const int token = ArgInt(L, 4);
    // ClassicAPI extension: a table as the 5th argument selects FILL mode —
    // the slot ids are written into it (t[1..n], stale tail cleared, t.n = n)
    // and the call returns `continuationToken, n` instead of the vararg list.
    // Lua 5.0 builds an `arg` table for every vararg CALL, so a Lua helper
    // that receives the retail multi-return allocates one table per batch;
    // filling the caller's table here is what keeps a per-frame scan
    // allocation-free (AuraUtil and pfUI's ScanAuraSlots use this form).
    const bool fill = Game::Lua::Type(L, 5) == Game::Lua::TYPE_TABLE;

    int slots[Data::OPAQUE_SLOTS_MAX];
    int total = 0;
    if (unitToken != nullptr) {
        const uint8_t *unit = ResolveUnit(unitToken);
        const uint64_t guid = (unit == nullptr) ? GuidForOutOfRange(unitToken) : 0;
        const ParsedFilter pf = ParseFilters(filterStr);
        total = Data::CollectSlots(unit, guid, RangeFilter(pf), pf.ToMatch(), slots,
                                   Data::OPAQUE_SLOTS_MAX);
    }
    int start = (token > 0) ? token - 1 : 0;
    if (start > total)
        start = total;
    int n = total - start;
    if (maxSlots > 0 && n > maxSlots)
        n = maxSlots;
    const bool more = start + n < total;

    if (fill) {
        for (int i = 0; i < n; ++i) {
            Game::Lua::PushNumber(L, static_cast<double>(i + 1));
            Game::Lua::PushNumber(L, static_cast<double>(slots[start + i]));
            Game::Lua::RawSet(L, 5);
        }
        // Clear what a previous, longer fill left past n. The array is dense
        // by construction, so stop at the first hole.
        for (int k = n + 1;; ++k) {
            Game::Lua::PushNumber(L, static_cast<double>(k));
            Game::Lua::RawGet(L, 5);
            const bool hole = Game::Lua::Type(L, -1) == Game::Lua::TYPE_NIL;
            Game::Lua::SetTop(L, -2);
            if (hole)
                break;
            Game::Lua::PushNumber(L, static_cast<double>(k));
            Game::Lua::PushNil(L);
            Game::Lua::RawSet(L, 5);
        }
        Game::Lua::PushString(L, "n");
        Game::Lua::PushNumber(L, static_cast<double>(n));
        Game::Lua::RawSet(L, 5);
        Game::Lua::SetTop(L, 0);
        if (more)
            Game::Lua::PushNumber(L, static_cast<double>(start + n + 1));
        else
            Game::Lua::PushNil(L);
        Game::Lua::PushNumber(L, static_cast<double>(n));
        return 2;
    }

    // Vararg form. Everything below only pushes, so the args can go; a full
    // batch is up to OPAQUE_SLOTS_MAX + 1 values, past Lua's guaranteed C-call
    // headroom.
    Game::Lua::SetTop(L, 0);
    if (Game::Lua::CheckStack(L, n + 1) == 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    if (more)
        Game::Lua::PushNumber(L, static_cast<double>(start + n + 1));
    else
        Game::Lua::PushNil(L);
    for (int i = 0; i < n; ++i)
        Game::Lua::PushNumber(L, static_cast<double>(slots[start + i]));
    return 1 + n;
}

// Shared body of GetAuraDataBySlot (table) / UnitAuraBySlot (positional):
// pushes the aura an opaque slot id from GetAuraSlots names, or a single nil
// for an id that no longer names one.
int PushAuraBySlot(void *L, Data::Emit emit) {
    const char *unitToken = ArgUnit(L, 1);
    if (unitToken == nullptr || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const int slot = ArgInt(L, 2);
    const uint8_t *unit = ResolveUnit(unitToken);
    const uint64_t guid = (unit == nullptr) ? GuidForOutOfRange(unitToken) : 0;
    if (Data::PushBySlot(L, unit, guid, slot, emit))
        return (emit == Data::Emit::Positional) ? 15 : 1;
    Game::Lua::PushNil(L);
    return 1;
}

// C_UnitAuras.GetAuraDataBySlot(unit, slot) -> AuraData | nil
int __fastcall Script_GetAuraDataBySlot(void *L) {
    return PushAuraBySlot(L, Data::Emit::Table);
}

// C_UnitAuras.UnitAuraBySlot(unit, slot) -> the 15 positional UnitAura values | nil
// Zero-allocation sibling of GetAuraDataBySlot, namespaced under C_UnitAuras
// like C_UnitAuras.UnitAura. What AuraUtil's non-packed iteration reads.
int __fastcall Script_UnitAuraBySlot(void *L) {
    return PushAuraBySlot(L, Data::Emit::Positional);
}

// Iterates the auras of one polarity (in `SlotInFilterOrder`, the same order
// the by-index getters use) and pushes AuraData tables into `outer` at
// sequential keys starting from `nextKey`. Updates `nextKey` so a follow-up
// call can append to the same outer table.
void AppendRangeToArray(void *L, const uint8_t *unit, int outerIdx,
                       Data::Filter filter, int &nextKey, Data::Match match) {
    for (int i = 0; i < Offsets::UNIT_AURA_TOTAL; ++i) {
        const int slot = Data::SlotInFilterOrder(filter, i);
        if (!Data::SlotMatchesFilter(unit, slot, filter, match))
            continue;
        Game::Lua::PushNumber(L, static_cast<double>(nextKey++));
        Data::Push(L, unit, slot);
        Game::Lua::SetTable(L, outerIdx);
    }
    // Append auras the descriptor dropped but Aura::Source still has live.
    Data::AppendCacheFallbacks(L, unit, filter, match, outerIdx, nextKey);
}

int __fastcall Script_GetUnitAuras(void *L) {
    const char *unitToken = ArgUnit(L, 1);
    const char *filterStr = ArgOptString(L, 2);
    const uint8_t *unit = ResolveUnit(unitToken);

    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    // Range tokens are independent of PLAYER: an explicit HELPFUL/HARMFUL
    // selects that range, neither selects both. So `"PLAYER"` alone returns
    // both ranges restricted to player-cast auras (matching retail).
    const ParsedFilter pf = ParseFilters(filterStr);
    const bool both = !pf.helpful && !pf.harmful;

    int nextKey = 1;
    if (unit != nullptr) {
        if (pf.helpful || both)
            AppendRangeToArray(L, unit, 1, Data::Filter::Helpful, nextKey, pf.ToMatch());
        if (pf.harmful || both)
            AppendRangeToArray(L, unit, 1, Data::Filter::Harmful, nextKey, pf.ToMatch());
    } else {
        // No live CGUnit — out-of-range / cross-map groupmate. Enumerate the
        // group-member aura array (spell IDs the server still transmits), the
        // same source the built-in UnitBuff/UnitDebuff read out of range.
        const uint64_t guid = GuidForOutOfRange(unitToken);
        if (pf.helpful || both)
            Data::AppendGroupAuras(L, guid, Data::Filter::Helpful, pf.ToMatch(), 1, nextKey);
        if (pf.harmful || both)
            Data::AppendGroupAuras(L, guid, Data::Filter::Harmful, pf.ToMatch(), 1, nextKey);
    }
    return 1;
}

// Pushes a plain `{r, g, b, a}` table decoded from the packed argb
// int in `ColorData.h`. Used only on the fallback path where
// `!!!ClassicAPI/Util/Color.lua` hasn't run — without `CreateColor`
// we can't build a real ColorMixin, but addons reading `.r/.g/.b/.a`
// still get the right numbers.
void PushPlainColorTable(void *L, int32_t argb) {
    const uint32_t v = static_cast<uint32_t>(argb);
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "r", ((v >> 16) & 0xFF) / 255.0);
    Game::Lua::SetFieldNumber(L, "g", ((v >>  8) & 0xFF) / 255.0);
    Game::Lua::SetFieldNumber(L, "b", ( v        & 0xFF) / 255.0);
    Game::Lua::SetFieldNumber(L, "a", ((v >> 24) & 0xFF) / 255.0);
}

bool PushColorByTag(void *L, const char *baseTag) {
    for (int i = 0; i < UI::ColorData::kColorCount; ++i) {
        if (strcmp(UI::ColorData::kColors[i].baseTag, baseTag) == 0) {
            PushPlainColorTable(L, UI::ColorData::kColors[i].argb);
            return true;
        }
    }
    return false;
}

// Mirrors modern retail's one-liner:
//     return _G["DEBUFF_TYPE_"..type:upper().."_COLOR"]
//            or DEBUFF_TYPE_NONE_COLOR
//
// `!!!ClassicAPI/Util/Color.lua` walks `C_UIColor.GetColors()` at
// addon load and publishes every entry as a ColorMixin under its
// `baseTag` global. By the time any addon calls us,
// `_G.DEBUFF_TYPE_MAGIC_COLOR` etc. are already ColorMixin
// instances — we push the same one, no rebuild, no duplicate
// source of truth. The Enrage row is a ClassicAPI extension in
// `ColorData.h` so it gets the same treatment.
//
// Fallback path: if `!!!ClassicAPI` isn't loaded (or hasn't reached
// `Color.lua` yet), the global lookup returns nil. We then read the
// raw argb out of `ColorData.h` and push a plain `{r,g,b,a}` table
// so consumers don't get a nil and crash on `:GetRGB()`. The plain
// table loses ColorMixin methods but preserves the field access most
// callers actually use.
int __fastcall Script_GetAuraDispelTypeColor(void *L) {
    const char *type = ArgOptString(L, 1);

    char tag[64];
    if (type != nullptr && type[0] != '\0') {
        std::memcpy(tag, "DEBUFF_TYPE_", 12);
        size_t off = 12;
        for (size_t i = 0; type[i] != '\0' && off < sizeof(tag) - 7;
             ++i, ++off) {
            const char c = type[i];
            tag[off] = (c >= 'a' && c <= 'z') ? c - 32 : c;
        }
        std::memcpy(tag + off, "_COLOR", 7); // includes the NUL
    } else {
        std::memcpy(tag, "DEBUFF_TYPE_NONE_COLOR", 23);
    }

    Game::Lua::SetTop(L, 0);
    Game::Lua::PushString(L, tag);
    Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE)
        return 1;

    Game::Lua::SetTop(L, 0);
    if (PushColorByTag(L, tag))
        return 1;
    PushColorByTag(L, "DEBUFF_TYPE_NONE_COLOR");
    return 1;
}

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_UnitAuras", "GetAuraDataByIndex",
                                     &Script_GetAuraDataByIndex);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "GetBuffDataByIndex",
                                     &Script_GetBuffDataByIndex);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "GetDebuffDataByIndex",
                                     &Script_GetDebuffDataByIndex);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "UnitAura",
                                     &Script_UnitAura);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "UnitBuff",
                                     &Script_UnitBuff);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "UnitDebuff",
                                     &Script_UnitDebuff);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "GetAuraSlots",
                                     &Script_GetAuraSlots);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "GetAuraDataBySlot",
                                     &Script_GetAuraDataBySlot);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "UnitAuraBySlot",
                                     &Script_UnitAuraBySlot);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "GetUnitAuraBySpellID",
                                     &Script_GetUnitAuraBySpellID);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "GetPlayerAuraBySpellID",
                                     &Script_GetPlayerAuraBySpellID);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "GetAuraDataBySpellName",
                                     &Script_GetAuraDataBySpellName);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "GetUnitAuras",
                                     &Script_GetUnitAuras);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "GetAuraDispelTypeColor",
                                     &Script_GetAuraDispelTypeColor);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Aura::Api
