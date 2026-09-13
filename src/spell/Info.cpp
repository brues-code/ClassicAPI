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

#include "Game.h"
#include "Offsets.h"
#include "spell/Arg.h"
#include "spell/Lookup.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Spell::Info {

// GetSpellInfo's isFunnel means a health-funnel spell — AttributesEx2 (+0x20)
// bit 11 = SPELL_ATTR_EX2_HEALTH_FUNNEL. Verified from Spell.dbc: Health
// Funnel (755) Ex2=0x808 and Hellfire (1949) Ex2=0x800 carry it, while
// channeled non-funnels (Drain Life, Mind Flay, Arcane Missiles, Rain of
// Fire) do not. An earlier version read AttributesEx bit 6, which is
// CHANNELED_2 — false for Health Funnel itself and true for unrelated
// channels, so it never actually detected funnel spells.
static constexpr uint32_t SPELL_ATTR_EX2_HEALTH_FUNNEL = 0x800;
// Spell.dbc effect-target arrays. Each spell has 3 effects, each
// with an implicit target A and an implicit target B (the latter
// often 0). Used by IsSpellHarmful / IsSpellHelpful — vanilla has
// no dedicated "positive"/"negative" attribute flag (AttributesEx
// bit 0x80 = NEGATIVE is sparsely set — most damage spells don't
// have it), so we classify by walking effect targets and checking
// whether any falls into a known hostile-target or friendly-target
// set. Same algorithm CMaNGOS uses in `SpellMgr::IsPositiveSpell`.
// Target IDs from vanilla 1.12's `SpellTarget` enum that mark a
// spell as hostile-targeted (covers single-target damage,
// debuffs, AoE damage, etc.). List is conservative — anything not
// in here defaults to non-hostile.
static bool IsHostileTarget(uint32_t t) {
    switch (t) {
        case 6:   // TARGET_CHAIN_DAMAGE (Fireball, Polymorph, …)
        case 15:  // TARGET_ALL_ENEMY_IN_AREA
        case 16:  // TARGET_ALL_ENEMY_IN_AREA_INSTANT
        case 25:  // TARGET_DUELVSPLAYER
        case 28:  // TARGET_ALL_ENEMY_IN_AREA_CHANNELED
        case 36:  // TARGET_ALL_HOSTILE_UNITS_AROUND_CASTER
        case 47:  // TARGET_SINGLE_ENEMY
        case 53:  // TARGET_LARGE_FRONTAL_CONE (hostile cone)
        case 54:  // TARGET_NARROW_FRONTAL_CONE
            return true;
        default:
            return false;
    }
}

// Friendly-targeted set (including self-cast). Same caveat — list
// is curated, false-negatives are possible for edge cases.
static bool IsFriendlyTarget(uint32_t t) {
    switch (t) {
        case 1:   // TARGET_SELF
        case 5:   // TARGET_PET
        case 20:  // TARGET_ALL_PARTY_AROUND_CASTER
        case 21:  // TARGET_SINGLE_FRIEND
        case 30:  // TARGET_ALL_FRIENDLY_UNITS_AROUND_CASTER
        case 31:  // TARGET_ALL_FRIENDLY_UNITS_IN_AREA
        case 33:  // TARGET_ALL_PARTY
        case 34:  // TARGET_ALL_PARTY_AROUND_CASTER_2
        case 35:  // TARGET_SINGLE_PARTY
        case 37:  // TARGET_AREAEFFECT_PARTY
        case 40:  // TARGET_CHAIN_HEAL
        case 45:  // TARGET_DYNAMIC_OBJECT_RIGHT_SIDE (friendly buffs use this)
        case 56:  // TARGET_RANDOM_NEARBY_LOC (totem placement)
        case 57:  // TARGET_RANDOM_CIRCUMFERENCE_POINT
        case 61:  // TARGET_RANDOM_FRIEND_AROUND_CASTER
        case 68:  // TARGET_NONCOMBAT_PET
        case 77:  // TARGET_SINGLE_FRIEND_2
        case 80:  // TARGET_AREAEFFECT_PARTY_AND_CLASS
            return true;
        default:
            return false;
    }
}

template <typename T>
static T ReadGlobal(uintptr_t addr) {
    return *reinterpret_cast<T *>(addr);
}

// Sub-DBC lookup (SpellIcon, SpellCastTimes, SpellRange) — these are the
// "indirected fields" of a Spell.dbc record. Different base/count
// addresses than the main Spell.dbc, so they don't go through
// `Spell::Lookup::RecordForID`.
static const uint8_t *LookupSubRecord(uintptr_t baseAddr, uintptr_t countAddr, int id) {
    if (id <= 0)
        return nullptr;
    const int count = ReadGlobal<int>(countAddr);
    if (id > count)
        return nullptr;
    const uint8_t *const *records = ReadGlobal<const uint8_t *const *>(baseAddr);
    if (records == nullptr)
        return nullptr;
    return records[id];
}

struct SpellInfoData {
    int spellID;
    const char *name;       // localized; nullable if record's locale slot is unset
    const char *rank;       // localized; same nullability as name
    const char *iconPath;   // SpellIcon.dbc path; null if icon record missing
    int cost;               // base ManaCost (no per-level scaling)
    bool isFunnel;
    int powerType;          // 0=mana, 1=rage, 2=focus, 3=energy, 4=happiness
    int castTimeMs;         // base time from SpellCastTimes.dbc
    float minRange;
    float maxRange;
};

