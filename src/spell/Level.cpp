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
#include "spell/Lookup.h"
#include "talent/SpellSet.h"

#include <cstdint>
#include <unordered_set>

namespace Spell::Level {

namespace {

// Reads the local player's descriptor — same pattern as
// Unit::Combat::Script_InCombatLockdown. Returns nullptr at login
// screen / character select / loading transition where the
// CGPlayer doesn't exist yet.
const uint8_t *PlayerDescriptor() {
    auto *player = static_cast<const uint8_t *>(Game::ResolveUnitToken("player"));
    if (player == nullptr)
        return nullptr;
    return Game::Read<const uint8_t *>(
        player, Offsets::OFF_UNIT_DESCRIPTOR);
}

// Looks up the `BaseLevel` field of a Spell.dbc record. Returns 0
// for invalid spellIDs or records the engine doesn't have cached.
int SpellBaseLevel(int spellID) {
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return 0;
    return Game::Read<int32_t>(
        record, Offsets::OFF_SPELL_RECORD_BASE_LEVEL);
}

// Spell.dbc attribute filter for the learnable-spell walk. Only the
// verified HIDDEN_CLIENTSIDE bit is used:
//
//   Attributes (+0x18) bit 0x80 = HIDDEN_CLIENTSIDE — engine-internal
//   helpers like "Defensive Stance Passive" (7376) that pair with a
//   user-visible spell (Defensive Stance, 71) and never appear in the
//   spellbook. Skip these.
//
// A prior version also tested AttributesEx3 (+0x24) bit 0x02000000 as a
// "hide from auto-learn" gate ported from a MoP function (FUN_00911c60).
// That was invalid on every count: FUN_00911c60 is outside 1.12's .text
// and its `+0x24` is not 1.12's AttributesEx3. Verified against Spell.dbc,
// the 1.12 bit is a sparse grab-bag (44 / 27925 records: passive procs,
// odd DoT ranks, the Night Elf priest Starshards line, Turtle customs) —
// not an auto-learn flag — so it could only wrongly hide legitimate
// learnable spells (Starshards carries it). Removed. The authoritative
// auto-learn signal is SkillLineAbility.learnOnGetSkill (col 9: 2 =
// race/class, 1 = profession, 0 = trainer-taught), not a Spell.dbc bit;
// this feature deliberately reports the broader "trainable at level" set,
// so it applies no learnOnGetSkill filter.
constexpr uint32_t SPELL_ATTR_HIDDEN_CLIENTSIDE = 0x80;

bool SpellHiddenFromSpellbook(int spellID) {
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return false;
    const uint32_t attributes = Game::Read<uint32_t>(
        record, Offsets::OFF_SPELL_RECORD_ATTRIBUTES);
    return (attributes & SPELL_ATTR_HIDDEN_CLIENTSIDE) != 0;
}

// ---- Target-level gate for ranked beneficial auras ----------------------
//
// A ranked beneficial aura can't be applied to a target too low for the
// rank — e.g. Divine Spirit Rank 1 (14752) requires the target to be level
// >= 20. Server-enforced via `SpellMgr::SelectAuraRankForLevel`, whose rule
// is: a ranked positive aura is castable when `targetLevel + 10 >=
// spellLevel`, i.e. the minimum target level is `spellLevel - 10`. Below
// it the lowest rank fails (SPELL_FAILED_LOWLEVEL); higher ranks auto-
// downrank. The gate only applies to *ranked* *positive* *auras* — see
// RequiredTargetLevel. Fully derivable from the client's Spell.dbc.
//
// (The explicit MinTargetLevel / MaxTargetLevel fields the server also
// checks are NOT in the 1.12 client's Spell.dbc — server-only columns — so
// the spellLevel rule is the only client-readable target-level mechanism.)

constexpr uint32_t SPELL_EFFECT_APPLY_AREA_AURA_PARTY = 35;

int LocaleIndex() {
    return Game::Read<int>(
        static_cast<uintptr_t>(Offsets::VAR_LOCALE_INDEX));
}

// The friendly-target IDs the server's SelectAuraRankForLevel treats as
// "positive" for rank selection: IsExplicitPositiveTarget (21/35/45/57/61)
// plus IsAreaEffectPossitiveTarget (20/30/31/33/34/37/56/61). Narrower than
// a general "is helpful" set — this is the exact gate the rank rule uses
// (self-cast / pet targets don't count).
bool IsPositiveRankTarget(uint32_t t) {
    switch (t) {
    case 21: // TARGET_UNIT_FRIEND
    case 35: // TARGET_UNIT_PARTY
    case 45: // TARGET_UNIT_FRIEND_CHAIN_HEAL
    case 57: // TARGET_UNIT_RAID
    case 61: // TARGET_UNIT_RAID_AND_CLASS
    case 20: // TARGET_ENUM_UNITS_PARTY_WITHIN_CASTER_RANGE
    case 30: // TARGET_ENUM_UNITS_FRIEND_AOE_AT_SRC_LOC
    case 31: // TARGET_ENUM_UNITS_FRIEND_AOE_AT_DEST_LOC
    case 33: // TARGET_ENUM_UNITS_PARTY_AOE_AT_SRC_LOC
    case 34: // TARGET_ENUM_UNITS_PARTY_AOE_AT_DEST_LOC
    case 37: // TARGET_UNIT_FRIEND_AND_PARTY
    case 56: // TARGET_ENUM_UNITS_RAID_WITHIN_CASTER_RANGE
        return true;
    default:
        return false;
    }
}

// True if the record carries a localized "Rank N" string — the client's
// marker for a spell in a ranked line (single-rank spells leave it empty).
// Proxy for the server's `GetSpellRank(id) != 0`, verified against
// single-rank buffs (Power Infusion / Innervate / Blessing of Kings, all
// empty Rank and un-gated) vs ranked ones (Divine Spirit / Fortitude).
bool HasRankString(const uint8_t *record) {
    const char *rank = Game::Read<const char *>(
        record, Offsets::OFF_SPELL_RECORD_RANK + LocaleIndex() * 4);
    return rank != nullptr && *rank != '\0';
}

// Minimum target level required to apply this exact spell (rank), or 0 if
// the spell isn't subject to the ranked-positive-aura target-level rule.
int RequiredTargetLevel(const uint8_t *record) {
    const int spellLevel =
        Game::Read<int>(record, Offsets::OFF_SPELL_RECORD_SPELL_LEVEL);
    if (spellLevel <= 10) // targetLevel + 10 >= spellLevel holds for any level
        return 0;
    const uint32_t attr =
        Game::Read<uint32_t>(record, Offsets::OFF_SPELL_RECORD_ATTRIBUTES);
    if (attr & Offsets::SPELL_ATTR_PASSIVE)
        return 0;
    if (!HasRankString(record)) // single-rank spell → not gated
        return 0;

    auto *effect = Game::Ptr<const uint32_t>(record, Offsets::OFF_SPELL_RECORD_EFFECT);
    auto *targetA =
        Game::Ptr<const uint32_t>(record, Offsets::OFF_SPELL_RECORD_EFFECT_IMPLICIT_TARGET_A);
    bool positiveAura = false;
    for (int i = 0; i < 3; ++i) {
        if ((effect[i] == Offsets::SPELL_EFFECT_APPLY_AURA &&
             IsPositiveRankTarget(targetA[i])) ||
            effect[i] == SPELL_EFFECT_APPLY_AREA_AURA_PARTY) {
            positiveAura = true;
            break;
        }
    }
    if (!positiveAura)
        return 0;
    return spellLevel - 10;
}

int ArgSpellID(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0;
    return static_cast<int>(Game::Lua::ToNumber(L, 1));
}

// `C_Spell.GetSpellLevelInfo(spellID)` → spellLevel, baseLevel, maxLevel.
// Raw Spell.dbc level fields: spellLevel (this rank's effective level),
// baseLevel, maxLevel (level at which scaling caps). nil for an unknown
// spellID.
int __fastcall Script_GetSpellLevelInfo(void *L) {
    const uint8_t *record = Spell::Lookup::RecordForID(ArgSpellID(L));
    if (record == nullptr)
        return 0;
    Game::Lua::PushNumber(
        L, static_cast<double>(
               Game::Read<int>(record, Offsets::OFF_SPELL_RECORD_SPELL_LEVEL)));
    Game::Lua::PushNumber(
        L, static_cast<double>(
               Game::Read<int>(record, Offsets::OFF_SPELL_RECORD_BASE_LEVEL)));
    Game::Lua::PushNumber(
        L, static_cast<double>(
               Game::Read<int>(record, Offsets::OFF_SPELL_RECORD_MAX_LEVEL)));
    return 3;
}

// `GetSpellRequiredTargetLevel(spellID)` → the minimum level a target must
// be for this spell (rank) to apply (0 = no restriction). nil for an
// unknown spellID. Ranked beneficial auras only; see RequiredTargetLevel.
int __fastcall Script_GetSpellRequiredTargetLevel(void *L) {
    const uint8_t *record = Spell::Lookup::RecordForID(ArgSpellID(L));
    if (record == nullptr)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(RequiredTargetLevel(record)));
    return 1;
}

// `C_SpellBook.GetSpellLevelLearned(spellID)` — returns the level
// at which a spell becomes available (the `BaseLevel` field in
// Spell.dbc). Direct read off the record — no class/race filtering,
// the level is the spell's own intrinsic requirement.
//
// Returns 0 for: invalid spellIDs, spells with no level requirement
// (most non-class utility spells), or records the engine doesn't
// have. Matches modern semantics where unknown/utility spells
// return 0.
int __fastcall Script_GetSpellLevelLearned(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_SpellBook.GetSpellLevelLearned(spellID)");
        return 0;
    }
    const int spellID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (spellID <= 0) {
        Game::Lua::PushNumber(L, 0);
        return 1;
    }
    Game::Lua::PushNumber(L, static_cast<double>(SpellBaseLevel(spellID)));
    return 1;
}

