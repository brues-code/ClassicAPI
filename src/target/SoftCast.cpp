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

// Action targeting for spells: a spell cast with no unit given goes to
// `anyenemy` when harmful and `anyfriend` when helpful — the soft unit, or a
// locked target that is a candidate of that side (3.80.2's default spell
// target, FUN_1413d1ac0). A friendly locked target does not stop a harmful
// spell from going to the soft enemy.
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
// GUID, when the side's any-unit is not the target:
//   - the range pre-check is handed that unit, the side from
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
// `/cast`, the spellbook and bag / equipped item use take the same path.
// 1.12's CastSpellByName, CastSpell, UseContainerItem and UseInventoryItem
// pass the selection on as an explicit target, where an action button passes
// none, so a heal macro or a bandage went at a hostile Force target while the
// same spell or item on a button went to the soft friend. 3.80.2's
// CastSpellByName (FUN_141756f10) and UseInventoryItem (FUN_1419be400)
// resolve a missing unit through FUN_141b8e5b0's nil-token branch, which
// returns the default spell target (FUN_1413d1ac0) for the spell or the
// item's use spell: the button's rule. So at those four engine calls the
// selection becomes "no unit given"; the player (onSelf, or the player
// selected) stays explicit.
//
// A spell that starts a melee swing (Heroic Strike, Sunder Armor) takes its
// unit earlier, from the melee resolve the request calls directly; that
// call is Target::SoftAttack's, and the GUID it settles reaches both reads
// here as given.

#include "target/SoftCast.h"

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
#include <initializer_list>

namespace Target::SoftCast {

namespace {

using Range_t = uint8_t(__fastcall *)(void *caster, const uint8_t *record, uint32_t guidLo,
                                      uint32_t guidHi, uint32_t report);
using Arm_t = uint8_t(__fastcall *)(void *caster, const uint8_t *record, uint32_t guidLo,
                                    uint32_t guidHi);
using Hostility_t = int(__fastcall *)(const uint8_t *record, void *edx);
using Dispatch_t = void(__fastcall *)(unsigned slot, int bookType, unsigned guidLo,
                                      unsigned guidHi);
using ItemUse_t = uint32_t(__thiscall *)(void *item, const uint64_t *target, int confirmed);
using UseSlot_t = int(__fastcall *)(int slot, const uint64_t *target);

// The soft unit the current request's range check took, for its arm.
uint64_t g_requestSoft = 0;

CVar::Factory::Handle g_autoEnemy = nullptr;
CVar::Factory::Handle g_autoFriend = nullptr;

int Hostility(const uint8_t *record) {
    return reinterpret_cast<Hostility_t>(Offsets::FUN_SPELL_HOSTILITY)(record, nullptr);
}

// 3.80.2's default spell target (FUN_1413d1ac0) for a cast with no unit: the
// side's any-unit, and when that is empty — or for a spell of neither side —
// AnyTarget. 0 when it is the selection, which the engine takes itself.
uint64_t Pick(const uint8_t *record) {
    const int hostility = Hostility(record);
    uint64_t unit = 0;
    if (hostility == Offsets::SPELL_HOSTILITY_HARMFUL)
        unit = SoftUnit::GetAny(SoftUnit::Side::Enemy);
    else if (hostility == Offsets::SPELL_HOSTILITY_HELPFUL)
        unit = SoftUnit::GetAny(SoftUnit::Side::Friend);
    if (unit == 0)
        unit = SoftUnit::AnyTarget();
    return unit != Reason::Selection() ? unit : 0;
}

uint8_t __fastcall Range_h(void *caster, const uint8_t *record, uint32_t guidLo, uint32_t guidHi,
                           uint32_t report) {
    auto range = reinterpret_cast<Range_t>(Offsets::FUN_CHECK_SPELL_TARGET_RANGE);
    g_requestSoft = 0;
    if ((guidLo | guidHi) != 0 || record == nullptr)
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
    if (soft == 0 || (guidLo | guidHi) != 0)
        return arm(caster, record, guidLo, guidHi);
    if (arm(caster, record, static_cast<uint32_t>(soft), static_cast<uint32_t>(soft >> 32)) != 0)
        return 1;
    return arm(caster, record, guidLo, guidHi);
}

// A script's "current target" stand-in: the selection becomes no unit, so
// the request takes the default spell target; anything else stays.
uint64_t Implicit(uint64_t guid) {
    return guid != 0 && guid == Reason::Selection() && guid != Unit::Identity::PlayerGuid()
               ? 0
               : guid;
}

void __fastcall ScriptDispatch_h(unsigned slot, int bookType, unsigned guidLo, unsigned guidHi) {
    const uint64_t guid = Implicit((static_cast<uint64_t>(guidHi) << 32) | guidLo);
    reinterpret_cast<Dispatch_t>(Offsets::FUN_SPELL_CAST_DISPATCH)(
        slot, bookType, static_cast<unsigned>(guid), static_cast<unsigned>(guid >> 32));
}

// Script_UseContainerItem's FUN_ITEM_USE call (__thiscall; EDX unused).
uint32_t __fastcall ContainerItemUse_h(void *item, void *, const uint64_t *target, int confirmed) {
    const uint64_t guid = Implicit(*target);
    return reinterpret_cast<ItemUse_t>(Offsets::FUN_ITEM_USE)(item, &guid, confirmed);
}

int __fastcall InventorySlotUse_h(int slot, const uint64_t *target) {
    const uint64_t guid = Implicit(*target);
    return reinterpret_cast<UseSlot_t>(Offsets::FUN_USE_INVENTORY_SLOT)(slot, &guid);
}

void OnCastGo(const Spell::CastPacket::Go &g) {
    if (g.header.caster != Unit::Identity::PlayerGuid() || g.hitCount == 0)
        return;
    const uint8_t *record = Spell::Lookup::RecordForID(static_cast<int>(g.header.spellId));
    if (record == nullptr)
        return;
    const uint64_t hit = g.hits[0];
    const int hostility = Hostility(record);
    if ((hostility == Offsets::SPELL_HOSTILITY_HARMFUL && CVar::Factory::GetInt(g_autoEnemy) != 0 &&
         hit == SoftUnit::Current(SoftUnit::Side::Enemy)) ||
        (hostility == Offsets::SPELL_HOSTILITY_HELPFUL && CVar::Factory::GetInt(g_autoFriend) != 0 &&
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
    for (const uintptr_t site : {static_cast<uintptr_t>(Offsets::CALL_SCRIPT_CAST_SPELL_DISPATCH),
                                 static_cast<uintptr_t>(Offsets::CALL_SCRIPT_CAST_SPELL_BY_NAME_DISPATCH)})
        Common::RedirectCall(site, Offsets::FUN_SPELL_CAST_DISPATCH,
                             reinterpret_cast<const void *>(&ScriptDispatch_h));
    Common::RedirectCall(Offsets::CALL_SCRIPT_USE_CONTAINER_ITEM_USE, Offsets::FUN_ITEM_USE,
                         reinterpret_cast<const void *>(&ContainerItemUse_h));
    Common::RedirectCall(Offsets::CALL_SCRIPT_USE_INVENTORY_ITEM_SLOT,
                         Offsets::FUN_USE_INVENTORY_SLOT,
                         reinterpret_cast<const void *>(&InventorySlotUse_h));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

uint64_t DefaultTarget(const uint8_t *record) { return record != nullptr ? Pick(record) : 0; }

} // namespace Target::SoftCast
