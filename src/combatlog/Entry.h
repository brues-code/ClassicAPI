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

// The INFO_MISS value as 4.3.4's g_spellMissStrings names it.
const char *MissName(int missType);

// The INFO_ENVIRONMENTAL value as retail's CLEU and log file name it.
const char *EnvironmentalName(int type);

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

// COMBATLOG_OBJECT_* — 4.3.4's GetCombatLogObjectFlags, the bits of an
// entry's sourceFlags / destFlags.
namespace ObjectFlag {
constexpr uint32_t AFFILIATION_MINE = 0x1;
constexpr uint32_t AFFILIATION_PARTY = 0x2;
constexpr uint32_t AFFILIATION_RAID = 0x4;
constexpr uint32_t AFFILIATION_OUTSIDER = 0x8;
constexpr uint32_t AFFILIATION_MASK = 0xF;
// The player, a party or raid member, or a unit one of them controls.
constexpr uint32_t AFFILIATION_GROUP = AFFILIATION_MINE | AFFILIATION_PARTY | AFFILIATION_RAID;
constexpr uint32_t REACTION_FRIENDLY = 0x10;
constexpr uint32_t REACTION_NEUTRAL = 0x20;
constexpr uint32_t REACTION_HOSTILE = 0x40;
constexpr uint32_t REACTION_MASK = 0xF0;
// A unit the player can fight: hostile or neutral (a neutral creature the
// group attacks is its enemy all the same).
constexpr uint32_t REACTION_ENEMY = REACTION_NEUTRAL | REACTION_HOSTILE;
constexpr uint32_t CONTROL_PLAYER = 0x100;
constexpr uint32_t CONTROL_NPC = 0x200;
constexpr uint32_t TYPE_PLAYER = 0x400;
constexpr uint32_t TYPE_NPC = 0x800;
constexpr uint32_t TYPE_PET = 0x1000;
constexpr uint32_t TYPE_GUARDIAN = 0x2000;
constexpr uint32_t TYPE_OBJECT = 0x4000;
constexpr uint32_t TARGET = 0x10000;
constexpr uint32_t FOCUS = 0x20000;
constexpr uint32_t NONE = 0x80000000;
} // namespace ObjectFlag

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

// 4.3.4's rule for a spell that writes nothing: no record, Attributes 0x80 or
// 0x100, or no name in the client's locale.
bool Hidden(const uint8_t *spell);

// Whether the log writes an aura's arrival and loss (SPELL_AURA_APPLIED,
// _REMOVED, _REFRESH): 4.3.4's UnitCombatLogAuraAddedOrRemoved writes none
// for a hidden or a passive spell.
bool LogsAura(const uint8_t *spell);

// Whether the log writes an aura's stack changes (SPELL_AURA_APPLIED_DOSE,
// _REMOVED_DOSE): 4.3.4's UnitCombatLogAuraModifiedApplications writes them
// for a spell that stacks and is not hidden, passive or not.
bool LogsAuraStacks(const uint8_t *spell);

// AttributesEx3 0x8000: the spell is a ranged attack (Auto Shot, Shoot,
// Throw), logged as RANGE_* instead of SPELL_*.
bool Ranged(const uint8_t *spell);

// A school index (Spell.dbc School, the packets' school) as a school mask.
uint32_t SchoolMask(uint32_t school);

// The spell's school as a mask (4.3.4's SpellRec m_schoolMask), 0 for nullptr.
uint32_t SpellSchoolMask(const uint8_t *spell);

// The GUID the object answers to, the one its flags take their affiliation
// from: a unit's charmer or creator (Unit::Identity::OwnerGuid), a game
// object's creator, and for a minion's minion (a totem's totem) its master's
// controller. The object itself when nothing controls it; 0 for a game object
// nobody created.
uint64_t Controller(uint64_t guid);

// The object's name as an entry names it: the combat log namer's steps, else
// UNKNOWNOBJECT.
std::string NameOf(uint64_t guid);

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