static bool ReadSpellInfo(int spellID, SpellInfoData &out) {
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return false;

    const int locale = ReadGlobal<int>(Offsets::VAR_LOCALE_INDEX);

    out.spellID = spellID;
    out.name = Game::Read<const char *>(record, Offsets::OFF_SPELL_NAMES + locale * 4);
    out.rank = Game::Read<const char *>(record, Offsets::OFF_SPELL_RECORD_RANK + locale * 4);

    out.iconPath = nullptr;
    const int iconID = Game::Read<int>(record, Offsets::OFF_SPELL_RECORD_ICON_ID);
    if (auto *iconRec = LookupSubRecord(Offsets::VAR_SPELL_ICON_RECORDS,
                                        Offsets::VAR_SPELL_ICON_COUNT, iconID)) {
        out.iconPath = Game::Read<const char *>(iconRec, Offsets::OFF_SPELLICON_PATH);
    }

    out.cost = Game::Read<int>(record, Offsets::OFF_SPELL_RECORD_MANA_COST);

    const uint32_t attrEx2 = Game::Read<uint32_t>(
        record, Offsets::OFF_SPELL_RECORD_ATTRIBUTES_EX2);
    out.isFunnel = (attrEx2 & SPELL_ATTR_EX2_HEALTH_FUNNEL) != 0;

    out.powerType = Game::Read<int>(record, Offsets::OFF_SPELL_RECORD_POWER_TYPE);

    out.castTimeMs = 0;
    const int castIndex = Game::Read<int>(record, Offsets::OFF_SPELL_RECORD_CASTING_TIME_INDEX);
    if (auto *castRec = LookupSubRecord(Offsets::VAR_SPELL_CAST_TIMES_RECORDS,
                                        Offsets::VAR_SPELL_CAST_TIMES_COUNT, castIndex)) {
        out.castTimeMs = *reinterpret_cast<const int *>(castRec + 4);
    }

    out.minRange = 0.0f;
    out.maxRange = 0.0f;
    const int rangeIndex = Game::Read<int>(record, Offsets::OFF_SPELL_RECORD_RANGE_INDEX);
    if (auto *rangeRec = LookupSubRecord(Offsets::VAR_SPELL_RANGE_RECORDS,
                                         Offsets::VAR_SPELL_RANGE_COUNT, rangeIndex)) {
        out.minRange = *reinterpret_cast<const float *>(rangeRec + 4);
        out.maxRange = *reinterpret_cast<const float *>(rangeRec + 8);
    }

    return true;
}

// Case-insensitive match against the literal string "pet". Anything else
// (including the explicit "spell") maps to the player spellbook —
// matches 1.12's `Script_GetSpellName` behavior, which `SStrCmpI`s the
// input against "pet" and treats every other value as the player book.
static bool BookTypeIsPet(const char *s) {
    if (s == nullptr)
        return false;
    auto lc = [](char c) -> char {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
    };
    return lc(s[0]) == 'p' && lc(s[1]) == 'e' && lc(s[2]) == 't' && s[3] == '\0';
}

// Parses a spellID out of a spell hyperlink ("…|Hspell:<id>[:…]…").
// Returns 0 when `s` isn't a spell link, so the caller can fall through
// to a plain-name lookup.
static int SpellIDFromLink(const char *s) {
    if (s == nullptr)
        return 0;
    const char *p = std::strstr(s, "Hspell:");
    if (p == nullptr)
        return 0;
    p += 7; // past "Hspell:"
    if (*p < '0' || *p > '9')
        return 0;
    int id = 0;
    while (*p >= '0' && *p <= '9')
        id = id * 10 + (*p++ - '0');
    return id;
}

// Resolves the Lua args to a spellID, supporting the full retail
// GetSpellInfo argument set:
//   GetSpellInfo(spellID)              -- arg1 number, arg2 non-string
//   GetSpellInfo(slot, "spell"|"pet")  -- arg1 number, arg2 string → book slot
//   GetSpellInfo("name")               -- arg1 string → spellbook name lookup
//   GetSpellInfo("…|Hspell:ID|h…")      -- arg1 string → link's embedded spellID
// A name resolves against the player's/pet's spellbook (retail's scope),
// returning the highest known rank; an unknown name yields 0. Returns 0
// for invalid/empty inputs (Lua side surfaces 0 as nil since spellID 0 is
// never valid). Only a non-string, non-number arg1 raises `lua_error` —
// a name retail doesn't recognize returns nil, it does not error.
static int ResolveLuaArgsToSpellID(void *L) {
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_STRING) {
        const char *s = Game::Lua::ToString(L, 1);
        const int fromLink = SpellIDFromLink(s);
        return fromLink > 0 ? fromLink : Spell::Lookup::SpellNameToID(s);
    }
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetSpellInfo(spellID | \"name\" | link) "
                            "or GetSpellInfo(slot, bookType)");
        return 0;
    }
    const int arg1 = static_cast<int>(Game::Lua::ToNumber(L, 1));

    if (Game::Lua::Type(L, 2) == Game::Lua::TYPE_STRING) {
        const char *book = Game::Lua::ToString(L, 2);
        const int bookType = static_cast<int>(BookTypeIsPet(book));
        return Spell::Lookup::SpellbookSlotToID(arg1, bookType);
    }
    return arg1;
}

static int __fastcall Script_GetSpellInfo(void *L) {
    const int spellID = ResolveLuaArgsToSpellID(L);
    if (spellID <= 0)
        return 0;

    SpellInfoData info;
    if (!ReadSpellInfo(spellID, info))
        return 0;

    // PushString tail-calls PushNil for null pointers, so unset locale
    // slots (rare, but possible) come through as nil rather than crash.
    Game::Lua::PushString(L, info.name);                         // 1. name
    Game::Lua::PushString(L, info.rank);                         // 2. rank
    Game::Lua::PushString(L, info.iconPath);                     // 3. icon (path string)
    Game::Lua::PushNumber(L, static_cast<double>(info.cost));    // 4. cost
    Game::Lua::PushBool(L, info.isFunnel);            // 5. isFunnel
    Game::Lua::PushNumber(L, static_cast<double>(info.powerType)); // 6. powerType
    Game::Lua::PushNumber(L, static_cast<double>(info.castTimeMs)); // 7. castTime (ms)
    Game::Lua::PushNumber(L, static_cast<double>(info.minRange)); // 8. minRange
    Game::Lua::PushNumber(L, static_cast<double>(info.maxRange)); // 9. maxRange
    Game::Lua::PushNumber(L, static_cast<double>(info.spellID));  // 10. spellID (NEW)
    return 10;
}

// Helpers for building the C_Spell.GetSpellInfo result table. Each
// expects the table at stack[-3] before the call (key + value get
// pushed onto top, then `RawSet(L, -3)` pops them and sets the field).
static void SetField(void *L, const char *key, const char *value) {
    Game::Lua::PushString(L, key);
    Game::Lua::PushString(L, value); // PushString handles NULL -> nil
    Game::Lua::RawSet(L, -3);
}
static void SetField(void *L, const char *key, double value) {
    Game::Lua::PushString(L, key);
    Game::Lua::PushNumber(L, value);
    Game::Lua::RawSet(L, -3);
}
static void SetFieldBool(void *L, const char *key, bool value) {
    Game::Lua::PushString(L, key);
    Game::Lua::PushBool(L, value);
    Game::Lua::RawSet(L, -3);
}

