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

// Action targeting for spells: a spell cast with no target goes to the soft
// unit — `softenemy` for a harmful spell, `softfriend` for a helpful one —
// and a harmful one that goes off makes its unit the loose target.
//
// Retail 12.x probes, Enable Action Targeting on:
//   - Arcane Blast with no target and a soft enemy: it casts at the soft
//     enemy, which becomes the target (loose) once the cast succeeds — with
//     SoftTargetForce 0 too;
//   - Arcane Intellect with no target and a soft friend: the soft friend gets
//     it (not the caster), and the target stays empty.
//
// 1.12's cast request (FUN_006E4B60, nampower hooks its entry) reads its
// target GUID twice, and both calls are redirected (see
// CALL_CAST_CHECK_TARGET_RANGE / CALL_CAST_TARGET_ARM in Offsets.h). With no
// GUID and no target:
//   - the range pre-check is handed the soft unit of the spell's side, from
//     the engine's own classifier (GetSpellHostility). It range-checks only a
//     unit the spell can take, so a soft unit out of the spell's range gets
//     the engine's "Out of range", and a self spell is not affected;
//   - the arm is handed it too. When the arm binds it, the spell goes to that
//     unit. When it does not, the arm runs again as 1.12 has it (the self
//     cast with autoSelfCast, else the "no target" refusal or the cursor) —
//     so the soft friend comes before autoSelfCast, as in retail.
// SMSG_SPELL_GO for the player's spell then makes a soft enemy the loose
// target, while there is still no target.
//
// Not covered: a spell that starts a melee swing (Heroic Strike) takes its
// unit from the melee resolve the request calls directly, which 1.12 has.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "net/PacketDispatch.h"
#include "target/Loose.h"
#include "target/SoftUnit.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Target::SoftCast {

namespace {

using Range_t = uint8_t(__fastcall *)(void *caster, const uint8_t *record, uint32_t guidLo,
                                      uint32_t guidHi, uint32_t report);
using Arm_t = uint8_t(__fastcall *)(void *caster, const uint8_t *record, uint32_t guidLo,
                                    uint32_t guidHi);
using Hostility_t = int(__fastcall *)(const uint8_t *record, void *edx);

constexpr int kHarmful = 2;
constexpr int kHelpful = 1;

// The soft unit the current request's range check took, for its arm.
uint64_t g_requestSoft = 0;
bool g_requestHarmful = false;

// The harmful spell sent at a soft enemy, until its SMSG_SPELL_GO.
int g_pendingSpell = 0;
uint64_t g_pendingUnit = 0;

uint64_t Pick(const uint8_t *record, bool &harmful) {
    const int hostility =
        reinterpret_cast<Hostility_t>(Offsets::FUN_SPELL_HOSTILITY)(record, nullptr);
    harmful = hostility == kHarmful;
    if (hostility == kHarmful)
        return SoftUnit::Get(SoftUnit::Side::Enemy);
    if (hostility == kHelpful)
        return SoftUnit::Get(SoftUnit::Side::Friend);
    return 0;
}

uint8_t __fastcall Range_h(void *caster, const uint8_t *record, uint32_t guidLo, uint32_t guidHi,
                           uint32_t report) {
    auto range = reinterpret_cast<Range_t>(Offsets::FUN_CHECK_SPELL_TARGET_RANGE);
    g_requestSoft = 0;
    if ((guidLo | guidHi) != 0 || Loose::Selection() != 0 || record == nullptr)
        return range(caster, record, guidLo, guidHi, report);
    const uint64_t soft = Pick(record, g_requestHarmful);
    if (soft == 0)
        return range(caster, record, guidLo, guidHi, report);
    g_requestSoft = soft;
    return range(caster, record, static_cast<uint32_t>(soft), static_cast<uint32_t>(soft >> 32),
                 report);
}

uint8_t __fastcall Arm_h(void *caster, const uint8_t *record, uint32_t guidLo, uint32_t guidHi) {
    auto arm = reinterpret_cast<Arm_t>(Offsets::FUN_SPELL_TARGET_ARM);
    const uint64_t soft = g_requestSoft;
    g_requestSoft = 0;
    if (soft == 0 || (guidLo | guidHi) != 0 || Loose::Selection() != 0)
        return arm(caster, record, guidLo, guidHi);
    if (arm(caster, record, static_cast<uint32_t>(soft), static_cast<uint32_t>(soft >> 32)) != 0) {
        if (g_requestHarmful) {
            g_pendingSpell = Game::Read<int>(record, 0);
            g_pendingUnit = soft;
        }
        return 1;
    }
    return arm(caster, record, guidLo, guidHi);
}

void OnPacket(uint32_t opcode, Net::CDataStore *p) {
    if (opcode != Offsets::SMSG_SPELL_GO || g_pendingSpell == 0)
        return;
    Net::ReadPackedGuid(p); // the item
    const uint64_t caster = Net::ReadPackedGuid(p);
    const int spellId = static_cast<int>(Net::Read<uint32_t>(p));
    if (p->m_read > p->m_size || caster != Unit::Identity::PlayerGuid() ||
        spellId != g_pendingSpell)
        return;
    const uint64_t unit = g_pendingUnit;
    g_pendingSpell = 0;
    g_pendingUnit = 0;
    if (Loose::Selection() == 0)
        Loose::SetLoose(unit);
}

const Net::PacketDispatch::AutoSubscribe _sub{&OnPacket};

void Register() {
    Common::RedirectCall(Offsets::CALL_CAST_CHECK_TARGET_RANGE,
                         Offsets::FUN_CHECK_SPELL_TARGET_RANGE,
                         reinterpret_cast<const void *>(&Range_h));
    Common::RedirectCall(Offsets::CALL_CAST_TARGET_ARM, Offsets::FUN_SPELL_TARGET_ARM,
                         reinterpret_cast<const void *>(&Arm_h));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Target::SoftCast
