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

// Spell misses — 4.3.4's UnitCombatLogSpellMissed.
//
// 1.12 writes every spell miss through one function, FUN_COMBAT_LOG_SPELL_MISS:
// the SMSG_SPELL_GO miss list for an instant spell, a missile's arrival for
// one that travels, and SMSG_SPELLLOGMISS, which passes its damage-shield
// flag. These are the moments 4.3.4 logs a miss too (Spell_C for a spell with
// no speed, Missile_C on arrival, HandleSpellLogMiss with damageShield), so the
// entry is built at the start of that function, before its range test. The
// function drops the spells 4.3.4 drops (missType 0, Attributes 0x180, no
// name); the entry tests the same.

#include "combatlog/Entry.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace CombatLog::Miss {

namespace {

using SpellMiss_t = void(__fastcall *)(int missType, int spellId, uint32_t casterLo,
                                       uint32_t casterHi, uint32_t targetLo, uint32_t targetHi,
                                       uint32_t damageShield);

SpellMiss_t SpellMiss_o = nullptr;

uint64_t Guid(uint32_t lo, uint32_t hi) { return (static_cast<uint64_t>(hi) << 32) | lo; }

void __fastcall SpellMiss_h(int missType, int spellId, uint32_t casterLo, uint32_t casterHi,
                            uint32_t targetLo, uint32_t targetHi, uint32_t damageShield) {
    const uint8_t *spell = SpellRecord(spellId);
    if (missType != MISS_NONE && !Hidden(spell)) {
        const bool shield = (damageShield & 0xFF) != 0;
        const int event =
            shield ? DAMAGE_SHIELD_MISSED : (Ranged(spell) ? RANGE_MISSED : SPELL_MISSED);
        Entry e = Begin(Guid(casterLo, casterHi), Guid(targetLo, targetHi), event, spell);
        SetMissInfo(e, missType, 0, false);
        Queue(e);
    }
    SpellMiss_o(missType, spellId, casterLo, casterHi, targetLo, targetHi, damageShield);
}

const Game::HookAutoRegister _hook{Offsets::FUN_COMBAT_LOG_SPELL_MISS,
                                   reinterpret_cast<void *>(&SpellMiss_h),
                                   reinterpret_cast<void **>(&SpellMiss_o)};

} // namespace

} // namespace CombatLog::Miss
