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

// Spell effects — 4.3.4's CombatLog::HandleSpellLogExecute and
// Packet_SMSG_SPELLINSTAKILLLOG, read from 1.12's bodies of the same messages
// through Net::PacketDispatch.
//
// 0x24c SPELLLOGEXECUTE (handler 0x005E7F90): packed caster, spell, effect
// count; per effect its id and a target count, then per target what that
// effect carries. The layout is the server's (tortoise-wow
// Spell::SendLogExecute), which 1.12's reader follows for every effect it
// knows:
//   8 (power drain)        u64 target, amount, power, float multiplier
//   19 (extra attacks)     u64 target, count
//   24 (create item)       item
//   68 (interrupt)         u64 target, interrupted spell
//   101 (feed pet)         item
//   111 (durability)       u64 target, item, slot (both -1: every item)
//   1, 18, 33, 38, 59, 63, 69, 79, 91, 102, 108, 113, 114, 116, 125, 126
//                          u64 target
//   28, 41, 42, 50, 56, 73, 74, 76, 87-90, 97, 104-107, 112 (the summons)
//                          u64 summoned — the one block 1.12's reader skips:
//                          it reads nothing for these effects and leaves the
//                          GUID in the buffer, so the engine's own log never
//                          names a summon
//   any other              nothing (the server sends no message for it)
// 4.3.4 logs drains and leeches, resurrections (18, 113), extra attacks,
// interrupts, durability losses and summons (SPELL_SUMMON for a unit,
// SPELL_CREATE for a game object) from these.
//
// 0x32f SPELLINSTAKILLLOG (0x005E85A0): the victim (u64) and the spell. 1.12
// names no caster; the server sends it from the effect's handler, after the
// cast's SMSG_SPELL_GO in the same tick, so the source is the caster of the
// newest cast of that spell at the victim (RecentCasts), else none.

#include "combatlog/Entry.h"

#include "Game.h"
#include "Offsets.h"
#include "combatlog/RecentCasts.h"
#include "guid/Guid.h"
#include "net/PacketDispatch.h"

#include <cstdint>

