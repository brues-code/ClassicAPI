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

// Deaths — 4.3.4's UnitCombatLogUnitDead and Packet_SMSG_PARTY_KILL_LOG.
//
// A death is FUN_PRESENT_UNIT_DEATH, 1.12's death presentation (the death
// line, the death animation), which is where 4.3.4's OnDeathAnimate logs it.
// Its line writer (0x0062C160) is not hooked: nampower detours it. A unit
// whose creating spell summons it (FUN_UNIT_IS_SUMMONED, the test behind
// 1.12's UNITDESTROYEDOTHER) is UNIT_DESTROYED, as 4.3.4 logs a totem's.
// 4.3.4's UNIT_DISSIPATES is for creature type 13, which 1.12 does not have.
//
// 0x1f5 PARTYKILLLOG (handler 0x00628890): killer, victim (u64).

#include "combatlog/Entry.h"

#include "Game.h"
#include "Offsets.h"
#include "net/PacketDispatch.h"
#include "unit/Identity.h"

#include <cstdint>

namespace CombatLog::Deaths {

namespace {

using Net::CDataStore;
using Net::Read;

constexpr uint32_t SMSG_PARTYKILLLOG = 0x1f5;

bool IsSummoned(void *unit) {
    using IsSummoned_t = bool(__thiscall *)(void *unit);
    return reinterpret_cast<IsSummoned_t>(static_cast<uintptr_t>(Offsets::FUN_UNIT_IS_SUMMONED))(
        unit);
}

using PresentDeath_t = void(__fastcall *)(void *unit);
PresentDeath_t PresentDeath_o = nullptr;

void __fastcall PresentDeath_h(void *unit) {
    Entry e = Begin(0, Unit::Identity::GuidForObject(unit),
                    IsSummoned(unit) ? UNIT_DESTROYED : UNIT_DIED, nullptr);
    Queue(e);
    PresentDeath_o(unit);
}

const Game::HookAutoRegister _hook{Offsets::FUN_PRESENT_UNIT_DEATH,
                                   reinterpret_cast<void *>(&PresentDeath_h),
                                   reinterpret_cast<void **>(&PresentDeath_o)};

// SMSG_PARTYKILLLOG → Packet_SMSG_PARTY_KILL_LOG.
void OnPacket(uint32_t opcode, CDataStore *p) {
    if (opcode != SMSG_PARTYKILLLOG)
        return;
    const uint64_t killer = Read<uint64_t>(p);
    const uint64_t victim = Read<uint64_t>(p);
    if (p->m_read > p->m_size)
        return;
    Entry e = Begin(killer, victim, PARTY_KILL, nullptr);
    Queue(e);
}

const Net::PacketDispatch::AutoSubscribe _sub{&OnPacket};

} // namespace

} // namespace CombatLog::Deaths