static int __fastcall Script_C_GetSpellInfo(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    SpellInfoData info;
    if (!ReadSpellInfo(spellID, info))
        return 0; // nil for unknown spellID

    Game::Lua::NewTable(L);
    SetField(L, "name", info.name);
    // `iconID` deviates from modern's fileID:number — vanilla has no
    // fileID system, so we surface the icon path string instead.
    // Practical: feed it directly to texture:SetTexture(...).
    SetField(L, "iconID", info.iconPath);
    SetField(L, "castTime", static_cast<double>(info.castTimeMs));
    SetField(L, "minRange", static_cast<double>(info.minRange));
    SetField(L, "maxRange", static_cast<double>(info.maxRange));
    SetField(L, "spellID", static_cast<double>(info.spellID));
    // Vanilla extras beyond the modern signature — present in 1.12's
    // Spell.dbc, no harm including them for addons backporting from
    // 3.3.5 where the same data was exposed positionally.
    SetField(L, "rank", info.rank);
    SetField(L, "cost", static_cast<double>(info.cost));
    SetFieldBool(L, "isFunnel", info.isFunnel);
    SetField(L, "powerType", static_cast<double>(info.powerType));
    return 1;
}

static int __fastcall Script_C_GetSpellName(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return 0; // nil for unknown spellID

    const int locale = ReadGlobal<int>(Offsets::VAR_LOCALE_INDEX);
    const char *name = Game::Read<const char *>(record, Offsets::OFF_SPELL_NAMES + locale * 4);
    if (name == nullptr || *name == '\0')
        return 0; // empty / no name in current locale → nil
    Game::Lua::PushString(L, name);
    return 1;
}

static int __fastcall Script_C_GetSpellTexture(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return 0;

    const int iconID = Game::Read<int>(record, Offsets::OFF_SPELL_RECORD_ICON_ID);
    auto *iconRec = LookupSubRecord(Offsets::VAR_SPELL_ICON_RECORDS,
                                    Offsets::VAR_SPELL_ICON_COUNT, iconID);
    if (iconRec == nullptr)
        return 0;
    const char *path = Game::Read<const char *>(iconRec, Offsets::OFF_SPELLICON_PATH);
    if (path == nullptr || *path == '\0')
        return 0;
    Game::Lua::PushString(L, path);
    return 1;
}

// Builds the spell hyperlink string `|cff71d5ff|Hspell:ID:0|h[Name]|h|r`
// into `out`. The trailing `:0` after the spellID matches modern's
// hyperlink format (the field's a sub-data slot used for pet-spellbook
// flags etc. in later expansions); 1.12 ignores it during link parsing
// but addons grepping with `|Hspell:(%d+):` patterns will pick it up.
//
// Returns false if the spellID is invalid, the locale-resolved name is
// missing, or the output buffer is too small (the caller's 256-byte
// stack buffer is comfortable for any vanilla spell name).
static bool BuildSpellLink(int spellID, char *out, size_t outLen) {
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return false;
    const int locale = ReadGlobal<int>(Offsets::VAR_LOCALE_INDEX);
    const char *name = Game::Read<const char *>(record, Offsets::OFF_SPELL_NAMES + locale * 4);
    if (name == nullptr || *name == '\0')
        return false;
    const int n = std::snprintf(out, outLen, "|cff71d5ff|Hspell:%d:0|h[%s]|h|r",
                                spellID, name);
    return n > 0 && static_cast<size_t>(n) < outLen;
}

// `GetSpellLink(spellID)` / `GetSpellLink(slot, bookType)` — same arg
// shape as `GetSpellInfo`. Returns `(linkString, spellID)`. The second
// return is what makes the spellbook overload useful: callers can pass
// in `(slot, "spell")` and get back both the link AND the resolved
// spellID without a separate lookup.
static int __fastcall Script_GetSpellLink(void *L) {
    const int spellID = ResolveLuaArgsToSpellID(L);
    if (spellID <= 0)
        return 0;

    char buf[256];
    if (!BuildSpellLink(spellID, buf, sizeof(buf)))
        return 0;

    Game::Lua::PushString(L, buf);
    Game::Lua::PushNumber(L, static_cast<double>(spellID));
    return 2;
}

// `C_Spell.GetSpellLink(spellID)` — table-namespace version. Returns
// only the link string (modern omits the spellID echo since the caller
// already had it on hand to call this).
static int __fastcall Script_C_GetSpellLink(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    if (spellID <= 0)
        return 0;

    char buf[256];
    if (!BuildSpellLink(spellID, buf, sizeof(buf)))
        return 0;

    Game::Lua::PushString(L, buf);
    return 1;
}

// `FindSpellBookSlotByID(spellID)` — inverse of `GetSpellName(slot,
// bookType)`. Searches the player spellbook first, then the pet
// spellbook, for a slot whose spellID matches. Returns
// `(slot, bookType)` so callers can feed both directly into the
// existing slot-and-bookType API surface (`GetSpellName`,
// `GetSpellTexture`, etc.). Returns nil if the spellID isn't currently
// in either book.
static int __fastcall Script_FindSpellBookSlotByID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: FindSpellBookSlotByID(spellID)");
        return 0;
    }
    const int spellID = static_cast<int>(Game::Lua::ToNumber(L, 1));

    int bookType = 0;
    const int slot = Spell::Lookup::FindSpellbookSlot(spellID, &bookType);
    if (slot == 0)
        return 0;

    Game::Lua::PushNumber(L, static_cast<double>(slot));
    Game::Lua::PushString(L, bookType == 1 ? "pet" : "spell");
    return 2;
}

// Shared back-end for both passive-spell query forms. Reads `Attributes`
// (+0x18) bit 6 (`SPELL_ATTR_PASSIVE`) off the Spell.dbc record and
// pushes the boolean. Returns 1 with a boolean on the Lua stack for a
// valid spellID, 0 (= nil to Lua) for an invalid one.
static int PushIsPassive(void *L, int spellID) {
    if (spellID <= 0)
        return 0;
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return 0;
    const uint32_t attr = Game::Read<uint32_t>(
        record, Offsets::OFF_SPELL_RECORD_ATTRIBUTES);
    Game::Lua::PushBool(L, (attr & Offsets::SPELL_ATTR_PASSIVE) != 0);
    return 1;
}

