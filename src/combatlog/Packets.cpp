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

// The combat log messages — 4.3.4's CombatLog::Handle* parsers and the
// UnitCombatLog* builders they call, read from 1.12's bodies of the same
// messages through Net::PacketDispatch (no hook on the handlers, which
// nampower detours).
//
// 1.12's bodies (each handler disassembled; benilla-re objectlayer/
// playercombat, unitcombat):
//   0x14a ATTACKERSTATEUPDATE   hit info, packed attacker, packed victim,
//                               total damage, u8 n, n × {school, float,
//                               damage, absorbed, resisted}, victim state,
//                               u32, melee spell, blocked
//   0x250 SPELLNONMELEEDAMAGELOG packed target, packed caster, spell,
//                               damage, u8 school, absorbed, resisted,
//                               u8 periodic, u8, blocked, hit flags, u8 debug
//   0x150 SPELLHEALLOG          packed target, packed caster, spell, amount,
//                               u8 critical
//   0x151 SPELLENERGIZELOG      packed target, packed caster, spell, power,
//                               amount
//   0x24e PERIODICAURALOG       packed target, packed caster, spell, count,
//                               count × {aura type, values by type}
//   0x24f SPELLDAMAGESHIELD     shield owner, victim (u64), damage, school
//   0x1fc ENVIRONMENTALDAMAGELOG victim (u64), u8 type, damage, absorbed,
//                               resisted
// What 1.12 does not send is filled as 4.3.4 fills "none": overkill and
// overhealing 0, healing absorbed 0, no periodic critical. Schools are
// indexes here and masks in the event. A damage shield names no spell, so
// its spell prefix is spellId 0 with the damage school.
//
// Spell misses do not come from here: they reach the log from three places
// (the SPELL_GO miss list, a missile's arrival, SPELLLOGMISS), all through
// one function — see combatlog/Miss.cpp.

#include "combatlog/Entry.h"

#include "Game.h"
#include "Offsets.h"
#include "net/PacketDispatch.h"
#include "object/Resolve.h"

#include <cstdint>

namespace CombatLog::Packets {

namespace {

using Net::CDataStore;
using Net::Read;
using Net::ReadPackedGuid;

constexpr uint32_t SMSG_SPELLHEALLOG = 0x150;
constexpr uint32_t SMSG_SPELLENERGIZELOG = 0x151;
constexpr uint32_t SMSG_ATTACKERSTATEUPDATE = 0x14a;
constexpr uint32_t SMSG_ENVIRONMENTALDAMAGELOG = 0x1fc;
constexpr uint32_t SMSG_PERIODICAURALOG = 0x24e;
constexpr uint32_t SMSG_SPELLDAMAGESHIELD = 0x24f;
constexpr uint32_t SMSG_SPELLNONMELEEDAMAGELOG = 0x250;

// 1.12 melee hit info (the swing line's own tests, 0x00629B60 / 0x00629D30).
constexpr uint32_t HITINFO_LEFTSWING = 0x4;
constexpr uint32_t HITINFO_MISS = 0x10;
constexpr uint32_t HITINFO_ABSORB = 0x20;
constexpr uint32_t HITINFO_RESIST = 0x40;
constexpr uint32_t HITINFO_CRITICAL = 0x80;
constexpr uint32_t HITINFO_GLANCING = 0x4000;
constexpr uint32_t HITINFO_CRUSHING = 0x8000;
// SPELLNONMELEEDAMAGELOG hit flags (0x005E85E0).
constexpr uint32_t SPELL_HIT_CRITICAL = 0x2;
constexpr uint32_t SPELL_HIT_SPLIT = 0x8;

enum VictimState : uint32_t {
    VICTIMSTATE_NORMAL = 1,
    VICTIMSTATE_DODGE = 2,
    VICTIMSTATE_PARRY = 3,
    VICTIMSTATE_BLOCKS = 5,
    VICTIMSTATE_EVADES = 6,
    VICTIMSTATE_IMMUNE = 7,
    VICTIMSTATE_DEFLECTS = 8,
};

// Periodic aura types (SpellAuraNames) the log reads.
constexpr uint32_t AURA_PERIODIC_DAMAGE = 3;
constexpr uint32_t AURA_PERIODIC_HEAL = 8;
constexpr uint32_t AURA_OBS_MOD_HEALTH = 20;
constexpr uint32_t AURA_OBS_MOD_MANA = 21;
constexpr uint32_t AURA_PERIODIC_ENERGIZE = 24;
constexpr uint32_t AURA_PERIODIC_MANA_LEECH = 64;
constexpr uint32_t AURA_PERIODIC_DAMAGE_PERCENT = 89;

constexpr int kMaxDamageParts = 5;

bool Overrun(const CDataStore *p) { return p->m_read > p->m_size; }

void *Unit(uint64_t guid) { return Object::ByGuid(Offsets::TYPEMASK_UNIT, guid); }

bool PlayerControlled(const void *unit) {
    const auto *fields = Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_DESCRIPTOR);
    return fields != nullptr && (Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FLAGS) &
                                 Offsets::UNIT_FLAG_PLAYER_CONTROLLED) != 0;
}

