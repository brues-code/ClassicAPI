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

// Shared item-stat accumulation engine — see [src/item/StatAccum.h] for
// the public surface. This file holds the value derivation:
//
// Three sources feed the accumulator:
//   1. The base `ItemStats_C` record — the stored attributes
//      (Str/Agi/Sta/Int/Spi, mana/health-on-equip), armor, six resists,
//      and (for weapons) DPS derived from the damage/delay fields.
//   2. The item's on-equip spells (`m_spellTrigger == 1`) — vanilla
//      (and Turtle's custom gear) store "special" bonuses (crit, hit,
//      attack power, spell power/healing, mp5, defense, dodge, parry,
//      block, spell crit, flat health/mana, …) as an equip spell whose
//      aura effect carries the value, not as a stat slot. We decode those
//      auras (the aura → key map is in AddSpellStatAuras).
//   3. The link's random suffix (`item:id:enchant:SUFFIX:unique`) —
//      ItemRandomProperties.dbc → SpellItemEnchantment.dbc, whose stat
//      suffixes are themselves equip spells (enchant type 3) and whose
//      armor suffixes are direct resistance enchants (type 4).
//
// **Values are vanilla-native.** The percent-based stats (crit / hit /
// defense) are reported as the raw vanilla percentage under the modern
// rating key — e.g. Krol Blade's "+1% crit" is
// `ITEM_MOD_CRIT_MELEE_RATING = 1`, NOT the level-scaled rating (13) that
// TBC/Era would show. Vanilla has no rating conversion (it's
// level-dependent and doesn't exist in this client), so a native percent
// is the only honest, level-independent value.
//
// **Spell damage stays split the vanilla way.** The server sums healing
// from aura 135 alone and spell damage from aura 13 filtered by school
// (tortoise-wow `SpellBaseHealingBonusDone` / `SpellBaseDamageBonusDone`),
// so 3.3.5's spell-power merge does not apply: "+N damage and healing"
// stays two keys, and a single-school "+N Fire damage" lands on a
// per-school key (a ClassicAPI extension — no client ships one) instead
// of reading as generic spell damage.
//
// The aura → key map (below) covers the common gear stats. Rarer auras
// (percent attack power, weapon-skill-per-line, damage-taken, procs) are
// intentionally skipped — they either lack a clean single key or aren't
// part of the stat set.

#include "item/StatAccum.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "item/Record.h"

#include <cstdlib>
#include <cstring>