// `IsPassiveSpell(spellID)` / `IsPassiveSpell(slot, bookType)` — same
// arg shape as `GetSpellInfo`. The 3.0-era global form; takes either a
// spellID directly or a spellbook slot + bookType (`"spell"` / `"pet"`).
static int __fastcall Script_IsPassiveSpell(void *L) {
    return PushIsPassive(L, ResolveLuaArgsToSpellID(L));
}

// `C_Spell.IsSpellPassive(spellID)` — modern table-namespace form
// (10.0+; word order flipped from the older `IsPassiveSpell`).
// Takes a spellID only — `C_Spell.*` calls don't accept the spellbook
// slot+bookType shape.
static int __fastcall Script_C_IsSpellPassive(void *L) {
    return PushIsPassive(L, Spell::Arg::ResolveSpellID(L, 1));
}

// `IsPlayerSpell(spellID)` — returns true if the player currently
// "knows" the given spellID. Covers everything: trained class abilities,
// racials, talent passives, profession recipes (including ones learned
// from vendors / discovered tradeskill recipes), and anything else
// granted by SMSG_LEARNED_SPELL.
//
// Implementation: single bitmap lookup at `[VAR_PLAYER_SPELL_BITMAP]`.
// The engine maintains a dword bitmap of every spellID the player
// knows (one bit per spellID, indexed by `spellID`); learning/unlearning
// updates this bitmap. We just consult the same bit the engine itself
// reads via the helper at `0x0060C740` — no walks, no profession
// caching, no per-source data structure.
//
// Trade-offs vs. the older spellbook+talent walk implementation:
//   - Faster (one memory access vs hundreds of comparisons).
//   - Broader (covers profession recipes that are not in the spellbook
//     arrays and not in the talent tree).
//   - Same shape modern WoW (5.4.8+) uses for the same function — see
//     `[0x011C25D8]` in 5.4.8's `Wow.exe`.
// Shared read of the engine's known-spell bitmap (the same bit the helper
// at `0x0060C740` consults): true iff the local player knows `spellID`.
// Bounds-checked; false for out-of-range IDs or before the bitmap exists.
static bool PlayerKnowsSpell(int spellID) {
    if (spellID < 1)
        return false;
    const int spellCount = Game::Read<int>(
        static_cast<uintptr_t>(Offsets::VAR_SPELL_RECORD_COUNT));
    if (spellID > spellCount)
        return false;
    auto *bitmap = Game::Read<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PLAYER_SPELL_BITMAP));
    if (bitmap == nullptr)
        return false;
    const uint32_t mask = 1u << (spellID & 31);
    return (bitmap[spellID >> 5] & mask) != 0;
}

// Walks the known-spell bitmap and calls `fn(spellID, record)` for every
// set bit that has a Spell.dbc row, in ascending spellID order, until `fn`
// returns false. This is `IsPlayerSpell` turned around: only a set bit ever
// touches a record, so a full pass costs a few hundred lookups, not the
// ~28,000-row table. The bitmap covers spellIDs 0..spellCount inclusive,
// one bit each, so the last word is index spellCount >> 5; zero words — the
// vast majority — cost one compare. No-op before login, when the bitmap is
// not yet allocated.
template <typename F>
static void ForEachKnownSpell(F fn) {
    auto *bitmap = Game::Read<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PLAYER_SPELL_BITMAP));
    if (bitmap == nullptr)
        return;
    const int spellCount = Game::Read<int>(
        static_cast<uintptr_t>(Offsets::VAR_SPELL_RECORD_COUNT));
    for (int word = 0; word <= (spellCount >> 5); ++word) {
        const uint32_t bits = bitmap[word];
        if (bits == 0)
            continue;
        for (int bit = 0; bit < 32; ++bit) {
            if ((bits & (1u << bit)) == 0)
                continue;
            const int spellID = (word << 5) | bit;
            if (spellID < 1 || spellID > spellCount)
                continue;
            const uint8_t *record = Spell::Lookup::RecordForID(spellID);
            if (record == nullptr)
                continue;
            if (!fn(spellID, record))
                return;
        }
    }
}

// `C_SpellBook.GetPlayerSpellsByAura(auraName)` -> { spellID, ... }
//
// Every spell the player currently knows whose Spell.dbc record has an
// effect applying aura `auraName` (an EffectApplyAuraName code — 134 for
// MOD_MANA_REGEN_INTERRUPT, 217 for Turtle's MOD_ENERGY_REGEN_TIME, ...),
// as a 1-based ascending array of spell IDs.
//
// The known-spell bitmap is the right table to walk, because "which of MY
// spells apply this" is the question a caller summing aura amounts is
// actually asking. The bitmap holds only a talent's CURRENT rank, so such
// a sum never double-counts ranks.
//
// Known is not the same as active. For a passive it is — known means in
// effect, and a passive never appears in the buff list. A castable buff
// in this result is merely learned; whether it is up is a buff-list
// question, and a buff another player put on us is not in the bitmap at
// all. Callers split on `C_Spell.IsSpellPassive` accordingly.
//
// `auraName` 0 means "applies no aura" and would match most of what the
// player knows, so it and negatives return the empty array. Empty before
// login too.
static int __fastcall Script_GetPlayerSpellsByAura(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L,
            "Usage: C_SpellBook.GetPlayerSpellsByAura(auraName)");
        return 0;
    }
    const int auraName = static_cast<int>(Game::Lua::ToNumber(L, 1));

    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    if (auraName <= 0)
        return 1;

    int n = 0;
    ForEachKnownSpell([&](int spellID, const uint8_t *record) {
        auto *auras = reinterpret_cast<const int32_t *>(
            record + Offsets::OFF_SPELL_RECORD_EFFECT_APPLY_AURA_NAME);
        for (int e = 0; e < Offsets::SPELL_RECORD_EFFECT_COUNT; ++e) {
            if (auras[e] == auraName) {
                Game::Lua::PushNumber(L, static_cast<double>(++n));
                Game::Lua::PushNumber(L, static_cast<double>(spellID));
                Game::Lua::RawSet(L, -3);
                break; // one entry per spell, however many effects match
            }
        }
        return true;
    });
    return 1;
}

