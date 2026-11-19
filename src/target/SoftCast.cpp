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

// Action targeting for spells: a spell cast with no locked target goes to
// the soft unit — `softenemy` for a harmful spell, `softfriend` for a
// helpful one.
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
// GUID and no locked target:
//   - the range pre-check is handed the soft unit of the spell's side, from
//     the engine's own classifier (GetSpellHostility). It range-checks only a
//     unit the spell can take, so a soft unit out of the spell's range gets
//     the engine's "Out of range", and a self spell is not affected;
//   - the arm is handed it too. When the arm binds it, the spell goes to that
//     unit. When it does not, the arm runs again as 1.12 has it (the self
//     cast with autoSelfCast, else the target, the "no target" refusal or the
//     cursor) — so the soft friend comes before autoSelfCast, as in retail.
//
// The target that follows is 3.80.2's spell-go rule (FUN_1413d7a80): when the
// player's own spell lands and its first hit unit is the soft enemy (a
// harmful spell, TargetAutoEnemy on) or the soft friend (a helpful one,
// TargetAutoFriend on), that unit becomes the target for AutoSpell — loose,
// and never over a locked target (Target::Reason). On a Force target that is
// the same unit, so the raise makes it visible. TargetAutoEnemy defaults to
// 1, the Arcane Blast probe (retail's default too); TargetAutoFriend to 0,
// the Arcane Intellect probe (3.80.2's and Classic Era's default).
//
// Not covered: a spell that starts a melee swing (Heroic Strike) takes its
// unit from the melee resolve the request calls directly, which 1.12 has.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "cvar/Character.h"
#include "spell/Lookup.h"
#include "target/Reason.h"
#include "target/SoftUnit.h"
#include "unit/Identity.h"
#include "spell/CastPacket.h"

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

CVar::Factory::Handle g_autoEnemy = nullptr;
CVar::Factory::Handle g_autoFriend = nullptr;

int Hostility(const uint8_t *record) {
    return reinterpret_cast<Hostility_t>(Offsets::FUN_SPELL_HOSTILITY)(record, nullptr);
}

uint64_t Pick(const uint8_t *record) {
    const int hostility = Hostility(record);
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
    if ((guidLo | guidHi) != 0 || Reason::LockedTarget() != 0 || record == nullptr)
        return range(caster, record, guidLo, guidHi, report);
    const uint64_t soft = Pick(record);
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
    if (soft == 0 || (guidLo | guidHi) != 0 || Reason::LockedTarget() != 0)
        return arm(caster, record, guidLo, guidHi);
    if (arm(caster, record, static_cast<uint32_t>(soft), static_cast<uint32_t>(soft >> 32)) != 0)
        return 1;
    return arm(caster, record, guidLo, guidHi);
}

void OnCastGo(const Spell::CastPacket::Go &g) {
    if (g.header.caster != Unit::Identity::PlayerGuid() || g.hitCount == 0)
        return;
    const uint8_t *record = Spell::Lookup::RecordForID(static_cast<int>(g.header.spellId));
    if (record == nullptr)
        return;
    const uint64_t hit = g.hits[0];
    const int hostility = Hostility(record);
    if ((hostility == kHarmful && CVar::Factory::GetInt(g_autoEnemy) != 0 &&
         hit == SoftUnit::Current(SoftUnit::Side::Enemy)) ||
        (hostility == kHelpful && CVar::Factory::GetInt(g_autoFriend) != 0 &&
         hit == SoftUnit::Current(SoftUnit::Side::Friend)))
        Reason::Set(hit, Reason::AutoSpell);
}

const Spell::CastPacket::AutoGo _go{&OnCastGo};

void Register() {
    g_autoEnemy = CVar::Character::Register("TargetAutoEnemy", "Auto-Target from your single target helpful spells", Offsets::CVAR_CATEGORY_GAME, "1");
    g_autoFriend = CVar::Character::Register("TargetAutoFriend", "Auto-Target from your single target helpful spells", Offsets::CVAR_CATEGORY_GAME, "0");
    Common::RedirectCall(Offsets::CALL_CAST_CHECK_TARGET_RANGE,
                         Offsets::FUN_CHECK_SPELL_TARGET_RANGE,
                         reinterpret_cast<const void *>(&Range_h));
    Common::RedirectCall(Offsets::CALL_CAST_TARGET_ARM, Offsets::FUN_SPELL_TARGET_ARM,
                         reinterpret_cast<const void *>(&Arm_h));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Target::SoftCast
