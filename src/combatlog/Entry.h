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

// 4.3.4's combat log entry (UnitCombatLog_C.cpp `CombatLogEntry`): who did
// what to whom, built where the engine learns of it and sent as
// COMBAT_LOG_EVENT / COMBAT_LOG_EVENT_UNFILTERED in 4.3.4's argument layout.
// The modules that learn of events (the other files in combatlog/) build an
// entry with `Begin`, fill its suffix, and `Queue` it.

#include <cstdint>
#include <string>

namespace CombatLog {

// 4.3.4's event index (s_combatLogEventNames); `Name` gives the string.
enum Subevent : int {
    ENVIRONMENTAL_DAMAGE = 0,
    SWING_DAMAGE = 1,
    SWING_MISSED = 2,
    RANGE_DAMAGE = 3,
    RANGE_MISSED = 4,
    SPELL_CAST_START = 5,
    SPELL_CAST_SUCCESS = 6,
    SPELL_CAST_FAILED = 7,
    SPELL_MISSED = 8,
    SPELL_DAMAGE = 9,
    SPELL_HEAL = 10,
    SPELL_ENERGIZE = 11,
    SPELL_DRAIN = 12,
    SPELL_LEECH = 13,
    SPELL_INSTAKILL = 14,
    SPELL_SUMMON = 15,
    SPELL_CREATE = 16,
    SPELL_INTERRUPT = 17,
    SPELL_EXTRA_ATTACKS = 18,
    SPELL_DURABILITY_DAMAGE = 19,
    SPELL_DURABILITY_DAMAGE_ALL = 20,
    SPELL_AURA_APPLIED = 21,
    SPELL_AURA_APPLIED_DOSE = 22,
    SPELL_AURA_REMOVED_DOSE = 23,
    SPELL_AURA_REMOVED = 24,
    SPELL_AURA_REFRESH = 25,
    SPELL_DISPEL = 26,
    SPELL_PERIODIC_MISSED = 33,
    SPELL_PERIODIC_DAMAGE = 34,
    SPELL_PERIODIC_HEAL = 35,
    SPELL_PERIODIC_ENERGIZE = 36,
    SPELL_PERIODIC_DRAIN = 37,
    SPELL_PERIODIC_LEECH = 38,
    SPELL_DISPEL_FAILED = 39,
    DAMAGE_SHIELD = 40,
    DAMAGE_SHIELD_MISSED = 41,
    DAMAGE_SPLIT = 42,
    PARTY_KILL = 43,
    UNIT_DIED = 44,
    UNIT_DESTROYED = 45,
    SPELL_RESURRECT = 46,
    EVENT_COUNT = 50,
};

const char *Name(int event);

// 4.3.4's m_infoFlags: which suffix blocks the event carries, pushed in
// this order after the spell prefix.
enum Info : uint32_t {
    INFO_COUNT = 0x1,         // amount (extra attacks)
    INFO_TEXT = 0x2,          // failedType
    INFO_MISS = 0x4,          // missType, isOffHand
    INFO_ENVIRONMENTAL = 0x8, // environmentalType
    INFO_DAMAGE = 0x10,       // amount … isOffHand (10 values)
    INFO_MISS_ABSORBED = 0x2000,
    INFO_MISS_RESISTED = 0x4000,
    INFO_MISS_BLOCKED = 0x8000,
    INFO_HEAL = 0x20,     // amount, overhealing, absorbed, critical
    INFO_ENERGIZE = 0x40, // amount, powerType, alternatePowerType
    INFO_DRAIN = 0x80,    // amount, powerType, extraAmount, alternatePowerType
    INFO_EXTRA_SPELL = 0x100, // extraSpellId, extraSpellName, extraSchool
    INFO_ITEM = 0x200,        // itemId, itemName
    INFO_AURA_TYPE = 0x400,   // auraType
    INFO_AURA_AMOUNT = 0x800, // amount (stacks)
};

// 4.3.4's miss types (g_spellMissStrings), in the server's numbering.
enum MissType : int {
    MISS_NONE = 0,
    MISS_MISS = 1,
    MISS_RESIST = 2,
    MISS_DODGE = 3,
    MISS_PARRY = 4,
    MISS_BLOCK = 5,
    MISS_EVADE = 6,
    MISS_IMMUNE = 7,
    MISS_DEFLECT = 9,
    MISS_ABSORB = 10,
    MISS_REFLECT = 11,
    MISS_COUNT = 12,
};

struct Entry {
    uint32_t timeMs = 0;
    int event = -1;
    bool hideCaster = true;
    uint64_t source = 0;
    bool sourceNamed = false;
    std::string sourceName;
    uint32_t sourceFlags = 0x80000000;
    uint32_t sourceRaidFlags = 0x80000000;
    uint64_t dest = 0;
    bool destNamed = false;
    std::string destName;
    uint32_t destFlags = 0x80000000;
    uint32_t destRaidFlags = 0x80000000;
    // The spell prefix (spellId, spellName, spellSchool). 4.3.4 pushes it
    // when the entry has a spell; `spellPrefix` also keeps it, with spellId 0
    // and a nil name, for an event 1.12 sends without its spell (a damage
    // shield), so the suffix stays in place. `spellSchool` is used only then.
    bool spellPrefix = false;
    int spellId = 0;
    uint32_t spellSchool = 0;
    uint32_t info = 0;
    int v[7] = {}; // 4.3.4's m_info.m_values, same slots
    std::string text;     // INFO_TEXT
    bool itemNamed = false; // INFO_ITEM: the item's name, when the cache has it
    std::string itemName;
    bool harmful = false; // INFO_AURA_TYPE: DEBUFF, else BUFF
    bool critical = false;
    bool glancing = false;
    bool crushing = false;
    bool offHand = false;
};

// The Spell.dbc record of `spellId`, or nullptr.
const uint8_t *SpellRecord(int spellId);

// The spell's name in the client's locale, or nullptr when it has none.
const char *SpellName(const uint8_t *spell);

// 4.3.4's rule for a spell that writes nothing: no record, Attributes 0x80 or
// 0x100, or no name in the client's locale.
bool Hidden(const uint8_t *spell);

// AttributesEx3 0x8000: the spell is a ranged attack (Auto Shot, Shoot,
// Throw), logged as RANGE_* instead of SPELL_*.
bool Ranged(const uint8_t *spell);

// A school index (Spell.dbc School, the packets' school) as a school mask.
uint32_t SchoolMask(uint32_t school);

// Whether one of the spell's three effects applies the aura `auraName`
// (EffectApplyAuraName).
bool HasAuraEffect(const uint8_t *spell, int auraName);

// GetCombatLogEntry: an entry for `event` from `source` to `dest` (either may
// be 0), with the spell prefix when `spell` is a record.
Entry Begin(uint64_t source, uint64_t dest, int event, const uint8_t *spell);

// CombatLogEntry::SetMissInfo: the miss suffix, with the amount in the slot
// its type uses.
void SetMissInfo(Entry &entry, int missType, int amount, bool offHand);

// CombatLogEntry::SetExecuteItem: the item suffix, named from the item cache
// (a miss asks the server, and the name is nil this time).
void SetItem(Entry &entry, int itemId);

// A power amount in the units the client shows (Spell_C_GetPowerDisplayMod).
int DisplayAmount(int amount, int powerType);

// AddPowerDrainEntry: a drain or leech of `amount` (and `extra` to the
// caster), shown in the power's display units.
void PowerDrain(uint64_t source, uint64_t dest, int event, const uint8_t *spell, int amount,
                int powerType, int extra);

// The most values PushEvent makes: 11 + 3 + 10 + 3 (a damage event).
constexpr int MAX_EVENT_VALUES = 32;

// CombatLogEntry::PushEvent: pushes the entry's values in 4.3.4's layout and
// returns their count. The stack must have room for MAX_EVENT_VALUES.
int PushEvent(void *L, const Entry &entry);

// QueueCombatLogEntry + SendCombatLogEvents: stamps the entry, keeps it and
// sends it.
void Queue(Entry &entry);

} // namespace CombatLog
