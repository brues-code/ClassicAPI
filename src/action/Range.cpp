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

// The range answer of an action button whose macro is aimed at a unit.
//
// 4.3.4 keeps that unit on the macro (MACRONODE::m_target: the `[@unit]` of
// the clause its display resolved from, or the target SetMacroSpell /
// SetMacroItem named), and CGActionBar::HasValidTarget range-checks the slot's
// spell against it in place of the current target. 1.12's slot range function
// FUN_ACTION_SLOT_RANGE already takes the unit as a GUID for its range check,
// and always passes 0, the selection. `Macro::ShowTooltip` keeps the unit (its
// `Info::unit`), and this answers for those slots with that GUID, in the
// engine's order: the range check, then the implicit-target test, where the
// unit standing in for the selection must exist. Every other slot goes to the
// engine function unchanged.
//
// The function's one caller, Script_IsActionInRange, is redirected rather than
// the function hooked.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "action/Slot.h"
#include "macro/ShowTooltip.h"
#include "object/Resolve.h"
#include "spell/Lookup.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Action::Range {

namespace {

using SlotRange_t = int(__fastcall *)(int slot0, int *inRange);
using SlotToSpell_t = int(__fastcall *)(int slot0, uint32_t *outPetFlag);
using CheckRange_t = uint8_t(__fastcall *)(const void *caster, const uint8_t *record,
                                           uint32_t guidLo, uint32_t guidHi, uint32_t report);

// The first implicit targets the engine answers for: case 0 of its table.
bool TakesUnitTarget(int32_t implicitTarget) {
    switch (implicitTarget) {
    case 6: case 21: case 23: case 25: case 26: case 35: case 45: case 57: case 61:
        return true;
    default:
        return false;
    }
}

uint64_t MacroUnit(int slot0) {
    const uint32_t macroID = Action::Slot::MacroIDForSlot(slot0);
    Macro::ShowTooltip::Info info{};
    // Passive: this runs inside a Lua call, from a button's OnUpdate.
    if (macroID == 0 || !Macro::ShowTooltip::LookupPassive(macroID, &info))
        return 0;
    return info.unit;
}

int __fastcall SlotRange_h(int slot0, int *inRange) {
    const auto original = reinterpret_cast<SlotRange_t>(Offsets::FUN_ACTION_SLOT_RANGE);
    if (slot0 < 0 || slot0 >= Offsets::ACTION_TABLE_MAX_SLOTS)
        return original(slot0, inRange);
    const uint64_t unit = MacroUnit(slot0);
    if (unit == 0)
        return original(slot0, inRange);

    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return 0;
    uint32_t pet = 0;
    const int spellID =
        reinterpret_cast<SlotToSpell_t>(Offsets::FUN_ACTION_SLOT_TO_SPELL)(slot0, &pet);
    const uint8_t *record = ::Spell::Lookup::RecordForID(spellID);
    if (record == nullptr || pet != 0)
        return 0;
    if (!reinterpret_cast<CheckRange_t>(Offsets::FUN_CHECK_SPELL_TARGET_RANGE)(
            player, record, static_cast<uint32_t>(unit), static_cast<uint32_t>(unit >> 32), 0)) {
        *inRange = 0;
        return 1;
    }
    const int32_t implicitTarget =
        Game::Read<int32_t>(record, Offsets::OFF_SPELL_RECORD_EFFECT_IMPLICIT_TARGET_A);
    if (!TakesUnitTarget(implicitTarget) || Object::ByGuid(Offsets::TYPEMASK_UNIT, unit) == nullptr)
        return 0;
    *inRange = 1;
    return 1;
}

void Register() {
    Common::RedirectCall(Offsets::CALL_IS_ACTION_IN_RANGE_SLOT_RANGE,
                         Offsets::FUN_ACTION_SLOT_RANGE,
                         reinterpret_cast<const void *>(&SlotRange_h));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Action::Range
