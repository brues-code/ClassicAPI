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

// Casts — 4.3.4's UnitCombatLogCastStart, UnitCombatLogCastGo and
// UnitCombatLogSpellFail.
//
// SPELL_CAST_START and SPELL_CAST_SUCCESS are read from SMSG_SPELL_START and
// SMSG_SPELL_GO through Net::PacketDispatch (nampower detours both handlers).
// 4.3.4 logs them from the same two messages. Its two builders read the
// unit's casting time for the spell with opposite tests — the 3.3.5 binary
// agrees (0x00751920 and 0x007519E0 call the same vtable slot): a spell with a
// cast time logs its start, an instant spell its success, and a cast-time
// spell no success.
//
// SPELL_CAST_FAILED is the 1.12 failure line's own moment
// (FUN_COMBAT_LOG_SPELL_FAIL), which the failure reporter calls with the
// player and the error text just shown, as 4.3.4 calls UnitCombatLogSpellFail.
//
// 1.12's bodies (handlers 0x006E7640 / 0x006E7A70, target reader 0x007E4ED0):
//   0x131 SPELL_START  packed item or caster, packed caster, spell, u16 flags,
//                      cast time, targets
//   0x132 SPELL_GO     packed item or caster, packed caster, spell, u16 flags,
//                      u8 n, n × u64 hit, u8 m, m × {u64, u8 reason, u8 more
//                      for a reflect}, targets
//   targets            u16 mask, a packed object GUID for mask 0x8A02 (unit,
//                      corpse or game object), then the rest by mask

#include "combatlog/Entry.h"

#include "Game.h"
#include "Offsets.h"
#include "combatlog/RecentCasts.h"
#include "net/PacketDispatch.h"
#include "object/Resolve.h"
#include "spell/Lookup.h"
#include "unit/Identity.h"
#include "spell/CastPacket.h"

#include <cstdint>

namespace CombatLog::Casts {

namespace {

// The cast flag that keeps a cast out of the combat log (both servers'
// CAST_FLAG_HIDDEN_COMBATLOG); 4.3.4 tests the same bit.
constexpr uint16_t CAST_FLAG_HIDDEN_COMBATLOG = 0x1;

// 4.3.4's SpellHasOpenLockEffect tests 33 alone; 1.12's success line also
// drops 59 (0x0062AD90), which 4.3.4 logs.
constexpr int SPELL_EFFECT_OPEN_LOCK = 33;
// AttributesEx4 0x1: the cast writes no line (the 1.12 start line tests it,
// 0x0062B30D, as 4.3.4 does).
constexpr uint32_t ATTR_EX4_NO_CAST_LOG = 0x1;

int CastingTime(void *unit, int spellId) {
    using CastingTime_t = int(__thiscall *)(void *unit, int spellId);
    const auto *vtbl = Game::Read<const uintptr_t *>(unit, 0);
    return reinterpret_cast<CastingTime_t>(
        vtbl[Offsets::OFF_CGUNIT_VTBL_GET_SPELL_CASTING_TIME / sizeof(uintptr_t)])(unit,
                                                                                    spellId);
}

// The rules both cast builders share.
bool Logged(const uint8_t *spell) {
    return !Hidden(spell) && !Spell::Lookup::HasEffect(spell, Offsets::SPELL_EFFECT_LEARN_SPELL) &&
           (Game::Read<uint32_t>(spell, Offsets::OFF_SPELL_RECORD_ATTRIBUTES_EX4) &
            ATTR_EX4_NO_CAST_LOG) == 0;
}

// SMSG_SPELL_START → UnitCombatLogCastStart: a cast with a cast time.
void SpellStart(const Spell::CastPacket::Start &s) {
    const uint64_t caster = s.header.caster;
    const int spellId = static_cast<int>(s.header.spellId);
    if ((s.header.castFlags & CAST_FLAG_HIDDEN_COMBATLOG) != 0)
        return;
    void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, caster);
    const uint8_t *spell = SpellRecord(spellId);
    if (unit == nullptr || !Logged(spell) || CastingTime(unit, spellId) == 0)
        return;
    Entry e = Begin(caster, 0, SPELL_CAST_START, spell);
    Queue(e);
}

// SMSG_SPELL_GO → UnitCombatLogCastGo: an instant cast, at its target. Every
// cast's hit list also goes to RecentCasts, for the messages that follow it.
void SpellGo(const Spell::CastPacket::Go &g) {
    const uint64_t caster = g.header.caster;
    const int spellId = static_cast<int>(g.header.spellId);
    const uint64_t target = g.targets.object;
    RecentCasts::Record(caster, g.header.spellId, g.hits, g.hitCount);
    if ((g.header.castFlags & CAST_FLAG_HIDDEN_COMBATLOG) != 0)
        return;
    void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, caster);
    const uint8_t *spell = SpellRecord(spellId);
    if (unit == nullptr || !Logged(spell) ||
        Spell::Lookup::HasEffect(spell, SPELL_EFFECT_OPEN_LOCK) || CastingTime(unit, spellId) != 0)
        return;
    Entry e = Begin(caster, target, SPELL_CAST_SUCCESS, spell);
    Queue(e);
}

const Spell::CastPacket::AutoStart _start{&SpellStart};
const Spell::CastPacket::AutoGo _go{&SpellGo};

using SpellFail_t = void(__fastcall *)(void *player, int spellId, const char *text);
SpellFail_t SpellFail_o = nullptr;

void __fastcall SpellFail_h(void *player, int spellId, const char *text) {
    const uint8_t *spell = SpellRecord(spellId);
    if (player != nullptr && text != nullptr && *text != '\0' && spell != nullptr &&
        // 4.3.4's failure builder drops only DO_NOT_DISPLAY, not DO_NOT_LOG.
        (Game::Read<uint32_t>(spell, Offsets::OFF_SPELL_RECORD_ATTRIBUTES) &
         Offsets::SPELL_ATTR_DO_NOT_DISPLAY) == 0 &&
        Spell::Lookup::Name(spell) != nullptr) {
        Entry e = Begin(Unit::Identity::GuidForObject(player), 0, SPELL_CAST_FAILED, spell);
        e.info |= INFO_TEXT;
        e.text = text;
        Queue(e);
    }
    SpellFail_o(player, spellId, text);
}

const Game::HookAutoRegister _hook{Offsets::FUN_COMBAT_LOG_SPELL_FAIL,
                                   reinterpret_cast<void *>(&SpellFail_h),
                                   reinterpret_cast<void **>(&SpellFail_o)};

} // namespace

} // namespace CombatLog::Casts