namespace CombatLog::Execute {

namespace {

using Net::CDataStore;
using Net::Read;
using Net::ReadPackedGuid;

constexpr uint32_t SMSG_SPELLLOGEXECUTE = 0x24c;
constexpr uint32_t SMSG_SPELLINSTAKILLLOG = 0x32f;

enum Effect : uint32_t {
    EFFECT_INSTAKILL = 1,
    EFFECT_POWER_DRAIN = 8,
    EFFECT_RESURRECT = 18,
    EFFECT_ADD_EXTRA_ATTACKS = 19,
    EFFECT_CREATE_ITEM = 24,
    EFFECT_SUMMON = 28,
    EFFECT_OPEN_LOCK = 33,
    EFFECT_DISPEL = 38,
    EFFECT_SUMMON_WILD = 41,
    EFFECT_SUMMON_GUARDIAN = 42,
    EFFECT_TRANS_DOOR = 50,
    EFFECT_SUMMON_PET = 56,
    EFFECT_OPEN_LOCK_ITEM = 59,
    EFFECT_THREAT = 63,
    EFFECT_INTERRUPT_CAST = 68,
    EFFECT_DISTRACT = 69,
    EFFECT_SUMMON_POSSESSED = 73,
    EFFECT_SUMMON_TOTEM = 74,
    EFFECT_SUMMON_OBJECT_WILD = 76,
    EFFECT_SANCTUARY = 79,
    EFFECT_SUMMON_TOTEM_SLOT1 = 87,
    EFFECT_SUMMON_TOTEM_SLOT2 = 88,
    EFFECT_SUMMON_TOTEM_SLOT3 = 89,
    EFFECT_SUMMON_TOTEM_SLOT4 = 90,
    EFFECT_THREAT_ALL = 91,
    EFFECT_SUMMON_CRITTER = 97,
    EFFECT_FEED_PET = 101,
    EFFECT_DISMISS_PET = 102,
    EFFECT_SUMMON_OBJECT_SLOT1 = 104,
    EFFECT_SUMMON_OBJECT_SLOT2 = 105,
    EFFECT_SUMMON_OBJECT_SLOT3 = 106,
    EFFECT_SUMMON_OBJECT_SLOT4 = 107,
    EFFECT_DISPEL_MECHANIC = 108,
    EFFECT_DURABILITY_DAMAGE = 111,
    EFFECT_SUMMON_DEMON = 112,
    EFFECT_RESURRECT_NEW = 113,
    EFFECT_ATTACK_ME = 114,
    EFFECT_SKIN_PLAYER_CORPSE = 116,
    EFFECT_MODIFY_THREAT_PERCENT = 125,
    EFFECT_126 = 126,
};

// AttributesEx3 0x2000000: a drain 4.3.4 logs as periodic.
constexpr uint32_t ATTR_EX3_TREAT_AS_PERIODIC = 0x2000000;

bool Overrun(const CDataStore *p) { return p->m_read > p->m_size; }

// UnitCombatLogPowerDrain.
void PowerDrainEffect(uint64_t caster, uint64_t target, const uint8_t *spell, int amount,
                      int powerType, float multiplier) {
    if (amount == 0 || Hidden(spell))
        return;
    const bool periodic = (Game::Read<uint32_t>(spell, Offsets::OFF_SPELL_RECORD_ATTRIBUTES_EX3) &
                           ATTR_EX3_TREAT_AS_PERIODIC) != 0;
    // A drain (effect 8) gives the caster `multiplier` of it: a leech.
    const int extra = static_cast<int>(static_cast<float>(amount) * multiplier);
    if (extra != 0)
        PowerDrain(caster, target, periodic ? SPELL_PERIODIC_LEECH : SPELL_LEECH, spell, amount,
                   powerType, extra);
    else
        PowerDrain(caster, target, periodic ? SPELL_PERIODIC_DRAIN : SPELL_DRAIN, spell, amount,
                   powerType, 0);
}

void Simple(uint64_t caster, uint64_t target, int event, const uint8_t *spell) {
    if (Hidden(spell))
        return;
    Entry e = Begin(caster, target, event, spell);
    Queue(e);
}

// UnitCombatLogSummon: a game object or dynamic object is SPELL_CREATE, a
// unit SPELL_SUMMON. A portal writes nothing: every portal spell has
// Attributes 0x100 (verified in Spell.dbc: 11417, 11418, Turtle's 49362), the
// same bit that keeps it out of 1.12's own cast lines; Fishing does not.
void Summon(uint64_t caster, uint64_t summoned, const uint8_t *spell) {
    if (summoned == 0)
        return;
    const bool object = Guid::IsGameObject(summoned) || Guid::IsDynamicObject(summoned);
    Simple(caster, summoned, object ? SPELL_CREATE : SPELL_SUMMON, spell);
}

// UnitCombatLogExtraAttacks.
void ExtraAttacks(uint64_t caster, uint64_t target, const uint8_t *spell, int count) {
    if (count == 0 || Hidden(spell))
        return;
    Entry e = Begin(caster, target, SPELL_EXTRA_ATTACKS, spell);
    e.info |= INFO_COUNT;
    e.v[0] = count;
    Queue(e);
}

// UnitCombatLogInterrupt.
void Interrupt(uint64_t caster, uint64_t target, const uint8_t *spell, int interruptedId) {
    const uint8_t *interrupted = SpellRecord(interruptedId);
    if (Hidden(spell) || Hidden(interrupted))
        return;
    Entry e = Begin(caster, target, SPELL_INTERRUPT, spell);
    e.info |= INFO_EXTRA_SPELL;
    e.v[0] = interruptedId;
    Queue(e);
}

// UnitCombatLogDurabilityDamage / DurabilityDamageAll.
void Durability(uint64_t caster, uint64_t target, const uint8_t *spell, int itemId, int slot) {
    if (Hidden(spell))
        return;
    if (itemId == -1 && slot == -1) {
        Entry e = Begin(caster, target, SPELL_DURABILITY_DAMAGE_ALL, spell);
        Queue(e);
        return;
    }
    Entry e = Begin(caster, target, SPELL_DURABILITY_DAMAGE, spell);
    SetItem(e, itemId);
    Queue(e);
}

// One target of one effect: reads what 1.12 reads, logs what 4.3.4 logs.
// False for an effect that carries nothing, whose other targets read nothing
// either.
bool Target(CDataStore *p, uint32_t effect, uint64_t caster, const uint8_t *spell) {
    switch (effect) {
    case EFFECT_POWER_DRAIN: {
        const uint64_t target = Read<uint64_t>(p);
        const int amount = Read<int32_t>(p);
        const int powerType = Read<int32_t>(p);
        const float multiplier = Read<float>(p);
        if (!Overrun(p))
            PowerDrainEffect(caster, target, spell, amount, powerType, multiplier);
        break;
    }
    case EFFECT_ADD_EXTRA_ATTACKS: {
        const uint64_t target = Read<uint64_t>(p);
        const int count = Read<int32_t>(p);
        if (!Overrun(p))
            ExtraAttacks(caster, target, spell, count);
        break;
    }
    case EFFECT_INTERRUPT_CAST: {
        const uint64_t target = Read<uint64_t>(p);
        const int interrupted = Read<int32_t>(p);
        if (!Overrun(p))
            Interrupt(caster, target, spell, interrupted);
        break;
    }
    case EFFECT_DURABILITY_DAMAGE: {
        const uint64_t target = Read<uint64_t>(p);
        const int itemId = Read<int32_t>(p);
        const int slot = Read<int32_t>(p);
        if (!Overrun(p))
            Durability(caster, target, spell, itemId, slot);
        break;
    }
    case EFFECT_RESURRECT:
    case EFFECT_RESURRECT_NEW: {
        const uint64_t target = Read<uint64_t>(p);
        if (!Overrun(p))
            Simple(caster, target, SPELL_RESURRECT, spell);
        break;
    }
    case EFFECT_SUMMON:
    case EFFECT_SUMMON_WILD:
    case EFFECT_SUMMON_GUARDIAN:
    case EFFECT_TRANS_DOOR:
    case EFFECT_SUMMON_PET:
    case EFFECT_SUMMON_POSSESSED:
    case EFFECT_SUMMON_TOTEM:
    case EFFECT_SUMMON_OBJECT_WILD:
    case EFFECT_SUMMON_TOTEM_SLOT1:
    case EFFECT_SUMMON_TOTEM_SLOT2:
    case EFFECT_SUMMON_TOTEM_SLOT3:
    case EFFECT_SUMMON_TOTEM_SLOT4:
    case EFFECT_SUMMON_CRITTER:
    case EFFECT_SUMMON_OBJECT_SLOT1:
    case EFFECT_SUMMON_OBJECT_SLOT2:
    case EFFECT_SUMMON_OBJECT_SLOT3:
    case EFFECT_SUMMON_OBJECT_SLOT4:
    case EFFECT_SUMMON_DEMON: {
        const uint64_t summoned = Read<uint64_t>(p);
        if (!Overrun(p))
            Summon(caster, summoned, spell);
        break;
    }
    case EFFECT_CREATE_ITEM:
    case EFFECT_FEED_PET:
        Read<uint32_t>(p);
        break;
    case EFFECT_INSTAKILL:
    case EFFECT_OPEN_LOCK:
    case EFFECT_DISPEL:
    case EFFECT_OPEN_LOCK_ITEM:
    case EFFECT_THREAT:
    case EFFECT_DISTRACT:
    case EFFECT_SANCTUARY:
    case EFFECT_THREAT_ALL:
    case EFFECT_DISMISS_PET:
    case EFFECT_DISPEL_MECHANIC:
    case EFFECT_ATTACK_ME:
    case EFFECT_SKIN_PLAYER_CORPSE:
    case EFFECT_MODIFY_THREAT_PERCENT:
    case EFFECT_126:
        Read<uint64_t>(p);
        break;
    default:
        return false;
    }
    return true;
}

void SpellLogExecute(CDataStore *p) {
    const uint64_t caster = ReadPackedGuid(p);
    const int spellId = Read<int32_t>(p);
    const uint32_t effects = Read<uint32_t>(p);
    const uint8_t *spell = SpellRecord(spellId);
    if (Overrun(p) || spell == nullptr)
        return;
    for (uint32_t i = 0; i < effects && !Overrun(p); ++i) {
        const uint32_t effect = Read<uint32_t>(p);
        const uint32_t targets = Read<uint32_t>(p);
        for (uint32_t j = 0; j < targets && !Overrun(p); ++j) {
            if (!Target(p, effect, caster, spell))
                break;
        }
    }
}

// SMSG_SPELLINSTAKILLLOG → UnitCombatLogInstakill.
void InstakillLog(CDataStore *p) {
    const uint64_t victim = Read<uint64_t>(p);
    const int spellId = Read<int32_t>(p);
    if (Overrun(p))
        return;
    const uint64_t caster = RecentCasts::CasterOf(static_cast<uint32_t>(spellId), victim);
    Simple(caster, victim, SPELL_INSTAKILL, SpellRecord(spellId));
}

void OnPacket(uint32_t opcode, CDataStore *p) {
    if (opcode == SMSG_SPELLLOGEXECUTE)
        SpellLogExecute(p);
    else if (opcode == SMSG_SPELLINSTAKILLLOG)
        InstakillLog(p);
}

const Net::PacketDispatch::AutoSubscribe _sub{&OnPacket};

} // namespace

} // namespace CombatLog::Execute