int DisplayAmount(int amount, int powerType) {
    using Divisor_t = int(__fastcall *)(int powerType);
    const int divisor = reinterpret_cast<Divisor_t>(
        static_cast<uintptr_t>(Offsets::FUN_POWER_DISPLAY_DIVISOR))(powerType);
    return divisor != 0 ? amount / divisor : amount;
}

// SendDamageEvent.
void SendDamage(uint64_t source, uint64_t dest, int event, const uint8_t *spell, int damage,
                uint32_t schoolMask, int resisted, int blocked, int absorbed, bool critical,
                bool glancing, bool crushing, bool offHand) {
    Entry e = Begin(source, dest, event, spell);
    e.info |= INFO_DAMAGE;
    e.v[1] = damage;
    e.v[2] = 0; // overkill: not sent
    e.v[3] = static_cast<int>(schoolMask);
    e.v[4] = absorbed;
    e.v[5] = resisted;
    e.v[6] = blocked;
    e.critical = critical;
    e.glancing = glancing;
    e.crushing = crushing;
    e.offHand = offHand;
    Queue(e);
}

void SendMiss(uint64_t source, uint64_t dest, int event, const uint8_t *spell, int missType,
              int amount, bool offHand) {
    Entry e = Begin(source, dest, event, spell);
    SetMissInfo(e, missType, amount, offHand);
    Queue(e);
}

// UnitCombatLogSpellDamage.
void SpellDamage(uint64_t source, uint64_t dest, const uint8_t *spell, int damage,
                 uint32_t school, int absorbed, int resisted, int blocked, bool critical,
                 bool periodic) {
    if (Hidden(spell))
        return;
    if (damage != 0) {
        const int event = periodic ? SPELL_PERIODIC_DAMAGE
                                   : (Ranged(spell) ? RANGE_DAMAGE : SPELL_DAMAGE);
        SendDamage(source, dest, event, spell, damage, SchoolMask(school), resisted, blocked,
                   absorbed, critical, false, false, false);
        return;
    }
    const int event =
        periodic ? SPELL_PERIODIC_MISSED : (Ranged(spell) ? RANGE_MISSED : SPELL_MISSED);
    if (absorbed != 0)
        SendMiss(source, dest, event, spell, MISS_ABSORB, absorbed, false);
    else if (blocked != 0)
        SendMiss(source, dest, event, spell, MISS_BLOCK, blocked, false);
    else if (resisted != 0)
        SendMiss(source, dest, event, spell, MISS_RESIST, resisted, false);
}

// UnitCombatLogHeal.
void Heal(uint64_t caster, uint64_t target, const uint8_t *spell, int amount, bool critical,
          bool periodic) {
    if (amount == 0 || Hidden(spell))
        return;
    Entry e = Begin(caster, target, periodic ? SPELL_PERIODIC_HEAL : SPELL_HEAL, spell);
    e.info |= INFO_HEAL;
    e.v[0] = amount;
    e.v[1] = 0; // overhealing: not sent
    e.v[2] = 0; // absorbed: 1.12 has no healing absorbs
    e.critical = critical;
    Queue(e);
}

