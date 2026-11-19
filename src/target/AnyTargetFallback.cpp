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

// Pet actions and emotes with no unit given take AnyTarget, as in 3.80.2.
//
// 1.12 falls back to the selection where the 3.80.2 client calls AnyTarget
// (FUN_1405dd690, Target::SoftUnit::AnyTarget): the soft enemy, else the soft
// friend, else the soft interact while the target is empty or loose, else
// the target. 3.80.2's pet action dispatcher (FUN_1419c09f0: PetAttack
// FUN_14173bd40, CastPetAction, CastSpell(slot, "pet")) and its emote core
// (FUN_1418cc340, for an empty or `%` target name) take it [code]. Without
// it the pet ignored the soft enemy with no target (or a loose one), and
// `/wave` waved at nothing while a soft friend stood in front of you.
//
// Each engine fallback load is replaced where it stands, so the engine keeps
// every other step (see Offsets.h SITE_PRESS_PET_ACTION_TARGET_LOAD):
//   - PressPetAction's zero-target load, which every pet bar click,
//     CastPetAction and pet-book cast reaches;
//   - Script_PetAttack, which hands the selection itself to PressPetAttack:
//     it now hands no target, so the same fallback applies;
//   - Script_DoEmote's target load, before its name argument replaces it.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "target/SoftUnit.h"

#include <cstdint>

namespace Target::AnyTargetFallback {

namespace {

using PressPetAttack_t = void(__fastcall *)(const uint64_t *target);

uint64_t __stdcall AnyTarget() { return SoftUnit::AnyTarget(); }

// Stands in for PressPetAction's `mov edx,[lo]; mov eax,[hi]`: the answer
// in EDX (lo) and EAX (hi); ECX is free (the next instruction after the
// stores zeroes it).
__declspec(naked) void PetActionTargetThunk() {
    __asm {
        call AnyTarget
        mov ecx, eax
        mov eax, edx
        mov edx, ecx
        ret
    }
}

// Stands in for Script_DoEmote's `mov esi,[lo]; mov edi,[hi]`: the answer
// in ESI (lo) and EDI (hi), ECX (the Lua state) kept.
__declspec(naked) void EmoteTargetThunk() {
    __asm {
        push ecx
        call AnyTarget
        mov esi, eax
        mov edi, edx
        pop ecx
        ret
    }
}

void __fastcall PetAttack_h(const uint64_t *) {
    static const uint64_t kNone = 0;
    reinterpret_cast<PressPetAttack_t>(Offsets::FUN_PRESS_PET_ATTACK)(&kNone);
}

void Patch() {
    static_assert(Offsets::VAR_CURRENT_SELECTION_GUID_LO == 0x00B4E2D8,
                  "the loads below hold the selection's address");
    static const uint8_t kPetActionLoad[11] = {0x8B, 0x15, 0xD8, 0xE2, 0xB4, 0x00,
                                               0xA1, 0xDC, 0xE2, 0xB4, 0x00};
    static const uint8_t kEmoteLoad[12] = {0x8B, 0x35, 0xD8, 0xE2, 0xB4, 0x00,
                                           0x8B, 0x3D, 0xDC, 0xE2, 0xB4, 0x00};
    Common::ReplaceWithCall(Offsets::SITE_PRESS_PET_ACTION_TARGET_LOAD, kPetActionLoad,
                            sizeof kPetActionLoad,
                            reinterpret_cast<const void *>(&PetActionTargetThunk));
    Common::ReplaceWithCall(Offsets::SITE_DO_EMOTE_TARGET_LOAD, kEmoteLoad, sizeof kEmoteLoad,
                            reinterpret_cast<const void *>(&EmoteTargetThunk));
    Common::RedirectCall(Offsets::CALL_SCRIPT_PET_ATTACK_PRESS, Offsets::FUN_PRESS_PET_ATTACK,
                         reinterpret_cast<const void *>(&PetAttack_h));
}

const Game::ModuleAutoRegister _autoreg{&Patch};

} // namespace

} // namespace Target::AnyTargetFallback