// `C_SpellBook.GetCurrentLevelSpells([level])` — returns a 1-based
// table of spellIDs the player's class/race can learn at `level`.
// If `level` is omitted or non-numeric, defaults to the player's
// current level.
//
// Algorithm: walk SkillLineAbility.dbc; for each entry with the
// player's class/race mask bits matching (and not in the exclude
// masks), look up the spell's BaseLevel. If it equals the queried
// level, the entry contributes to the result.
//
// Vanilla 1.12's class progression is trainer-driven — most class
// spells require visiting a trainer to actually learn them, even
// though SkillLineAbility says "this is a Mage spell available at
// level 12". Modern `GetCurrentLevelSpells` (added in 5.x when
// trainers were removed) reports auto-learned spells; we report
// the closest available analog — *what's trainable* at this level.
// Addon code that ported from MoP+ for "what's new this level" UI
// will find this useful.
//
// The optional `level` argument lets addons preview "what would I
// get at level N" (e.g. a talent planner showing future spells).
// Class/race still comes from the local player — there's no
// `(class, race, level)` form because vanilla doesn't expose a
// class-string→classID lookup we can rely on cleanly.
//
// Empty inputs (no player, no descriptor, missing DBC) return an
// empty table — never errors, never returns nil. Matches modern.
int __fastcall Script_GetCurrentLevelSpells(void *L) {
    // Optional level arg (stack[1]) — read BEFORE blowing the stack.
    const bool hasLevelArg = Game::Lua::IsNumber(L, 1);
    const int argLevel = hasLevelArg
        ? static_cast<int>(Game::Lua::ToNumber(L, 1))
        : 0;

    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    auto *desc = PlayerDescriptor();
    if (desc == nullptr)
        return 1;

    const uint8_t classByte = *(desc + Offsets::OFF_UNIT_DESCRIPTOR_CLASS_BYTE);
    const uint8_t raceByte = *(desc + Offsets::OFF_UNIT_DESCRIPTOR_RACE_BYTE);
    const int32_t playerLevel = Game::Read<int32_t>(
        desc, Offsets::OFF_UNIT_FIELD_LEVEL);
    if (classByte == 0 || raceByte == 0 || playerLevel <= 0)
        return 1;

    // Resolve the queried level — caller-provided when valid (>0),
    // else fall back to the player's current level. Out-of-range
    // values (negative, 0) are treated as "use mine" since there's
    // no useful answer for them anyway.
    const int queryLevel = (hasLevelArg && argLevel > 0) ? argLevel : playerLevel;

    const uint32_t playerClassBit = 1u << (classByte - 1);
    const uint32_t playerRaceBit = 1u << (raceByte - 1);

    // Bitmask of all valid 1.12 vanilla class bits combined:
    //   1=Warrior, 2=Paladin, 3=Hunter, 4=Rogue, 5=Priest,
    //   7=Shaman, 8=Mage, 9=Warlock, 11=Druid
    // (Class 6 = Death Knight, 10 = Monk; both post-vanilla.)
    //
    // Used as a sanity gate: a class mask with bits OUTSIDE this
    // range is junk data — typically Turtle WoW custom spells with
    // a buggy classMask = 0xFFFFFFFF or sentinel values. Honoring
    // those would surface unrelated-class spells (e.g. Hunter's
    // Searing Shot showing up for a Priest at level 32 because
    // 0xFFFFFFFF AND Priest-bit != 0). Drop them.
    constexpr uint32_t VALID_VANILLA_CLASS_MASK =
        (1u << 0)  | (1u << 1)  | (1u << 2)  | (1u << 3)  | (1u << 4)  |
        (1u << 6)  | (1u << 7)  | (1u << 8)  | (1u << 10);

    const uint8_t *const *records = Game::Read<const uint8_t *const *>(
        static_cast<uintptr_t>(Offsets::VAR_SKILL_LINE_ABILITY_RECORDS));
    const int count = Game::Read<int>(
        static_cast<uintptr_t>(Offsets::VAR_SKILL_LINE_ABILITY_COUNT));
    if (records == nullptr || count <= 0)
        return 1;

    // Dedup: pet abilities (Bite, Claw, Growl) have one SLA entry per
    // creature family, all pointing at the same spellID. Without
    // dedup the list double-counts. Even if the class filter below
    // catches them, profession recipes also have multi-row entries
    // (per skill line tier) so we keep the set.
    std::unordered_set<int> seen;

    int outIdx = 1;
    for (int i = 1; i <= count; ++i) {
        const uint8_t *rec = records[i];
        if (rec == nullptr)
            continue;

        const uint32_t recClassMask = Game::Read<uint32_t>(
            rec, Offsets::OFF_SLA_CLASS_MASK);
        const uint32_t recRaceMask = Game::Read<uint32_t>(
            rec, Offsets::OFF_SLA_RACE_MASK);
        const uint32_t recExcludeClass = Game::Read<uint32_t>(
            rec, Offsets::OFF_SLA_EXCLUDE_CLASS);
        const uint32_t recExcludeRace = Game::Read<uint32_t>(
            rec, Offsets::OFF_SLA_EXCLUDE_RACE);

        // Skip if explicitly excluded.
        if ((recExcludeClass & playerClassBit) != 0)
            continue;
        if ((recExcludeRace & playerRaceBit) != 0)
            continue;

        // Race filter — race-locked spells need our race bit set in
        // raceMask. Applied to both class spells and racials. Without
        // this, Turtle WoW's race-locked class spells (e.g. Searing
        // Shot tagged as Night Elf Priest at skill 613) leak into
        // other races' results.
        const bool raceOk = (recRaceMask == 0) ||
                            ((recRaceMask & playerRaceBit) != 0);
        if (!raceOk)
            continue;

        // Match rules:
        //   - classMask has our class bit AND classMask has no
        //     bits outside vanilla's valid-class set → class spell
        //     (Smite, Fireball, etc.). The PRIMARY case. The
        //     valid-bits check rejects Turtle's buggy classMask =
        //     0xFFFFFFFF entries that would otherwise leak Hunter
        //     spells into a Priest's list, etc.
        //   - classMask = 0 AND raceMask has our race bit → racial
        //     ability (Stoneform, Berserking, etc.) — class-agnostic
        //     but race-locked. (Race bit already verified above.)
        //
        // We deliberately DO NOT pass `classMask = 0 AND raceMask = 0`
        // (universal entries) because that bucket is dominated by pet
        // abilities, profession recipes, and generic utility — none of
        // which modern's `GetCurrentLevelSpells` reports. Modern
        // returns class-specific learnable spells only; we match that.
        const bool maskWithinValid =
            (recClassMask & ~VALID_VANILLA_CLASS_MASK) == 0;
        const bool isClassSpell = maskWithinValid &&
                                   (recClassMask & playerClassBit) != 0;
        const bool isRacial = (recClassMask == 0) && (recRaceMask != 0);
        if (!isClassSpell && !isRacial)
            continue;

        const int spellID = Game::Read<int>(
            rec, Offsets::OFF_SLA_SPELL_ID);
        if (spellID <= 0)
            continue;
        if (SpellBaseLevel(spellID) != queryLevel)
            continue;
        // Skip talents — they're class spells in SLA but learned via
        // talent points, not auto-granted at level. Modern's
        // GetCurrentLevelSpells excludes them too.
        if (Talent::SpellSet::IsTalentRank(spellID))
            continue;
        // Skip engine-internal spellbook-hidden spells (HIDDEN_CLIENTSIDE):
        // they can carry a BaseLevel but never surface as learnable.
        if (SpellHiddenFromSpellbook(spellID))
            continue;
        if (!seen.insert(spellID).second)
            continue;

        Game::Lua::PushNumber(L, static_cast<double>(outIdx));
        Game::Lua::PushNumber(L, static_cast<double>(spellID));
        Game::Lua::SetTable(L, -3);
        ++outIdx;
    }
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_SpellBook", "GetSpellLevelLearned",
                                      &Script_GetSpellLevelLearned);
    Game::Lua::RegisterTableFunction("C_SpellBook", "GetCurrentLevelSpells",
                                      &Script_GetCurrentLevelSpells);
    Game::Lua::RegisterGlobalFunction("GetSpellRequiredTargetLevel",
                                      &Script_GetSpellRequiredTargetLevel);
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellLevelInfo",
                                      &Script_GetSpellLevelInfo);
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellRequiredTargetLevel",
                                      &Script_GetSpellRequiredTargetLevel);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Spell::Level
