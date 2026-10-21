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

// Auras and dispels — 4.3.4's UnitCombatLogAuraAddedOrRemoved,
// UnitCombatLogAuraModifiedApplications, UnitCombatLogDispelled and
// Packet_SMSG_DISPEL_FAILED.
//
// An aura's arrival, loss and stack change are the engine's own aura
// handlers (OnAuraAdded / OnAuraRemoved / OnAuraStacksChanged, which write
// 1.12's aura lines), reached through Aura::Source, which binds each aura to
// its caster. The rules are 4.3.4's, which are also 1.12's (LogAuraChange
// 0x0062B480, LogAuraApplication 0x0062B800): no passive spell, no loss on a
// dead unit, and stacks only for a spell that stacks. 4.3.4 also logs a
// stack's fall (SPELL_AURA_REMOVED_DOSE), which 1.12 does not write.
//
// 4.3.4's SPELL_AURA_REFRESH needs the server to say that an aura's duration
// restarted, which 1.12 never says for another unit, so it does not occur.
//
// 1.12's dispel messages name no dispelling spell (handlers 0x005E8B60,
// 0x00628C20; vmangos writes none), so the entry's spell is spellId 0 with no
// name, as a damage shield's is:
//   0x27b SPELLDISPELLOG  packed target, packed caster, count, count × spell
//   0x262 DISPEL_FAILED   caster, target (u64), spells to the end
// A dispel's BUFF or DEBUFF is the slot the aura holds on the target, which
// it still holds: the message comes before the update that removes it.

#include "combatlog/Entry.h"

#include "Game.h"
#include "Offsets.h"
#include "aura/Data.h"
#include "aura/Source.h"
#include "net/PacketDispatch.h"
#include "object/Resolve.h"
#include "unit/Identity.h"

#include <cstdint>

namespace CombatLog::Auras {

namespace {

using Net::CDataStore;
using Net::Read;
using Net::ReadPackedGuid;

constexpr uint32_t SMSG_DISPEL_FAILED = 0x262;
constexpr uint32_t SMSG_SPELLDISPELLOG = 0x27b;

bool Overrun(const CDataStore *p) { return p->m_read > p->m_size; }

int Health(const void *unit) {
    const auto *fields = Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_DESCRIPTOR);
    return fields != nullptr ? Game::Read<int>(fields, Offsets::OFF_UNIT_FIELD_HEALTH) : 0;
}

void OnAuraChange(const Aura::Source::Change &c) {
    const uint8_t *spell = SpellRecord(static_cast<int>(c.spellId));
    if (c.spellId == 0 || Hidden(spell))
        return;
    const uint64_t unit = Unit::Identity::GuidForObject(c.unit);
    if (c.type == Aura::Source::ChangeType::Stacks) {
        if (Game::Read<uint32_t>(spell, Offsets::OFF_SPELL_RECORD_STACK_AMOUNT) <= 1 ||
            c.newStacks == c.oldStacks)
            return;
        Entry e = Begin(c.caster, unit,
                        c.newStacks > c.oldStacks ? SPELL_AURA_APPLIED_DOSE
                                                  : SPELL_AURA_REMOVED_DOSE,
                        spell);
        e.info |= INFO_AURA_TYPE | INFO_AURA_AMOUNT;
        e.harmful = c.harmful;
        e.v[2] = c.newStacks;
        Queue(e);
        return;
    }
    const bool added = c.type == Aura::Source::ChangeType::Added;
    if ((!added && Health(c.unit) <= 0) ||
        (Game::Read<uint32_t>(spell, Offsets::OFF_SPELL_RECORD_ATTRIBUTES) &
         Offsets::SPELL_ATTR_PASSIVE) != 0)
        return;
    Entry e = Begin(c.caster, unit, added ? SPELL_AURA_APPLIED : SPELL_AURA_REMOVED, spell);
    e.info |= INFO_AURA_TYPE;
    e.harmful = c.harmful;
    Queue(e);
}

const Aura::Source::AutoObserve _obs{&OnAuraChange};

// The polarity of the slot `spellId` holds on `target`; a target the client
// does not know, or no longer holding it, reads as BUFF.
bool Harmful(uint64_t target, uint32_t spellId) {
    const auto *unit =
        static_cast<const uint8_t *>(Object::ByGuid(Offsets::TYPEMASK_UNIT, target));
    if (unit == nullptr)
        return false;
    const int slot = Aura::Data::FindSlotBySpellID(unit, spellId, nullptr);
    return slot >= 0 && Aura::Data::IsSlotHarmful(unit, slot);
}

// One entry with the dispelled spell as its extra spell.
void SendDispel(uint64_t caster, uint64_t target, int event, int dispelledId, bool auraType) {
    if (Hidden(SpellRecord(dispelledId)))
        return;
    Entry e = Begin(caster, target, event, nullptr);
    e.spellPrefix = true; // the dispelling spell, which 1.12 does not name
    e.info |= INFO_EXTRA_SPELL;
    e.v[0] = dispelledId;
    if (auraType) {
        e.info |= INFO_AURA_TYPE;
        e.harmful = Harmful(target, static_cast<uint32_t>(dispelledId));
    }
    Queue(e);
}

// SMSG_SPELLDISPELLOG → UnitCombatLogDispelled.
void DispelLog(CDataStore *p) {
    const uint64_t target = ReadPackedGuid(p);
    const uint64_t caster = ReadPackedGuid(p);
    const uint32_t count = Read<uint32_t>(p);
    for (uint32_t i = 0; i < count && !Overrun(p); ++i) {
        const int dispelled = Read<int32_t>(p);
        if (!Overrun(p))
            SendDispel(caster, target, SPELL_DISPEL, dispelled, true);
    }
}

// SMSG_DISPEL_FAILED → Packet_SMSG_DISPEL_FAILED.
void DispelFailed(CDataStore *p) {
    const uint64_t caster = Read<uint64_t>(p);
    const uint64_t target = Read<uint64_t>(p);
    while (!Overrun(p) && p->m_read + sizeof(int32_t) <= p->m_size)
        SendDispel(caster, target, SPELL_DISPEL_FAILED, Read<int32_t>(p), false);
}

void OnPacket(uint32_t opcode, CDataStore *p) {
    if (opcode == SMSG_SPELLDISPELLOG)
        DispelLog(p);
    else if (opcode == SMSG_DISPEL_FAILED)
        DispelFailed(p);
}

const Net::PacketDispatch::AutoSubscribe _sub{&OnPacket};

} // namespace

} // namespace CombatLog::Auras
