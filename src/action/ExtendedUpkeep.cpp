// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

// What happens to Action::Extended's slots 121..360 when the spellbook or the
// macro list changes. They follow 3.80.2, whose one table holds every slot;
// the engine's slots 1..120 keep 1.12's behaviour.
//
// 3.80.2 (WowClassicGM-China.exe 3.80.2.69496):
// - A superceded spell (handler 0x14141AF00) runs the replace 0x14175FCF0
//   (old, new) before it unlearns the old and learns the new. Each slot whose
//   spell (the resolver 0x14175DBF0) is `old`: a spell word becomes `new`, a
//   macro word's record gets `new` as its cached spell (+0x58C), any other
//   word is left. That is 1.12's FUN_ACTION_BAR_REPLACE_SPELL rule. 3.80.2
//   has no client-side rank replace on a plain learn: the learn 0x14145A640
//   calls the replace with an old spell of 0, which matches no slot.
// - An unlearned spell (handler 0x14141AAF0 → 0x14145AD80) clears no slot.
//   It ends in 0x14175FBC0(spell): every slot whose spell is the unlearned
//   one gets its cached state recomputed and ACTIONBAR_SLOT_CHANGED(slot).
//   The learned-spells handler 0x14141A980 runs the same refresh through the
//   learn's tail (0x14145ACFB). 1.12 clears its own slots instead
//   (FUN_ACTION_BAR_PRUNE_SPELL).
// - DeleteMacro (0x141768B30) clears every slot that holds the macro, as
//   1.12's FUN_ACTION_BAR_CLEAR_MACRO does.
//
// Each rule runs after the engine's own pass, from the one call site the
// engine makes for it. Our slots never reach the server: a changed word goes
// through Action::Extended::Set (file write, recount, ACTIONBAR_SLOT_CHANGED).

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "action/Extended.h"
#include "action/ExtendedState.h"

#include <cstdint>

namespace Action::ExtendedUpkeep {

namespace {

using SpellPair_t = void(__fastcall *)(uint32_t oldSpell, uint32_t newSpell);
using SpellOne_t = void(__fastcall *)(uint32_t id);
// __thiscall through __fastcall: ECX = player, EDX unused, then the four
// stack arguments the callee pops (RET 0x10).
using PlayerLearn_t = void(__fastcall *)(void *player, void *edx, uint32_t spellID, int a2,
                                         int a3, int a4);
using MacroIDToEntry_t = uint8_t *(__fastcall *)(uint32_t macroID);

uint32_t SpellOf(uint32_t word) {
    uint32_t book = 0;
    return Extended::SpellFor(word, &book);
}

// 0x14175FBC0 for our slots.
void RefreshSpell(uint32_t spellID) {
    if (spellID == 0 || !Extended::Loaded())
        return;
    for (int slot0 = Extended::kFirstSlot0; slot0 < Extended::kEndSlot0; ++slot0) {
        if (SpellOf(Extended::Get(slot0)) == spellID)
            ExtendedState::Notify(slot0);
    }
}

// 0x14175FCF0 for our slots. Like the engine's pass, the book of the slot's
// spell is not compared.
void Replace(uint32_t oldSpell, uint32_t newSpell) {
    if (oldSpell == 0 || !Extended::Loaded())
        return;
    for (int slot0 = Extended::kFirstSlot0; slot0 < Extended::kEndSlot0; ++slot0) {
        const uint32_t word = Extended::Get(slot0);
        if (word == 0 || SpellOf(word) != oldSpell)
            continue;
        if ((word & Offsets::ACTION_TYPE_MASK) == Offsets::ACTION_TYPE_SPELL) {
            Extended::Set(slot0, newSpell);
        } else if (const uint32_t macroID = Extended::LiveMacroID(word)) {
            // No notify, as in both clients' macro branch.
            if (uint8_t *entry =
                    reinterpret_cast<MacroIDToEntry_t>(Offsets::FUN_MACRO_ID_TO_ENTRY)(macroID))
                *reinterpret_cast<uint32_t *>(entry + Offsets::OFF_MACRO_PRIMARY_SPELL) = newSpell;
        }
    }
}

// 0x141768B30's slot loop for our slots: the macro is already gone, so the
// raw word is compared, as FUN_ACTION_BAR_CLEAR_MACRO does.
void ClearMacro(uint32_t macroID) {
    if (macroID == 0 || !Extended::Loaded())
        return;
    for (int slot0 = Extended::kFirstSlot0; slot0 < Extended::kEndSlot0; ++slot0) {
        const uint32_t word = Extended::Get(slot0);
        if ((word & Offsets::ACTION_TYPE_MASK) == Offsets::ACTION_TYPE_BAG_OR_MACRO &&
            (word & Offsets::ACTION_PAYLOAD_MASK_BAG_OR_MACRO) == macroID)
            Extended::Set(slot0, 0);
    }
}

void __fastcall Replace_h(uint32_t oldSpell, uint32_t newSpell) {
    reinterpret_cast<SpellPair_t>(Offsets::FUN_ACTION_BAR_REPLACE_SPELL)(oldSpell, newSpell);
    Replace(oldSpell, newSpell);
}

// The call goes to the prune's address, so Macro::ShowTooltip's co-hook on it
// still runs.
void __fastcall Prune_h(uint32_t spellID) {
    reinterpret_cast<SpellOne_t>(Offsets::FUN_ACTION_BAR_PRUNE_SPELL)(spellID);
    RefreshSpell(spellID);
}

void __fastcall ClearMacro_h(uint32_t macroID) {
    reinterpret_cast<SpellOne_t>(Offsets::FUN_ACTION_BAR_CLEAR_MACRO)(macroID);
    ClearMacro(macroID);
}

void __fastcall Learn_h(void *player, void *, uint32_t spellID, int a2, int a3, int a4) {
    reinterpret_cast<PlayerLearn_t>(Offsets::FUN_PLAYER_LEARN_SPELL)(player, nullptr, spellID, a2,
                                                                     a3, a4);
    RefreshSpell(spellID);
}

void Register() {
    Common::RedirectCall(Offsets::CALL_SUPERCEDED_SPELL_REPLACE,
                         Offsets::FUN_ACTION_BAR_REPLACE_SPELL,
                         reinterpret_cast<const void *>(&Replace_h));
    Common::RedirectCall(Offsets::CALL_UNLEARN_PRUNE_SPELL, Offsets::FUN_ACTION_BAR_PRUNE_SPELL,
                         reinterpret_cast<const void *>(&Prune_h));
    Common::RedirectCall(Offsets::CALL_DELETE_MACRO_CLEAR_SLOTS,
                         Offsets::FUN_ACTION_BAR_CLEAR_MACRO,
                         reinterpret_cast<const void *>(&ClearMacro_h));
    Common::RedirectCall(Offsets::CALL_LEARNED_SPELL_LEARN, Offsets::FUN_PLAYER_LEARN_SPELL,
                         reinterpret_cast<const void *>(&Learn_h));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Action::ExtendedUpkeep
