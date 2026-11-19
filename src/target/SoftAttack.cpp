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

// Action targeting for melee: an attack with no unit given goes to
// `anyenemy` — the soft enemy, or a locked target that is an enemy
// candidate — and attacking that unit leaves the target as it is (3.80.2's
// attack path, FUN_1414c6cb0: it sets the target, reason 3, only for a unit
// other than that action target). A friendly locked target does not stop
// the attack from going to the soft enemy.
//
// 1.12 selects whatever it attacks, twice: the resolve
// (FUN_ATTACK_RESOLVE_TARGET) runs the attacker's state ladder, then settles
// the GUID by selecting it and reading the selection back (see
// CALL_ATTACK_RESOLVE_SELECT in Offsets.h), and no target at all tab-targets
// the nearest enemy; then the start (FUN_ATTACK_START) selects the victim
// again before CMSG_ATTACKSWING. To attack the soft enemy without selecting
// it, the resolve is handed its GUID and both select calls are redirected:
//   - in the resolve, while that soft enemy is being settled, the call only
//     stands it in the selection slot for the read-back, and the slot gets
//     its target back when the resolve returns;
//   - in the start, the select is skipped for the unit the resolve just
//     settled.
// Everything else — the state errors, the friendly-target tab, the final
// alive / CanAttack test, the swing packet — is the engine's own. Every
// other select of theirs is the Attack reason (Target::Reason): a new unit
// becomes the locked target, and the current one keeps its reason — a Force
// target stays hidden, as attacking it is not choosing it.
//
// The attack follows the soft enemy through SoftTargetForce: Force moves the
// target to each new soft enemy, and 1.12's target setter carries a running
// attack to the new target, as 3.80.2's does.
//
// Applies to AttackTarget (its toggle's resolve call is redirected here) and
// StartAttack. With soft enemy off, or no soft enemy, the engine's resolve
// runs untouched (and tab-targets as 1.12 does).

#include "target/SoftAttack.h"

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "target/Reason.h"
#include "target/SoftUnit.h"

namespace Target::SoftAttack {

namespace {

using Resolve_t = int(__fastcall *)(void *player, void *edx, uint64_t *guid);

uint64_t g_settling = 0; // the soft enemy the resolve is settling, or 0
bool g_stood = false;    // the select call stood it in the selection slot
uint64_t g_stoodOver = 0; // the selection it stood in for
uint64_t g_settled = 0;  // the soft enemy the last resolve settled on, or 0

uint64_t &Selection() {
    return *reinterpret_cast<uint64_t *>(
        static_cast<uintptr_t>(Offsets::VAR_CURRENT_SELECTION_GUID_LO));
}

void __stdcall Select_h(uint32_t guidLo, uint32_t guidHi) {
    const uint64_t guid = (static_cast<uint64_t>(guidHi) << 32) | guidLo;
    if (g_settling != 0 && guid == g_settling) {
        g_stoodOver = Selection();
        Selection() = guid;
        g_stood = true;
        return;
    }
    Reason::Set(guid, Reason::Attack);
}

void __stdcall StartSelect_h(uint32_t guidLo, uint32_t guidHi) {
    const uint64_t guid = (static_cast<uint64_t>(guidHi) << 32) | guidLo;
    if (g_settled != 0 && guid == g_settled) {
        g_settled = 0;
        return;
    }
    Reason::Set(guid, Reason::Attack);
}

int Resolve(void *player, uint64_t *guid) {
    return reinterpret_cast<Resolve_t>(Offsets::FUN_ATTACK_RESOLVE_TARGET)(player, nullptr, guid);
}

int __fastcall Toggle_h(void *player, void *, uint64_t *guid) { return ResolveMelee(player, guid); }

void Register() {
    Common::RedirectCall(Offsets::CALL_ATTACK_RESOLVE_SELECT, Offsets::FUN_SET_SELECTION_BY_GUID,
                         reinterpret_cast<const void *>(&Select_h));
    Common::RedirectCall(Offsets::CALL_ATTACK_TOGGLE_RESOLVE, Offsets::FUN_ATTACK_RESOLVE_TARGET,
                         reinterpret_cast<const void *>(&Toggle_h));
    Common::RedirectCall(Offsets::CALL_ATTACK_START_SELECT, Offsets::FUN_SET_SELECTION_BY_GUID,
                         reinterpret_cast<const void *>(&StartSelect_h));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

int ResolveMelee(void *player, uint64_t *guid) {
    g_settled = 0;
    if (*guid != 0)
        return Resolve(player, guid);
    const uint64_t soft = SoftUnit::GetAny(SoftUnit::Side::Enemy);
    if (soft == 0 || soft == Reason::Selection())
        return Resolve(player, guid);
    *guid = soft;
    g_settling = soft;
    g_stood = false;
    const int result = Resolve(player, guid);
    g_settling = 0;
    if (g_stood)
        Selection() = g_stoodOver;
    if (result != 0 && *guid == soft)
        g_settled = soft;
    return result;
}

} // namespace Target::SoftAttack
