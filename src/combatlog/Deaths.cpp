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

#include "combatlog/Deaths.h"

#include "combatlog/DeathRecap.h"

#include "Game.h"
#include "Offsets.h"
#include "net/PacketDispatch.h"
#include "object/Resolve.h"
#include "tick/WorldTick.h"
#include "unit/Identity.h"

#include <cstdint>
#include <vector>

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

// A death arrives before the hit that killed: the server deals the damage,
// which kills, then sends the damage message (tortoise
// Unit::AttackerStateUpdate: DealMeleeDamage, then SendAttackStateUpdate —
// "in sniffs", as on the original servers), from the same call, so both reach
// the client in one burst. Later clients log the killing blow first, and so
// does 3.80.2's death recap, which is built when the death is logged. So the
// death entry is held: the unit's next damage or instakill entry releases it
// right behind (Observe), and the end of the frame releases whatever no hit
// claimed — a death with no logged killing blow, or one whose message slipped
// into a later frame, which then keeps 1.12's order.
std::vector<Entry> g_held;

// 3.80.2's UNIT_DIED handler (FUN_1414407d0) gives every death the death
// suffix: the recap ID for the local player's (made before the entry is
// queued, so the walk stops at the previous death), -1 for any other unit.
void Release(Entry e) {
    e.info |= INFO_DEATH;
    e.v[0] = e.dest != 0 && e.dest == Unit::Identity::PlayerGuid()
                 ? DeathRecap::OnPlayerDied(e.dest)
                 : -1;
    Queue(e);
}

void ReleaseAll() {
    std::vector<Entry> held;
    held.swap(g_held);
    for (Entry &e : held)
        Release(e);
}

const Tick::WorldTick::AutoSubscribe _tick{&ReleaseAll};

void PrepareForGlue() { g_held.clear(); }

const Game::GlueModuleAutoRegister _gluereg{&PrepareForGlue};

void __fastcall PresentDeath_h(void *unit) {
    g_held.push_back(
        Begin(0, ::Object::Guid(unit), IsSummoned(unit) ? UNIT_DESTROYED : UNIT_DIED, nullptr));
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
    if (Net::Overrun(p))
        return;
    Entry e = Begin(killer, victim, PARTY_KILL, nullptr);
    Queue(e);
}

const Net::PacketDispatch::AutoSubscribe _sub{&OnPacket};

} // namespace

void Observe(const Entry &entry) {
    if (g_held.empty() || entry.dest == 0 ||
        ((entry.info & INFO_DAMAGE) == 0 && entry.event != SPELL_INSTAKILL))
        return;
    for (auto it = g_held.begin(); it != g_held.end(); ++it) {
        if (it->dest == entry.dest) {
            Entry death = *it;
            g_held.erase(it);
            Release(death);
            return;
        }
    }
}

} // namespace CombatLog::Deaths