// `C_SpellBook.ContainsAnyDisenchantSpell()` -> bool. True iff the player
// knows a spell with SPELL_EFFECT_DISENCHANT. That is 13262 "Disenchant"
// (the only such row in Spell.dbc), but the test is data-driven like the
// retail original — "any disenchant spell" — so a custom variant a server
// adds counts too.
static int __fastcall Script_ContainsAnyDisenchantSpell(void *L) {
    bool found = false;
    ForEachKnownSpell([&](int, const uint8_t *record) {
        auto *effects = reinterpret_cast<const int32_t *>(
            record + Offsets::OFF_SPELL_RECORD_EFFECT);
        for (int e = 0; e < Offsets::SPELL_RECORD_EFFECT_COUNT; ++e) {
            if (effects[e] == Offsets::SPELL_EFFECT_DISENCHANT) {
                found = true;
                return false;
            }
        }
        return true;
    });
    Game::Lua::PushBool(L, found);
    return 1;
}

static int __fastcall Script_IsPlayerSpell(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: IsPlayerSpell(spellID)");
        return 0;
    }
    const int spellID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    Game::Lua::PushBoolean(L, PlayerKnowsSpell(spellID));
    return 1;
}

// `CanDualWield()` — true if the local player can equip a weapon in the off
// hand. The server tracks this as a plain bool (`Player::m_canDualWield`)
// flipped on only by `SPELL_EFFECT_DUAL_WIELD` (effect 40); no class has it
// innately. Verified against the client Spell.dbc: exactly ONE spell carries
// that effect — 674 "Dual Wield", the trained warrior/rogue/hunter passive —
// and it lands in the known-spell bitmap like any learned spell. So there's
// no separate client-visible capability flag to read; `IsPlayerSpell(674)`
// is the exact equivalent of the server's `CanDualWield()`.
static constexpr int kDualWieldSpellID = 674;

static int __fastcall Script_CanDualWield(void *L) {
    Game::Lua::PushBoolean(L, PlayerKnowsSpell(kDualWieldSpellID));
    return 1;
}

// `IsSpellKnown(spellID, [isPet])` — strict spellbook check, scoped to
// the player or pet spellbook depending on `isPet` (default false).
//
// Critically NOT the same as `IsPlayerSpell`: this is the narrower
// modern-semantics function. Returns true only for spells the player
// has as a usable spellbook entry (trained class abilities, active
// talent grants, racials that appear in the spellbook). Returns false
// for talent passives, profession recipes, and anything else that's
// "known" but not displayable as a spellbook button.
//
// Verified against 3.3.5's `Script_IsSpellKnown` at `0x0053C3A0`,
// whose inner function at `0x0053B4E0` walks the spellbook arrays
// (`[0x00BE6D88]` player / `[0x00BE7D98]` pet) with their respective
// counts — strict array scan, not a bitmap. Same shape applies in
// 1.12: the spellbook arrays at `VAR_PLAYER_SPELLBOOK` /
// `VAR_PET_SPELLBOOK` are the authoritative spellbook display data,
// so we walk those via the existing `Spell::Lookup::FindSpellbookSlot`
// and filter by the book type the caller asked for.
//
// Use `IsPlayerSpell(spellID)` for the broader "any kind of known"
// check (covers recipes, passives, etc.).
static int __fastcall Script_IsSpellKnown(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: IsSpellKnown(spellID, [isPet])");
        return 0;
    }
    const int spellID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (spellID < 1) {
        Game::Lua::PushBool(L, 0);
        return 1;
    }
    const int wantBookType = static_cast<int>(Game::Lua::ToBoolean(L, 2) != 0);

    int bookType = -1;
    const int slot = Spell::Lookup::FindSpellbookSlot(spellID, &bookType);
    const bool found = (slot > 0 && bookType == wantBookType);
    Game::Lua::PushBoolean(L, found);
    return 1;
}

// Walks the spell's 3 effects' implicit target IDs (both A and B
// slots) and returns true if `pred` matches any of them. Returns
// false for null records.
template <typename Pred>
static bool AnyEffectTarget(const uint8_t *record, Pred pred) {
    if (record == nullptr)
        return false;
    auto *targetsA = Game::Ptr<const uint32_t>(
        record, Offsets::OFF_SPELL_RECORD_EFFECT_IMPLICIT_TARGET_A);
    auto *targetsB = Game::Ptr<const uint32_t>(
        record, Offsets::OFF_SPELL_RECORD_EFFECT_IMPLICIT_TARGET_B);
    for (int i = 0; i < 3; ++i) {
        if (pred(targetsA[i]) || pred(targetsB[i]))
            return true;
    }
    return false;
}

// `IsSpellHarmful(spellID)` — true iff any effect of the spell
// targets a hostile-target type (chain damage, single enemy, AoE
// enemy, etc. — see `IsHostileTarget`). Same algorithm CMaNGOS uses
// to classify positive vs negative spells, since vanilla 1.12 has
// no dedicated "harmful" attribute bit (the `SPELL_ATTR_EX_NEGATIVE`
// flag is sparsely set — most damage spells don't have it).
static bool ComputeIsHarmful(int spellID) {
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    return AnyEffectTarget(record, &IsHostileTarget);
}

// `IsSpellHelpful(spellID)` — true iff any effect targets a
// friendly-target type (self, party, single friend, chain heal,
// etc.). Disjoint from harmful in practice for vanilla spells —
// most spells are clearly one or the other — but they aren't
// strict inverses: a few utility/geometry-targeted spells (script-
// driven, totem placement, etc.) return false for both.
static bool ComputeIsHelpful(int spellID) {
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    return AnyEffectTarget(record, &IsFriendlyTarget);
}

// `IsHarmfulSpell(spellID)` / `IsHarmfulSpell(slot, bookType)` —
// modern global. Accepts either form via the same
// `ResolveLuaArgsToSpellID` path `GetSpellInfo` uses.
static int __fastcall Script_IsHarmfulSpell(void *L) {
    const int spellID = ResolveLuaArgsToSpellID(L);
    Game::Lua::PushBool(L, ComputeIsHarmful(spellID));
    return 1;
}