// UnitCombatLogEnergize + SendEnergizeEvent.
void Energize(uint64_t caster, uint64_t target, const uint8_t *spell, int powerType, int amount,
              bool periodic) {
    if (amount == 0 || Hidden(spell))
        return;
    Entry e = Begin(caster, target, periodic ? SPELL_PERIODIC_ENERGIZE : SPELL_ENERGIZE, spell);
    e.info |= INFO_ENERGIZE;
    e.v[0] = DisplayAmount(amount, powerType);
    e.v[1] = powerType;
    e.v[2] = -1; // alternate power: none in 1.12
    Queue(e);
}

// The periodic mana leech's AddPowerDrainEntry.
void Drain(uint64_t caster, uint64_t target, const uint8_t *spell, int powerType, int amount,
           float multiplier) {
    if (amount == 0 || Hidden(spell))
        return;
    const int extra = static_cast<int>(static_cast<float>(amount) * multiplier);
    Entry e = Begin(caster, target, extra != 0 ? SPELL_PERIODIC_LEECH : SPELL_PERIODIC_DRAIN,
                    spell);
    e.info |= INFO_DRAIN;
    e.v[0] = DisplayAmount(amount, powerType);
    e.v[1] = powerType;
    e.v[2] = DisplayAmount(extra, powerType);
    e.v[3] = -1;
    Queue(e);
}

// SMSG_ATTACKERSTATEUPDATE → UnitCombatLog(ATTACKROUNDINFO).
void AttackerState(CDataStore *p) {
    const uint32_t hitInfo = Read<uint32_t>(p);
    const uint64_t attacker = ReadPackedGuid(p);
    const uint64_t victim = ReadPackedGuid(p);
    const int damage = Read<int32_t>(p);
    const int parts = Read<uint8_t>(p);
    uint32_t school = 0;
    int absorbed = 0;
    int resisted = 0;
    for (int i = 0; i < parts; ++i) {
        const uint32_t partSchool = Read<uint32_t>(p);
        Read<float>(p);
        Read<int32_t>(p); // the part's damage; the total is above
        absorbed += Read<int32_t>(p);
        resisted += Read<int32_t>(p);
        if (i == 0)
            school = partSchool;
    }
    uint32_t state = Read<uint32_t>(p);
    Read<uint32_t>(p);
    const uint32_t meleeSpell = Read<uint32_t>(p);
    const int blocked = Read<int32_t>(p);
    if (Overrun(p) || parts > kMaxDamageParts)
        return;

    // ReadAttackerState (0x00625E20): a swing with no damage and a blocked
    // amount on a victim the client knows is a block.
    void *victimUnit = Unit(victim);
    if (victimUnit != nullptr && damage == 0 && blocked != 0)
        state = VICTIMSTATE_BLOCKS;

    // ShouldShowFeedback, and who 4.3.4's SMSG_ATTACKER_STATE_UPDATE logs
    // for: a known attacker, or a player-controlled victim.
    if (meleeSpell != 0 && state != VICTIMSTATE_NORMAL)
        return;
    if (Unit(attacker) == nullptr && (victimUnit == nullptr || !PlayerControlled(victimUnit)))
        return;

    const bool offHand = (hitInfo & HITINFO_LEFTSWING) != 0;
    if ((state == VICTIMSTATE_NORMAL || state == VICTIMSTATE_BLOCKS) && damage != 0) {
        SendDamage(attacker, victim, SWING_DAMAGE, nullptr, damage, SchoolMask(school), resisted,
                   blocked, absorbed, (hitInfo & HITINFO_CRITICAL) != 0,
                   (hitInfo & HITINFO_GLANCING) != 0, (hitInfo & HITINFO_CRUSHING) != 0, offHand);
        return;
    }
    if (hitInfo & HITINFO_MISS) {
        SendMiss(attacker, victim, SWING_MISSED, nullptr, MISS_MISS, 0, offHand);
        return;
    }
    if (hitInfo & HITINFO_ABSORB) {
        SendMiss(attacker, victim, SWING_MISSED, nullptr, MISS_ABSORB, absorbed, offHand);
        return;
    }
    if (hitInfo & HITINFO_RESIST) {
        SendMiss(attacker, victim, SWING_MISSED, nullptr, MISS_RESIST, resisted, offHand);
        return;
    }
    switch (state) {
    case VICTIMSTATE_DODGE:
        SendMiss(attacker, victim, SWING_MISSED, nullptr, MISS_DODGE, 0, offHand);
        break;
    case VICTIMSTATE_PARRY:
        SendMiss(attacker, victim, SWING_MISSED, nullptr, MISS_PARRY, 0, offHand);
        break;
    case VICTIMSTATE_BLOCKS:
        SendMiss(attacker, victim, SWING_MISSED, nullptr, MISS_BLOCK, blocked, offHand);
        break;
    case VICTIMSTATE_EVADES:
        SendMiss(attacker, victim, SWING_MISSED, nullptr, MISS_EVADE, 0, offHand);
        break;
    case VICTIMSTATE_IMMUNE:
        SendMiss(attacker, victim, SWING_MISSED, nullptr, MISS_IMMUNE, 0, offHand);
        break;
    case VICTIMSTATE_DEFLECTS:
        SendMiss(attacker, victim, SWING_MISSED, nullptr, MISS_DEFLECT, 0, offHand);
        break;
    default:
        break;
    }
}

