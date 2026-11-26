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

// The range answer of an action button whose macro is aimed at a unit, or
// whose spell goes to a soft target.
//
// 4.3.4 keeps that unit on the macro (MACRONODE::m_target: the `[@unit]` of
// the clause its display resolved from, or the target SetMacroSpell /
// SetMacroItem named), and CGActionBar::HasValidTarget range-checks the slot's
// spell against it in place of the current target. 1.12's slot range function
// FUN_ACTION_SLOT_RANGE already takes the unit as a GUID for its range check,
// and always passes 0, the selection. `Macro::ShowTooltip` keeps the unit (its
// `Info::unit`), and this answers for those slots with that GUID, in the
// engine's order: the range check, then the implicit-target test, where the
// unit standing in for the selection must exist.
//
// With action targeting, a slot with no macro unit is range-checked the same
// way against the unit its cast would go to (Target::SoftCast::DefaultTarget),
// as 3.80.2's IsActionInRange core FUN_14175d860 does: the given unit, else
// the macro's (+0x5A8), else none, which its range check FUN_1413d1da0
// replaces with the default spell target FUN_1413d1ac0. So a heal
// button turns red for an out-of-range soft friend while a hostile Force
// target is selected, and shows a range with no target at all. Every other
// slot goes to the engine function unchanged.
//
// The function's one caller, Script_IsActionInRange, is redirected rather than
// the function hooked.
//
// C_ActionBar.IsActionInRange(actionID [, target]) answers through Answer with
// the target's GUID, which 3.80.2's core takes first (a given unit, else the
// macro's, else none). Action::Extended's slots 121..360 take the same path;
// where 1.12's slot function would run for one of them (no unit at all), its
// body is mirrored on their spell: the range check against the selection (GUID
// 0), then the implicit-target test with a target selected.

#include "action/Range.h"

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "action/Extended.h"
#include "action/Slot.h"
#include "macro/ShowTooltip.h"
#include "object/Resolve.h"
#include "spell/Lookup.h"
#include "target/SoftCast.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Action::Range {

namespace {

using SlotRange_t = int(__fastcall *)(int slot0, int *inRange);
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

// FUN_ACTION_SLOT_RANGE's body after its slot read: the range check of
// `record` against `unit` (0 = the selection), then the implicit-target test,
// which with no unit needs a target selected and with one needs it to exist.
int CheckRecord(const uint8_t *record, uint32_t pet, uint64_t unit, int *inRange) {
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr || record == nullptr || pet != 0)
        return 0;
    if (!reinterpret_cast<CheckRange_t>(Offsets::FUN_CHECK_SPELL_TARGET_RANGE)(
            player, record, static_cast<uint32_t>(unit), static_cast<uint32_t>(unit >> 32), 0)) {
        *inRange = 0;
        return 1;
    }
    const int32_t implicitTarget =
        Game::Read<int32_t>(record, Offsets::OFF_SPELL_RECORD_EFFECT_IMPLICIT_TARGET_A);
    if (!TakesUnitTarget(implicitTarget))
        return 0;
    const bool present = unit != 0
                             ? Object::ByGuid(Offsets::TYPEMASK_UNIT, unit) != nullptr
                             : Game::Read<uint64_t>(Offsets::VAR_CURRENT_SELECTION_GUID_LO) != 0;
    if (!present)
        return 0;
    *inRange = 1;
    return 1;
}

int Core(int slot0, uint64_t given, int *inRange) {
    const bool engineSlot = slot0 >= 0 && slot0 < Offsets::ACTION_TABLE_MAX_SLOTS;
    const auto original = reinterpret_cast<SlotRange_t>(Offsets::FUN_ACTION_SLOT_RANGE);
    if (!engineSlot && !Action::Extended::Owns(slot0))
        return 0;
    uint32_t pet = 0;
    const uint32_t spellID = Action::Slot::SpellFor(slot0, &pet);
    const uint8_t *record = static_cast<int>(spellID) > 0
                                ? ::Spell::Lookup::RecordForID(static_cast<int>(spellID))
                                : nullptr;
    uint64_t unit = given != 0 ? given : MacroUnit(slot0);
    if (unit == 0 && pet == 0)
        unit = Target::SoftCast::DefaultTarget(record);
    if (unit == 0 && engineSlot)
        return original(slot0, inRange);
    return CheckRecord(record, pet, unit, inRange);
}

// The global IsActionInRange stays 1.12's: slots past 120 get its own nil.
int __fastcall SlotRange_h(int slot0, int *inRange) {
    if (slot0 < 0 || slot0 >= Offsets::ACTION_TABLE_MAX_SLOTS)
        return reinterpret_cast<SlotRange_t>(Offsets::FUN_ACTION_SLOT_RANGE)(slot0, inRange);
    return Core(slot0, 0, inRange);
}

void Register() {
    Common::RedirectCall(Offsets::CALL_IS_ACTION_IN_RANGE_SLOT_RANGE,
                         Offsets::FUN_ACTION_SLOT_RANGE,
                         reinterpret_cast<const void *>(&SlotRange_h));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

int Answer(int slot0, uint64_t unit, int *inRange) { return Core(slot0, unit, inRange); }

} // namespace Action::Range