// `IsHelpfulSpell(spellID)` / `IsHelpfulSpell(slot, bookType)` —
// modern global. True for spells that exist and aren't marked
// harmful. Vanilla 1.12 has no dedicated "helpful" flag in
// Spell.dbc, so "non-harmful and present" is the best approximation
// without parsing every effect's implicit target.
static int __fastcall Script_IsHelpfulSpell(void *L) {
    const int spellID = ResolveLuaArgsToSpellID(L);
    Game::Lua::PushBool(L, ComputeIsHelpful(spellID));
    return 1;
}

// `C_Spell.IsSpellHarmful(spellID)` — direct-by-ID modern signature.
// No spellbook-slot variant; takes a numeric spellID only.
static int __fastcall Script_C_Spell_IsSpellHarmful(void *L) {
    Game::Lua::PushBool(L, ComputeIsHarmful(Spell::Arg::ResolveSpellID(L, 1)));
    return 1;
}

// `C_Spell.IsSpellHelpful(spellID)` — direct-by-ID modern signature.
static int __fastcall Script_C_Spell_IsSpellHelpful(void *L) {
    Game::Lua::PushBool(L, ComputeIsHelpful(Spell::Arg::ResolveSpellID(L, 1)));
    return 1;
}

// The learn / unlearn writer co-hooks (Player::StatSignal bump +
// LEARNED_SPELL_IN_SKILL_LINE) live in spell/Learn.cpp.

// Enum.SpellBookItemType values (retail). Vanilla's spellbook only ever
// produces real spells, so we emit Spell (player book) or PetAction (pet
// book); None/FutureSpell/Flyout don't occur in 1.12.
static constexpr int kSpellBookItemTypeSpell = 1;
static constexpr int kSpellBookItemTypePetAction = 3;

static const Game::Lua::EnumIntegerEntry kSpellBookSpellBankEntries[] = {
    {"Player", 0}, {"Pet", 1},
};
static const Game::Lua::EnumIntegerEntry kSpellBookItemTypeEntries[] = {
    {"None", 0}, {"Spell", 1}, {"FutureSpell", 2}, {"PetAction", 3},
    {"Flyout", 4},
};

// `C_SpellBook.GetSpellBookItemInfo(slotIndex, spellBank)` -> table.
// `slotIndex` is 1-based across the whole book; `spellBank` is
// Enum.SpellBookSpellBank (0 = Player, 1 = Pet). Returns nil for an empty or
// out-of-range slot. Vanilla's spellbook holds only real spells, so `itemType`
// is always Spell (player) or PetAction (pet).
//
// Field deviations from retail, all forced by 1.12 lacking the data:
//   - `iconID` is the icon PATH string (vanilla has no fileID), same as
//     C_Spell.GetSpellInfo.iconID -- feed it straight to texture:SetTexture.
//   - `isOffSpec` is always false (vanilla has no specializations).
//   - `skillLineIndex` is omitted (nil) -- spellbook tabs aren't SkillLines.
static int __fastcall Script_C_SpellBook_GetSpellBookItemInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0; // modern returns nil (not an error) for a bad index
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const int bookType = Spell::Lookup::SpellBankArgToBookType(L, 2);

    const int spellID = Spell::Lookup::SpellbookSlotToID(slot, bookType);
    if (spellID <= 0)
        return 0; // empty slot / out of range -> nil

    SpellInfoData info;
    if (!ReadSpellInfo(spellID, info))
        return 0;

    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    const uint32_t attr =
        record ? Game::Read<uint32_t>(
                     record, Offsets::OFF_SPELL_RECORD_ATTRIBUTES)
               : 0u;

    Game::Lua::NewTable(L);
    SetField(L, "itemType",
             static_cast<double>(bookType == 1 ? kSpellBookItemTypePetAction
                                               : kSpellBookItemTypeSpell));
    SetField(L, "actionID", static_cast<double>(spellID));
    SetField(L, "spellID", static_cast<double>(spellID));
    SetField(L, "name", info.name);
    // subName carries the rank text ("Rank N"); modern uses "" when absent.
    SetField(L, "subName", info.rank ? info.rank : "");
    SetField(L, "iconID", info.iconPath); // path string -- see note above
    SetFieldBool(L, "isPassive", (attr & Offsets::SPELL_ATTR_PASSIVE) != 0);
    SetFieldBool(L, "isOffSpec", false);
    return 1;
}

// --- Documentation ----------------------------------------------------------

// The globals that share `ResolveLuaArgsToSpellID`: a spell ID, a name, a
// "name(Rank N)", or a link — or a spellbook slot when a bookType follows.
static const Game::Doc::Field kSpellOrSlotArgs[] = {
    Game::Doc::Req("spell", "SpellIdentifier",
                   "A spell ID, name, or link, or a spellbook slot when bookType is given."),
    Game::Doc::Opt("bookType", "string", nullptr,
                   "\"spell\" or \"pet\"; makes the first argument a slot in that book."),
};

// The `C_Spell.*` forms take one identifier; they have no slot form.
static const Game::Doc::Field kSpellIdArgs[] = {
    Game::Doc::Req("spell", "SpellIdentifier", "A spell ID, spell link, or spell name."),
};

static const Game::Doc::Field kGetSpellInfoRets[] = {
    Game::Doc::Opt("name", "string", nullptr,
                   "Localized spell name; all returns are nil for an unknown spell."),
    Game::Doc::Opt("rank", "string", nullptr, "Localized rank text, such as \"Rank 2\"."),
    Game::Doc::Opt("icon", "string", nullptr, "Icon texture path."),
    Game::Doc::Opt("cost", "number", nullptr, "Base power cost."),
    Game::Doc::Opt("isFunnel", "bool", nullptr, "True for a health funnel spell."),
    Game::Doc::Opt("powerType", "number", nullptr,
                   "0 mana, 1 rage, 2 focus, 3 energy, 4 happiness."),
    Game::Doc::Opt("castTime", "number", nullptr,
                   "Base cast time in milliseconds; 0 when instant."),
    Game::Doc::Opt("minRange", "number", nullptr, "Minimum range in yards."),
    Game::Doc::Opt("maxRange", "number", nullptr, "Maximum range in yards."),
    Game::Doc::Opt("spellID", "number", nullptr, "The spell ID the arguments resolved to."),
};
static const Game::Doc::Function kGetSpellInfo{
    "Name, rank, icon, cost, cast time and range for any spell, plus its ID.",
    kSpellOrSlotArgs, kGetSpellInfoRets, "SpellGlobals"};