// SMSG_SPELLNONMELEEDAMAGELOG → HandleSpellNonMeleeDamageLog.
void NonMeleeDamage(CDataStore *p) {
    const uint64_t target = ReadPackedGuid(p);
    const uint64_t caster = ReadPackedGuid(p);
    const int spellId = Read<int32_t>(p);
    const int damage = Read<int32_t>(p);
    const uint32_t school = Read<uint8_t>(p);
    const int absorbed = Read<int32_t>(p);
    const int resisted = Read<int32_t>(p);
    const bool periodic = Read<uint8_t>(p) != 0;
    Read<uint8_t>(p);
    const int blocked = Read<int32_t>(p);
    const uint32_t hitFlags = Read<uint32_t>(p);
    if (Overrun(p))
        return;
    const uint8_t *spell = SpellRecord(spellId);
    const bool critical = (hitFlags & SPELL_HIT_CRITICAL) != 0;
    if (hitFlags & SPELL_HIT_SPLIT) {
        // UnitCombatLogSplitDamage.
        if (!Hidden(spell))
            SendDamage(caster, target, DAMAGE_SPLIT, spell, damage, SchoolMask(school), resisted,
                       blocked, absorbed, critical, false, false, false);
        return;
    }
    SpellDamage(caster, target, spell, damage, school, absorbed, resisted, blocked, critical,
                periodic);
}

// SMSG_SPELLHEALLOG → HandleSpellHealLog.
void HealLog(CDataStore *p) {
    const uint64_t target = ReadPackedGuid(p);
    const uint64_t caster = ReadPackedGuid(p);
    const int spellId = Read<int32_t>(p);
    const int amount = Read<int32_t>(p);
    const bool critical = Read<uint8_t>(p) != 0;
    if (Overrun(p))
        return;
    Heal(caster, target, SpellRecord(spellId), amount, critical, false);
}

// SMSG_SPELLENERGIZELOG → HandleSpellLogEnergizeLog.
void EnergizeLog(CDataStore *p) {
    const uint64_t target = ReadPackedGuid(p);
    const uint64_t caster = ReadPackedGuid(p);
    const int spellId = Read<int32_t>(p);
    const int powerType = Read<int32_t>(p);
    const int amount = Read<int32_t>(p);
    if (Overrun(p))
        return;
    Energize(caster, target, SpellRecord(spellId), powerType, amount, false);
}