namespace Item::StatAccum {

namespace {

// Base-record m_statType uses the ItemModType enum: 0=mana, 1=health,
// 3=agility, 4=strength, 5=intellect, 6=spirit, 7=stamina (2 is
// deprecated). Returns nullptr for types with no vanilla key.
const char *StatKeyForItemModType(uint32_t statType) {
    switch (statType) {
    case 0: return "ITEM_MOD_MANA_SHORT";
    case 1: return "ITEM_MOD_HEALTH_SHORT";
    case 3: return "ITEM_MOD_AGILITY_SHORT";
    case 4: return "ITEM_MOD_STRENGTH_SHORT";
    case 5: return "ITEM_MOD_INTELLECT_SHORT";
    case 6: return "ITEM_MOD_SPIRIT_SHORT";
    case 7: return "ITEM_MOD_STAMINA_SHORT";
    default: return nullptr;
    }
}

// SPELL_AURA_MOD_STAT (29) misc uses the UnitStat enum instead: 0=Str,
// 1=Agi, 2=Sta, 3=Int, 4=Spi (-1 = all stats, not a single key).
// Verified against the "Increased <stat> 01" random-property spells.
const char *StatKeyForUnitStat(int stat) {
    switch (stat) {
    case 0: return "ITEM_MOD_STRENGTH_SHORT";
    case 1: return "ITEM_MOD_AGILITY_SHORT";
    case 2: return "ITEM_MOD_STAMINA_SHORT";
    case 3: return "ITEM_MOD_INTELLECT_SHORT";
    case 4: return "ITEM_MOD_SPIRIT_SHORT";
    default: return nullptr;
    }
}

// Resistance school index → key. 0 = armor (physical), 1..6 = the magic
// schools in Holy/Fire/Nature/Frost/Shadow/Arcane order.
const char *ResistKey(int school) {
    static const char *const kKeys[7] = {
        "RESISTANCE0_NAME", "RESISTANCE1_NAME", "RESISTANCE2_NAME",
        "RESISTANCE3_NAME", "RESISTANCE4_NAME", "RESISTANCE5_NAME",
        "RESISTANCE6_NAME",
    };
    return (school >= 0 && school <= 6) ? kKeys[school] : nullptr;
}

// Single-school spell damage key, same 1..6 school order as ResistKey.
const char *SchoolDamageKey(int school) {
    static const char *const kKeys[7] = {
        nullptr,
        "ITEM_MOD_HOLY_SPELL_DAMAGE_DONE_SHORT",
        "ITEM_MOD_FIRE_SPELL_DAMAGE_DONE_SHORT",
        "ITEM_MOD_NATURE_SPELL_DAMAGE_DONE_SHORT",
        "ITEM_MOD_FROST_SPELL_DAMAGE_DONE_SHORT",
        "ITEM_MOD_SHADOW_SPELL_DAMAGE_DONE_SHORT",
        "ITEM_MOD_ARCANE_SPELL_DAMAGE_DONE_SHORT",
    };
    return (school >= 1 && school <= 6) ? kKeys[school] : nullptr;
}

// School-mask bits 1..6 — every magic school.
constexpr int kMagicSchools = 0x7E;

// Keys in this list are the ones AddByKey ever writes: the base stats +
// armor + six resistances, plus the equip-spell "special" stats. The
// equip-spell keys past the resistances are best-effort names verified
// against a 1.15.x client (crit melee/ranged are confirmed from a TBC
// GetItemStats dump); adjust here if the live client differs.
constexpr const char *kKeys[] = {
    "ITEM_MOD_MANA_SHORT",
    "ITEM_MOD_HEALTH_SHORT",
    "ITEM_MOD_AGILITY_SHORT",
    "ITEM_MOD_STRENGTH_SHORT",
    "ITEM_MOD_INTELLECT_SHORT",
    "ITEM_MOD_SPIRIT_SHORT",
    "ITEM_MOD_STAMINA_SHORT",
    "RESISTANCE0_NAME",
    "RESISTANCE1_NAME",
    "RESISTANCE2_NAME",
    "RESISTANCE3_NAME",
    "RESISTANCE4_NAME",
    "RESISTANCE5_NAME",
    "RESISTANCE6_NAME",
    "ITEM_MOD_ATTACK_POWER_SHORT",
    "ITEM_MOD_RANGED_ATTACK_POWER_SHORT",
    "ITEM_MOD_CRIT_MELEE_RATING",
    "ITEM_MOD_CRIT_RANGED_RATING",
    "ITEM_MOD_HIT_MELEE_RATING",
    "ITEM_MOD_HIT_RANGED_RATING",
    "ITEM_MOD_HIT_SPELL_RATING",
    "ITEM_MOD_CRIT_SPELL_RATING",
    "ITEM_MOD_SPELL_DAMAGE_DONE_SHORT",
    "ITEM_MOD_HOLY_SPELL_DAMAGE_DONE_SHORT",
    "ITEM_MOD_FIRE_SPELL_DAMAGE_DONE_SHORT",
    "ITEM_MOD_NATURE_SPELL_DAMAGE_DONE_SHORT",
    "ITEM_MOD_FROST_SPELL_DAMAGE_DONE_SHORT",
    "ITEM_MOD_SHADOW_SPELL_DAMAGE_DONE_SHORT",
    "ITEM_MOD_ARCANE_SPELL_DAMAGE_DONE_SHORT",
    "ITEM_MOD_SPELL_HEALING_DONE_SHORT",
    "ITEM_MOD_SPELL_PENETRATION",
    "ITEM_MOD_ARMOR_PENETRATION_RATING",
    "ITEM_MOD_MANA_REGENERATION",
    "ITEM_MOD_HEALTH_REGEN",
    "ITEM_MOD_DEFENSE_SKILL_RATING",
    "ITEM_MOD_DODGE_RATING",
    "ITEM_MOD_PARRY_RATING",
    "ITEM_MOD_BLOCK_RATING",
    "ITEM_MOD_BLOCK_VALUE",
};
static_assert(sizeof(kKeys) / sizeof(kKeys[0]) == kCount,
              "kCount must match the kKeys list");

long U32(const uint8_t *record, int offset) {
    return static_cast<long>(*reinterpret_cast<const uint32_t *>(record + offset));
}

// Folds a spell's stat/resistance auras into `acc`. Used for both the
// item's own on-equip spells and random-suffix equip enchants. The value
// is the fixed-die `EffectBasePoints + EffectBaseDice` (mangos
// CalculateSimpleValue). Percent stats keep their vanilla percentage.
// Auras with no clean stat key are ignored (procs, dummies,
// weapon-skill-per-line, damage-taken, etc.).
void AddSpellStatAuras(Accum *acc, int spellID, long sign) {
    if (spellID <= 0)
        return;
    const uint8_t *sp = DBC::Record(Offsets::VAR_SPELL_RECORDS,
                                    Offsets::VAR_SPELL_RECORD_COUNT,
                                    static_cast<uint32_t>(spellID));
    if (sp == nullptr)
        return;
    auto aura = Game::Ptr<const int32_t>(
        sp, Offsets::OFF_SPELL_RECORD_EFFECT_APPLY_AURA_NAME);
    auto misc = Game::Ptr<const int32_t>(
        sp, Offsets::OFF_SPELL_RECORD_EFFECT_MISC_VALUE);
    auto base = Game::Ptr<const int32_t>(
        sp, Offsets::OFF_SPELL_RECORD_EFFECT_BASE_POINTS);
    auto dice = Game::Ptr<const int32_t>(
        sp, Offsets::OFF_SPELL_RECORD_EFFECT_BASE_DICE);

    // Vanilla "+N Attack Power" spells carry a melee (99) AND a ranged
    // (124) aura with the same value; the generic melee key subsumes the
    // ranged one, so we emit ranged only for ranged-only spells (scopes).
    bool hasMeleeAP = false;
    for (int i = 0; i < Offsets::SPELL_RECORD_EFFECT_COUNT; ++i)
        if (aura[i] == 99)
            hasMeleeAP = true;

    for (int i = 0; i < Offsets::SPELL_RECORD_EFFECT_COUNT; ++i) {
        const long v = static_cast<long>(base[i]) + static_cast<long>(dice[i]);
        switch (aura[i]) {
        case 29: // SPELL_AURA_MOD_STAT — misc = UnitStat index
            AddByKey(acc, StatKeyForUnitStat(misc[i]), sign * v);
            break;
        case 34: // SPELL_AURA_MOD_INCREASE_HEALTH — flat +health
            AddByKey(acc, "ITEM_MOD_HEALTH_SHORT", sign * v);
            break;
        case 35: // SPELL_AURA_MOD_INCREASE_ENERGY — misc = power type
            if (misc[i] == 0) // mana
                AddByKey(acc, "ITEM_MOD_MANA_SHORT", sign * v);
            break;
        case 22: // SPELL_AURA_MOD_RESISTANCE — misc = school bitmask
            for (int school = 0; school <= 6; ++school)
                if (misc[i] & (1 << school))
                    AddByKey(acc, ResistKey(school), sign * v);
            break;
        case 99: // SPELL_AURA_MOD_ATTACK_POWER
            AddByKey(acc, "ITEM_MOD_ATTACK_POWER_SHORT", sign * v);
            break;
        case 124: // SPELL_AURA_MOD_RANGED_ATTACK_POWER
            if (!hasMeleeAP)
                AddByKey(acc, "ITEM_MOD_RANGED_ATTACK_POWER_SHORT", sign * v);
            break;
        case 52: // weapon crit % — TBC surfaces it as melee AND ranged
            AddByKey(acc, "ITEM_MOD_CRIT_MELEE_RATING", sign * v);
            AddByKey(acc, "ITEM_MOD_CRIT_RANGED_RATING", sign * v);
            break;
        case 54: // melee/ranged hit %
            AddByKey(acc, "ITEM_MOD_HIT_MELEE_RATING", sign * v);
            AddByKey(acc, "ITEM_MOD_HIT_RANGED_RATING", sign * v);
            break;
        case 55: // spell hit %
            AddByKey(acc, "ITEM_MOD_HIT_SPELL_RATING", sign * v);
            break;
        case 57: // SPELL_AURA_MOD_SPELL_CRIT_CHANCE
        case 71: // ..._SCHOOL (misc = school mask) — same stat
            AddByKey(acc, "ITEM_MOD_CRIT_SPELL_RATING", sign * v);
            break;
        case 47: // SPELL_AURA_MOD_PARRY_PERCENT
            AddByKey(acc, "ITEM_MOD_PARRY_RATING", sign * v);
            break;
        case 49: // SPELL_AURA_MOD_DODGE_PERCENT
            AddByKey(acc, "ITEM_MOD_DODGE_RATING", sign * v);
            break;
        case 51: // SPELL_AURA_MOD_BLOCK_PERCENT (block chance)
            AddByKey(acc, "ITEM_MOD_BLOCK_RATING", sign * v);
            break;
        case 13: { // SPELL_AURA_MOD_DAMAGE_DONE — misc = school mask
            // All magic schools → generic spell damage; a partial mask
            // goes to each school it names. Physical (bit 0) is skipped.
            const int magic = misc[i] & kMagicSchools;
            if (magic == kMagicSchools) {
                AddByKey(acc, "ITEM_MOD_SPELL_DAMAGE_DONE_SHORT", sign * v);
                break;
            }
            for (int school = 1; school <= 6; ++school)
                if (magic & (1 << school))
                    AddByKey(acc, SchoolDamageKey(school), sign * v);
            break;
        }
        case 135: // SPELL_AURA_MOD_HEALING_DONE
            AddByKey(acc, "ITEM_MOD_SPELL_HEALING_DONE_SHORT", sign * v);
            break;
        case 123: // SPELL_AURA_MOD_TARGET_RESISTANCE — misc = school mask
            // Stored negative (it lowers the target's resistance). Bit 0 is
            // armor ("Armor Penetration N" gear), the magic bits are spell
            // penetration — the same aura, two different stats.
            if (misc[i] & 1)
                AddByKey(acc, "ITEM_MOD_ARMOR_PENETRATION_RATING", sign * -v);
            if (misc[i] & kMagicSchools)
                AddByKey(acc, "ITEM_MOD_SPELL_PENETRATION", sign * -v);
            break;
        case 85: // SPELL_AURA_MOD_POWER_REGEN (mp5) — misc = power type
            if (misc[i] == 0) // mana
                AddByKey(acc, "ITEM_MOD_MANA_REGENERATION", sign * v);
            break;
        case 161: // SPELL_AURA_MOD_HEALTH_REGEN_IN_COMBAT (hp5)
            AddByKey(acc, "ITEM_MOD_HEALTH_REGEN", sign * v);
            break;
        case 158: // SPELL_AURA_MOD_SHIELD_BLOCKVALUE — adds to the record's m_block
            AddByKey(acc, "ITEM_MOD_BLOCK_VALUE", sign * v);
            break;
        case 30: // SPELL_AURA_MOD_SKILL — misc = skill line (95 = Defense)
            if (misc[i] == 95)
                AddByKey(acc, "ITEM_MOD_DEFENSE_SKILL_RATING", sign * v);
            break;
        default:
            break;
        }
    }
}

// Decodes the item's own on-equip spells (SpellTrigger == 1) into `acc`.
// On-use (0) and chance-on-hit (2) spells are skipped — they aren't
// passive stats. This is what surfaces crit / AP / spell power / hit /
// mp5 / defense on base items (vanilla stores them as equip spells, not
// stat slots).
void AddEquipSpellStats(Accum *acc, const uint8_t *record, long sign) {
    auto ids = Game::Ptr<const uint32_t>(
        record, Offsets::OFF_ITEMSTATS_SPELL_ID);
    auto triggers = Game::Ptr<const uint32_t>(
        record, Offsets::OFF_ITEMSTATS_SPELL_TRIGGER);
    for (int i = 0; i < Offsets::ITEMSTATS_SPELL_SLOT_COUNT; ++i)
        if (ids[i] != 0 && triggers[i] == 1) // ON_EQUIP
            AddSpellStatAuras(acc, static_cast<int>(ids[i]), sign);
}

} // namespace

void Init(Accum *acc) {
    for (int i = 0; i < kCount; ++i) {
        acc[i].key = kKeys[i];
        acc[i].value = 0;
    }
}

void AddByKey(Accum *acc, const char *key, long delta) {
    if (key == nullptr || delta == 0)
        return;
    for (int i = 0; i < kCount; ++i)
        if (std::strcmp(acc[i].key, key) == 0) {
            acc[i].value += delta;
            return;
        }
}

long Value(const Accum *acc, const char *key) {
    if (acc == nullptr || key == nullptr)
        return 0;
    for (int i = 0; i < kCount; ++i)
        if (std::strcmp(acc[i].key, key) == 0)
            return acc[i].value;
    return 0;
}

double ComputeDPS(const uint8_t *record) {
    if (record == nullptr)
        return 0.0;
    const uint32_t delayMs = Game::Read<uint32_t>(
        record, Offsets::OFF_ITEMSTATS_DELAY);
    if (delayMs == 0)
        return 0.0;
    auto *dmgMin = Game::Ptr<const float>(
        record, Offsets::OFF_ITEMSTATS_DAMAGE_MIN);
    auto *dmgMax = Game::Ptr<const float>(
        record, Offsets::OFF_ITEMSTATS_DAMAGE_MAX);
    double avgTotal = 0.0;
    for (int i = 0; i < Offsets::ITEMSTATS_DAMAGE_SLOT_COUNT; ++i)
        avgTotal += (static_cast<double>(dmgMin[i]) +
                     static_cast<double>(dmgMax[i])) * 0.5;
    if (avgTotal <= 0.0)
        return 0.0;
    return avgTotal / (static_cast<double>(delayMs) / 1000.0);
}

void ApplyEnchant(Accum *acc, uint32_t enchantID, long sign) {
    if (enchantID == 0)
        return;
    const uint8_t *en = DBC::Record(Offsets::VAR_SPELLITEMENCHANT_RECORDS,
                                    Offsets::VAR_SPELLITEMENCHANT_COUNT, enchantID);
    if (en == nullptr)
        return;
    auto type = Game::Ptr<const int32_t>(en, Offsets::OFF_SPELLITEMENCHANT_TYPE);
    auto amount = Game::Ptr<const int32_t>(en, Offsets::OFF_SPELLITEMENCHANT_AMOUNT);
    auto arg = Game::Ptr<const int32_t>(en, Offsets::OFF_SPELLITEMENCHANT_ARG);
    for (int i = 0; i < 3; ++i) { // SpellItemEnchantment has 3 effect slots
        switch (type[i]) {
        case 3: // equip spell — stat/resist value lives in the spell
            AddSpellStatAuras(acc, arg[i], sign);
            break;
        case 4: // resistance — arg = school index (0 = armor)
            AddByKey(acc, ResistKey(arg[i]), sign * static_cast<long>(amount[i]));
            break;
        case 5: // direct stat — arg = ItemModType, amount = value
            AddByKey(acc, StatKeyForItemModType(static_cast<uint32_t>(arg[i])),
                     sign * static_cast<long>(amount[i]));
            break;
        default: // 1 proc / 2 weapon-damage / 6 totem / 7 use spell
            break;
        }
    }
}

void ApplyRandomSuffix(Accum *acc, int suffixID, long sign) {
    if (suffixID <= 0)
        return;
    const uint8_t *rp = DBC::Record(Offsets::VAR_ITEMRANDOMPROP_RECORDS,
                                    Offsets::VAR_ITEMRANDOMPROP_COUNT,
                                    static_cast<uint32_t>(suffixID));
    if (rp == nullptr)
        return;
    auto enchants = Game::Ptr<const uint32_t>(
        rp, Offsets::OFF_ITEMRANDOMPROP_ENCHANT);
    for (int s = 0; s < Offsets::ITEMRANDOMPROP_ENCHANT_SLOT_COUNT; ++s)
        ApplyEnchant(acc, enchants[s], sign);
}

void AccumulateRecord(Accum *acc, const uint8_t *record, long sign) {
    if (record == nullptr)
        return;
    auto *types = Game::Ptr<const uint32_t>(
        record, Offsets::OFF_ITEMSTATS_STAT_TYPE);
    auto *values = Game::Ptr<const int32_t>(
        record, Offsets::OFF_ITEMSTATS_STAT_VALUE);
    for (int i = 0; i < Offsets::ITEMSTATS_STAT_SLOT_COUNT; ++i)
        AddByKey(acc, StatKeyForItemModType(types[i]),
                 sign * static_cast<long>(values[i]));

    AddByKey(acc, "RESISTANCE0_NAME", sign * U32(record, Offsets::OFF_ITEMSTATS_ARMOR));
    AddByKey(acc, "RESISTANCE1_NAME", sign * U32(record, Offsets::OFF_ITEMSTATS_RES_HOLY));
    AddByKey(acc, "RESISTANCE2_NAME", sign * U32(record, Offsets::OFF_ITEMSTATS_RES_FIRE));
    AddByKey(acc, "RESISTANCE3_NAME", sign * U32(record, Offsets::OFF_ITEMSTATS_RES_NATURE));
    AddByKey(acc, "RESISTANCE4_NAME", sign * U32(record, Offsets::OFF_ITEMSTATS_RES_FROST));
    AddByKey(acc, "RESISTANCE5_NAME", sign * U32(record, Offsets::OFF_ITEMSTATS_RES_SHADOW));
    AddByKey(acc, "RESISTANCE6_NAME", sign * U32(record, Offsets::OFF_ITEMSTATS_RES_ARCANE));

    // Shield block value is a stored field, not a stat slot or aura.
    AddByKey(acc, "ITEM_MOD_BLOCK_VALUE", sign * U32(record, Offsets::OFF_ITEMSTATS_BLOCK));

    AddEquipSpellStats(acc, record, sign);
}

int ParseRandomSuffixFromLink(void *L, int idx) {
    if (Game::Lua::Type(L, idx) != Game::Lua::TYPE_STRING)
        return 0;
    const char *s = Game::Lua::ToString(L, idx);
    if (s == nullptr)
        return 0;
    const char *m = std::strstr(s, "item:");
    if (m == nullptr)
        return 0;
    m += 5; // start of field 0 (itemID)
    // Advance to the start of field 2 (past two ':' separators), stopping
    // at the end of the hyperlink payload if the link is too short.
    int colons = 0;
    while (*m != '\0' && *m != '|') {
        if (*m == ':') {
            if (++colons == 2) {
                ++m;
                break;
            }
        }
        ++m;
    }
    if (colons < 2)
        return 0;
    return std::atoi(m);
}

} // namespace Item::StatAccum
