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

// Dropping a spell on a slot that already holds it — 4.3.4's
// CGActionBar::PutActionInSlot:
//
//   else if (CGActionBar::IsSpell(slot) && CGActionBar::GetSpell(slot, petSpell) == spellId)
//       CGGameUI::DropCursorSpell();
//
// 1.12's FUN_ACTION_PLACE runs the same compare without the IsSpell gate,
// and FUN_ACTION_SLOT_TO_SPELL resolves a macro slot to the macro's spell.
// So a spell dropped on a `/cast <that spell>` macro was taken for a
// duplicate: the cursor cleared and the macro stayed. The gate is one call
// redirect: a slot that is not a spell action has no spell to match.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Action::Place {

namespace {

using SlotToSpell_t = uint32_t(__fastcall *)(uint32_t slot0, uint32_t *outPetFlag);

uint32_t __fastcall SpellSlotSpell(uint32_t slot0, uint32_t *outPetFlag) {
    const uint32_t action = reinterpret_cast<const uint32_t *>(Offsets::VAR_ACTION_TABLE)[slot0];
    if (action == 0 || (action & Offsets::ACTION_TYPE_MASK) != Offsets::ACTION_TYPE_SPELL)
        return 0;
    return reinterpret_cast<SlotToSpell_t>(Offsets::FUN_ACTION_SLOT_TO_SPELL)(slot0, outPetFlag);
}

void Register() {
    Common::RedirectCall(Offsets::CALL_ACTION_PLACE_SAME_SPELL, Offsets::FUN_ACTION_SLOT_TO_SPELL,
                         reinterpret_cast<const void *>(&SpellSlotSpell));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Action::Place