// SMSG_PERIODICAURALOG → HandlePeriodicAuraLog.
void PeriodicAuraLog(CDataStore *p) {
    const uint64_t target = ReadPackedGuid(p);
    const uint64_t caster = ReadPackedGuid(p);
    const int spellId = Read<int32_t>(p);
    const uint32_t count = Read<uint32_t>(p);
    const uint8_t *spell = SpellRecord(spellId);
    if (Overrun(p) || spell == nullptr || Unit(target) == nullptr)
        return;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t auraType = Read<uint32_t>(p);
        switch (auraType) {
        case AURA_PERIODIC_DAMAGE:
        case AURA_PERIODIC_DAMAGE_PERCENT: {
            const int damage = Read<int32_t>(p);
            const uint32_t school = Read<uint32_t>(p);
            const int absorbed = Read<int32_t>(p);
            const int resisted = Read<int32_t>(p);
            if (Overrun(p))
                return;
            SpellDamage(caster, target, spell, damage, school, absorbed, resisted, 0, false, true);
            break;
        }
        case AURA_PERIODIC_HEAL:
        case AURA_OBS_MOD_HEALTH: {
            const int amount = Read<int32_t>(p);
            if (Overrun(p))
                return;
            Heal(caster, target, spell, amount, false, true);
            break;
        }
        case AURA_OBS_MOD_MANA:
        case AURA_PERIODIC_ENERGIZE: {
            const int powerType = Read<int32_t>(p);
            const int amount = Read<int32_t>(p);
            if (Overrun(p))
                return;
            Energize(caster, target, spell, powerType, amount, true);
            break;
        }
        case AURA_PERIODIC_MANA_LEECH: {
            const int powerType = Read<int32_t>(p);
            const int amount = Read<int32_t>(p);
            const float multiplier = Read<float>(p);
            if (Overrun(p))
                return;
            Drain(caster, target, spell, powerType, amount, multiplier);
            break;
        }
        default:
            return; // an unknown type ends the message, as in both clients
        }
    }
}

// SMSG_SPELLDAMAGESHIELD → UnitCombatLogDamageShield.
void DamageShield(CDataStore *p) {
    const uint64_t owner = Read<uint64_t>(p);
    const uint64_t victim = Read<uint64_t>(p);
    const int damage = Read<int32_t>(p);
    const uint32_t school = Read<uint32_t>(p);
    if (Overrun(p))
        return;
    Entry e = Begin(owner, victim, DAMAGE_SHIELD, nullptr);
    e.spellPrefix = true;
    e.spellSchool = SchoolMask(school);
    e.info |= INFO_DAMAGE;
    e.v[1] = damage;
    e.v[3] = static_cast<int>(SchoolMask(school));
    Queue(e);
}

// SMSG_ENVIRONMENTALDAMAGELOG → CGUnit_C::HandleEnvironmentDamage →
// UnitCombatLog(guid, type, damage, absorbed, resisted).
void EnvironmentalDamage(CDataStore *p) {
    // s_environmentalDamageSchools: physical ×3, fire, nature, fire.
    static constexpr uint32_t kSchools[] = {0, 0, 0, 2, 3, 2};
    const uint64_t victim = Read<uint64_t>(p);
    const uint32_t type = Read<uint8_t>(p);
    const int damage = Read<int32_t>(p);
    const int absorbed = Read<int32_t>(p);
    const int resisted = Read<int32_t>(p);
    if (Overrun(p) || Unit(victim) == nullptr || (damage == 0 && absorbed == 0 && resisted == 0))
        return;
    Entry e = Begin(0, victim, ENVIRONMENTAL_DAMAGE, nullptr);
    e.info |= INFO_ENVIRONMENTAL | INFO_DAMAGE;
    e.v[0] = static_cast<int>(type);
    e.v[1] = damage;
    e.v[3] = static_cast<int>(SchoolMask(type < 6 ? kSchools[type] : 0));
    e.v[4] = absorbed;
    e.v[5] = resisted;
    Queue(e);
}

void OnPacket(uint32_t opcode, CDataStore *p) {
    switch (opcode) {
    case SMSG_ATTACKERSTATEUPDATE:
        AttackerState(p);
        break;
    case SMSG_SPELLNONMELEEDAMAGELOG:
        NonMeleeDamage(p);
        break;
    case SMSG_SPELLHEALLOG:
        HealLog(p);
        break;
    case SMSG_SPELLENERGIZELOG:
        EnergizeLog(p);
        break;
    case SMSG_PERIODICAURALOG:
        PeriodicAuraLog(p);
        break;
    case SMSG_SPELLDAMAGESHIELD:
        DamageShield(p);
        break;
    case SMSG_ENVIRONMENTALDAMAGELOG:
        EnvironmentalDamage(p);
        break;
    default:
        break;
    }
}

const Net::PacketDispatch::AutoSubscribe _sub{&OnPacket};

} // namespace

} // namespace CombatLog::Packets