static const Game::Doc::Field kSpellInfoFields[] = {
    Game::Doc::Opt("name", "string", nullptr, "Localized spell name."),
    Game::Doc::Opt("iconID", "string", nullptr,
                   "Icon texture path; give it to texture:SetTexture."),
    Game::Doc::Req("castTime", "number", "Base cast time in milliseconds; 0 when instant."),
    Game::Doc::Req("minRange", "number", "Minimum range in yards."),
    Game::Doc::Req("maxRange", "number", "Maximum range in yards."),
    Game::Doc::Req("spellID", "number", "The spell ID."),
    Game::Doc::Opt("rank", "string", nullptr, "Localized rank text, such as \"Rank 2\"."),
    Game::Doc::Req("cost", "number", "Base power cost."),
    Game::Doc::Req("isFunnel", "bool", "True for a health funnel spell."),
    Game::Doc::Req("powerType", "number", "0 mana, 1 rage, 2 focus, 3 energy, 4 happiness."),
};
static const Game::Doc::Structure kSpellInfoStruct{
    "SpellInfo", "Spell", kSpellInfoFields, "Static data for one spell."};

static const Game::Doc::Field kCSpellInfoRets[] = {
    Game::Doc::Opt("info", "SpellInfo", nullptr, "Nil for an unknown spell."),
};
static const Game::Doc::Function kCSpellGetSpellInfo{
    "A table of the spell's name, icon, cast time, range and cost.",
    kSpellIdArgs, kCSpellInfoRets};

static const Game::Doc::Field kSpellNameRets[] = {
    Game::Doc::Opt("name", "string", nullptr,
                   "Nil for an unknown spell, or one with no name in this locale."),
};
static const Game::Doc::Function kCSpellGetSpellName{
    "The localized name of a spell.", kSpellIdArgs, kSpellNameRets};

static const Game::Doc::Field kSpellTextureRets[] = {
    Game::Doc::Opt("texture", "string", nullptr,
                   "Icon texture path; nil when the spell or its icon is unknown."),
};
static const Game::Doc::Function kCSpellGetSpellTexture{
    "The icon texture path for a spell.", kSpellIdArgs, kSpellTextureRets};

static const Game::Doc::Field kGetSpellLinkRets[] = {
    Game::Doc::Opt("link", "string", nullptr,
                   "Chat hyperlink for the spell; nil for an unknown spell."),
    Game::Doc::Opt("spellID", "number", nullptr, "The spell ID the arguments resolved to."),
};
static const Game::Doc::Function kGetSpellLink{
    "The chat hyperlink for a spell, with the spell ID it resolves to.",
    kSpellOrSlotArgs, kGetSpellLinkRets, "SpellGlobals"};

static const Game::Doc::Field kCSpellLinkRets[] = {
    Game::Doc::Opt("link", "string", nullptr,
                   "Chat hyperlink for the spell; nil for an unknown spell."),
};
static const Game::Doc::Function kCSpellGetSpellLink{
    "The chat hyperlink for a spell.", kSpellIdArgs, kCSpellLinkRets};

static const Game::Doc::Field kFindSlotArgs[] = {
    Game::Doc::Req("spellID", "number", "The spell to look for."),
};
static const Game::Doc::Field kFindSlotRets[] = {
    Game::Doc::Opt("slot", "luaIndex", nullptr,
                   "Spellbook slot holding the spell; nil when neither book has it."),
    Game::Doc::Opt("bookType", "string", nullptr, "\"spell\" or \"pet\"."),
};
static const Game::Doc::Function kFindSpellBookSlotByID{
    "The spellbook slot and book that hold a spell.",
    kFindSlotArgs, kFindSlotRets, "SpellGlobals"};

static const Game::Doc::Field kIsPassiveRets[] = {
    Game::Doc::Opt("isPassive", "bool", nullptr,
                   "True when the spell applies itself with no cast; nil for an unknown spell."),
};
static const Game::Doc::Function kIsPassiveSpell{
    "Whether a spell is passive, so it needs no cast.",
    kSpellOrSlotArgs, kIsPassiveRets, "SpellGlobals"};
static const Game::Doc::Function kCSpellIsSpellPassive{
    "Whether a spell is passive, so it needs no cast.",
    kSpellIdArgs, kIsPassiveRets};

static const Game::Doc::Field kIsPlayerSpellArgs[] = {
    Game::Doc::Req("spellID", "number", "The spell to test."),
};
static const Game::Doc::Field kIsPlayerSpellRets[] = {
    Game::Doc::Req("isKnown", "bool", "True when the player knows this exact spell ID."),
};
static const Game::Doc::Function kIsPlayerSpell{
    "Whether the player knows the spell, talents, racials and recipes included.",
    kIsPlayerSpellArgs, kIsPlayerSpellRets, "SpellGlobals"};

static const Game::Doc::Field kCanDualWieldRets[] = {
    Game::Doc::Req("canDualWield", "bool", "True when the player has learned Dual Wield."),
};
static const Game::Doc::Function kCanDualWield{
    "Whether the player can hold a weapon in the off hand.",
    {}, kCanDualWieldRets, "SpellGlobals"};

static const Game::Doc::Field kIsSpellKnownArgs[] = {
    Game::Doc::Req("spellID", "number", "The spell to test."),
    Game::Doc::Opt("isPet", "bool", "false", "Search the pet spellbook instead."),
};
static const Game::Doc::Field kIsSpellKnownRets[] = {
    Game::Doc::Req("isKnown", "bool", "True when the chosen spellbook holds the spell."),
};
static const Game::Doc::Function kIsSpellKnown{
    "Whether the spell has a button in the player's or the pet's spellbook.",
    kIsSpellKnownArgs, kIsSpellKnownRets, "SpellGlobals"};

static const Game::Doc::Field kIsHarmfulRets[] = {
    Game::Doc::Req("isHarmful", "bool", "True when an effect of the spell aims at an enemy."),
};
static const Game::Doc::Function kIsHarmfulSpell{
    "Whether the spell aims at an enemy.",
    kSpellOrSlotArgs, kIsHarmfulRets, "SpellGlobals"};
static const Game::Doc::Function kCSpellIsSpellHarmful{
    "Whether the spell aims at an enemy.", kSpellIdArgs, kIsHarmfulRets};

