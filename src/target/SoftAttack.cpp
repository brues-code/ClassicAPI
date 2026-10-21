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

// Action targeting for melee: an attack with no target goes to `softenemy`
// (retail probe: /startattack with no target swings at the soft enemy, and
// the target stays empty).
//
// 1.12 selects whatever it attacks, twice: the resolve
// (FUN_ATTACK_RESOLVE_TARGET) runs the attacker's state ladder, then settles
// the GUID by selecting it and reading the selection back (see
// CALL_ATTACK_RESOLVE_SELECT in Offsets.h), and no target at all tab-targets
// the nearest enemy; then the start (FUN_ATTACK_START) selects the victim
// again before CMSG_ATTACKSWING. To attack the soft enemy without selecting
// it, the resolve is handed its GUID and both select calls are redirected:
//   - in the resolve, while that soft enemy is being settled, the call only
//     stands it in the selection slot for the read-back, and the slot is
//     emptied again when the resolve returns;
//   - in the start, the select is skipped for the soft enemy the resolve
//     just settled while there is still no target.
// Everything else — the state errors, the friendly-target tab, the final
// alive / CanAttack test, the swing packet — is the engine's own. Neither
// select locks a loose target: attacking it is not choosing it (retail's
// SoftTargetMatchLocked 2, "for targets you attack", is the opt-in that
// treats it as locked).
//
// Applies to AttackTarget (its toggle's resolve call is redirected here) and
// StartAttack. With soft enemy off, or no soft enemy, the engine's resolve
// runs untouched (and tab-targets as 1.12 does).

#include "target/SoftAttack.h"

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "target/Loose.h"
#include "target/SoftUnit.h"

namespace Target::SoftAttack {

namespace {

using Resolve_t = int(__fastcall *)(void *player, void *edx, uint64_t *guid);

uint64_t g_settling = 0; // the soft enemy the resolve is settling, or 0
bool g_stood = false;    // the select call stood it in the selection slot
uint64_t g_settled = 0;  // the soft enemy the last resolve settled on, or 0

uint64_t &Selection() {
    return *reinterpret_cast<uint64_t *>(
        static_cast<uintptr_t>(Offsets::VAR_CURRENT_SELECTION_GUID_LO));
}

void __stdcall Select_h(uint32_t guidLo, uint32_t guidHi) {
    const uint64_t guid = (static_cast<uint64_t>(guidHi) << 32) | guidLo;
    if (g_settling != 0 && guid == g_settling) {
        Selection() = guid;
        g_stood = true;
        return;
    }
    Loose::SelectKeepingLoose(guid);
}

void __stdcall StartSelect_h(uint32_t guidLo, uint32_t guidHi) {
    const uint64_t guid = (static_cast<uint64_t>(guidHi) << 32) | guidLo;
    if (g_settled != 0 && guid == g_settled && Selection() == 0) {
        g_settled = 0;
        return;
    }
    Loose::SelectKeepingLoose(guid);
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
    if (*guid != 0 || Selection() != 0)
        return Resolve(player, guid);
    const uint64_t soft = SoftUnit::Get(SoftUnit::Side::Enemy);
    if (soft == 0)
        return Resolve(player, guid);
    *guid = soft;
    g_settling = soft;
    g_stood = false;
    const int result = Resolve(player, guid);
    g_settling = 0;
    if (g_stood)
        Selection() = 0;
    if (result != 0 && *guid == soft)
        g_settled = soft;
    return result;
}

} // namespace Target::SoftAttack