static const Game::Doc::Field kIsHelpfulRets[] = {
    Game::Doc::Req("isHelpful", "bool",
                   "True when an effect of the spell aims at yourself or a friendly unit."),
};
static const Game::Doc::Function kIsHelpfulSpell{
    "Whether the spell aims at yourself or a friendly unit.",
    kSpellOrSlotArgs, kIsHelpfulRets, "SpellGlobals"};
static const Game::Doc::Function kCSpellIsSpellHelpful{
    "Whether the spell aims at yourself or a friendly unit.",
    kSpellIdArgs, kIsHelpfulRets};

static const Game::Doc::Field kSpellBookItemInfoFields[] = {
    Game::Doc::Req("itemType", "SpellBookItemType", "Spell for the player book, PetAction for the pet book."),
    Game::Doc::Req("actionID", "number", "The spell ID; the same value as spellID."),
    Game::Doc::Req("spellID", "number", "The spell ID in the slot."),
    Game::Doc::Opt("name", "string", nullptr, "Localized spell name."),
    Game::Doc::Req("subName", "string", "Rank text, such as \"Rank 3\", or an empty string."),
    Game::Doc::Opt("iconID", "string", nullptr,
                   "Icon texture path; give it to texture:SetTexture."),
    Game::Doc::Req("isPassive", "bool", "True for a passive spell."),
    Game::Doc::Req("isOffSpec", "bool", "Always false."),
};
static const Game::Doc::Structure kSpellBookItemInfoStruct{
    "SpellBookItemInfo", "SpellBook", kSpellBookItemInfoFields,
    "The spell that fills one spellbook slot."};

static const Game::Doc::Field kGetSpellBookItemInfoArgs[] = {
    Game::Doc::Req("slotIndex", "luaIndex", "Slot number, counted across the whole book."),
    Game::Doc::Opt("spellBank", "SpellBookSpellBank", "0",
                   "Which book to read; Player by default."),
};
static const Game::Doc::Field kGetSpellBookItemInfoRets[] = {
    Game::Doc::Opt("info", "SpellBookItemInfo", nullptr,
                   "Nil for an empty slot, or a slot past the end of the book."),
};
static const Game::Doc::Function kGetSpellBookItemInfo{
    "A table describing the spell in a spellbook slot.",
    kGetSpellBookItemInfoArgs, kGetSpellBookItemInfoRets};

static const Game::Doc::Field kByAuraArgs[] = {
    Game::Doc::Req("auraName", "number", "The aura code an effect of the spell must apply."),
};
static const Game::Doc::Field kByAuraRets[] = {
    Game::Doc::Req("spellIDs", "table",
                   "Spell IDs in ascending order; empty when no known spell applies the aura."),
};
static const Game::Doc::Function kGetPlayerSpellsByAura{
    "Every spell the player knows that applies the given aura.",
    kByAuraArgs, kByAuraRets};

static const Game::Doc::Field kDisenchantRets[] = {
    Game::Doc::Req("hasDisenchant", "bool", "True when a known spell disenchants items."),
};
static const Game::Doc::Function kContainsAnyDisenchantSpell{
    "Whether the player knows a spell that disenchants items.", {}, kDisenchantRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetSpellInfo", &Script_GetSpellInfo, &kGetSpellInfo);
    Game::Lua::RegisterGlobalFunction("GetSpellLink", &Script_GetSpellLink, &kGetSpellLink);
    Game::Lua::RegisterGlobalFunction("FindSpellBookSlotByID",
                                      &Script_FindSpellBookSlotByID,
                                      &kFindSpellBookSlotByID);
    Game::Lua::RegisterGlobalFunction("IsPassiveSpell", &Script_IsPassiveSpell,
                                      &kIsPassiveSpell);
    Game::Lua::RegisterGlobalFunction("IsPlayerSpell", &Script_IsPlayerSpell,
                                      &kIsPlayerSpell);
    Game::Lua::RegisterGlobalFunction("CanDualWield", &Script_CanDualWield, &kCanDualWield);
    Game::Lua::RegisterGlobalFunction("IsSpellKnown", &Script_IsSpellKnown, &kIsSpellKnown);
    Game::Lua::RegisterGlobalFunction("IsHarmfulSpell", &Script_IsHarmfulSpell,
                                      &kIsHarmfulSpell);
    Game::Lua::RegisterGlobalFunction("IsHelpfulSpell", &Script_IsHelpfulSpell,
                                      &kIsHelpfulSpell);
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellLink", &Script_C_GetSpellLink,
                                      &kCSpellGetSpellLink);
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellInfo", &Script_C_GetSpellInfo,
                                      &kCSpellGetSpellInfo);
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellName", &Script_C_GetSpellName,
                                      &kCSpellGetSpellName);
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellTexture", &Script_C_GetSpellTexture,
                                      &kCSpellGetSpellTexture);
    Game::Lua::RegisterTableFunction("C_Spell", "IsSpellPassive", &Script_C_IsSpellPassive,
                                      &kCSpellIsSpellPassive);
    Game::Lua::RegisterTableFunction("C_Spell", "IsSpellHarmful",
                                      &Script_C_Spell_IsSpellHarmful,
                                      &kCSpellIsSpellHarmful);
    Game::Lua::RegisterTableFunction("C_Spell", "IsSpellHelpful",
                                      &Script_C_Spell_IsSpellHelpful,
                                      &kCSpellIsSpellHelpful);
    Game::Lua::RegisterTableFunction("C_SpellBook", "GetSpellBookItemInfo",
                                      &Script_C_SpellBook_GetSpellBookItemInfo,
                                      &kGetSpellBookItemInfo);
    Game::Lua::RegisterTableFunction("C_SpellBook", "GetPlayerSpellsByAura",
                                      &Script_GetPlayerSpellsByAura,
                                      &kGetPlayerSpellsByAura);
    Game::Lua::RegisterTableFunction("C_SpellBook", "ContainsAnyDisenchantSpell",
                                      &Script_ContainsAnyDisenchantSpell,
                                      &kContainsAnyDisenchantSpell);
    Game::Lua::RegisterIntegerEnum("Enum", "SpellBookSpellBank",
                                   kSpellBookSpellBankEntries, 2, "SpellBook");
    Game::Lua::RegisterIntegerEnum("Enum", "SpellBookItemType",
                                   kSpellBookItemTypeEntries, 5, "SpellBook");
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::Info
