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

// `C_Spell.IsSpellReactiveReady(spell)` — whether a spell's proc condition
// holds now, whatever the stance, form or power. A ClassicAPI extension:
// stance-dancing macros need "Overpower is up" while still in Defensive
// Stance, where IsUsableSpell says no.
//
// The answer is three of FUN_SPELL_IS_USABLE's gates, mirrored in its order
// from the disassembly (Offsets.h has the sites):
//   - combo points: AttributesEx REQ_*_COMBO_POINTS needs the player's count
//     above 0. The engine does not compare the combo target, so isReady
//     does not either; the combo target is a second return for the caller
//     to compare. Overpower is gated here: the server grants a hidden point
//     on a dodge and takes it back when the window ends or Overpower is cast.
//   - CasterAuraState n needs bit n−1 of the player's aura-state word
//     (Revenge, Riposte, and Turtle's Counterattack / Surprise Attack /
//     Arcane Surge / Kill Command).
//   - TargetAuraState n needs a target with bit n−1 set, attackable for a
//     first implicit target of TARGET_UNIT_ENEMY and assistable for
//     TARGET_UNIT_FRIEND (Execute, Hammer of Wrath).
// The gates the engine runs around them — dead, control, reagents, equipped
// item, form, stealth, combat, active, power — are left out on purpose.
// The predicate is one function with no way to run part of it, so this is a
// mirror; keep it in step with the engine's gates rather than adding any.
//
// No event of our own: the engine already signals SPELL_UPDATE_USABLE from
// every input these gates read, via FUN_FIRE_USABLE_EVENTS — combo points,
// target select / release, and the aura-state field watcher (0x00604890),
// which fires for the active player OR the current target.

#include "Game.h"
#include "Offsets.h"
#include "guid/Guid.h"
#include "object/Resolve.h"
#include "player/Combo.h"
#include "spell/Arg.h"
#include "spell/Lookup.h"
#include "unit/Identity.h"
#include "unit/Relation.h"

#include <cstdint>

namespace Spell::Reactive {

namespace {

bool HasAuraState(const void *unit, uint32_t state) {
    const uint8_t *fields = Object::Fields(unit);
    return fields != nullptr &&
           (Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_AURASTATE) & (1u << (state - 1))) != 0;
}

// `*comboTarget` gets the unit holding the points when the spell needs them
// and the count is above 0, else 0. The engine's gate ignores it, but the
// server does not: a dodge on mob A leaves the point on A, so Overpower on
// a tabbed-to B fails. Callers compare it with their target.
bool IsReady(int spellID, uint64_t *comboTarget) {
    *comboTarget = 0;
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    const void *player = Unit::Identity::PlayerObject();
    if (record == nullptr || player == nullptr)
        return false;

    if ((Game::Read<uint32_t>(record, Offsets::OFF_SPELL_RECORD_ATTRIBUTES_EX) &
         Offsets::SPELL_ATTR_EX_REQ_COMBO_POINTS) != 0) {
        if (Player::Combo::Points() == 0)
            return false;
        *comboTarget = Player::Combo::Target();
    }

    const uint32_t casterState = Game::Read<uint32_t>(record, Offsets::OFF_SPELL_RECORD_CASTER_AURA_STATE);
    if (casterState != 0 && !HasAuraState(player, casterState))
        return false;

    const uint32_t targetState = Game::Read<uint32_t>(record, Offsets::OFF_SPELL_RECORD_TARGET_AURA_STATE);
    if (targetState != 0) {
        const void *target = Object::ByGuid(
            Offsets::TYPEMASK_UNIT, Game::Read<uint64_t>(Offsets::VAR_CURRENT_SELECTION_GUID_LO));
        if (target == nullptr || !HasAuraState(target, targetState))
            return false;
        const int implicit = Game::Read<int>(record, Offsets::OFF_SPELL_RECORD_EFFECT_IMPLICIT_TARGET_A);
        if (implicit == Offsets::TARGET_UNIT_ENEMY && !Unit::Relation::CanAttack(player, target))
            return false;
        if (implicit == Offsets::TARGET_UNIT_FRIEND && !Unit::Relation::CanAssist(player, target))
            return false;
    }
    return true;
}

int __fastcall Script_IsSpellReactiveReady(void *L) {
    uint64_t comboTarget = 0;
    Game::Lua::PushBool(L, IsReady(Spell::Arg::ResolveSpellID(L, 1), &comboTarget));
    if (comboTarget == 0) {
        Game::Lua::PushNil(L);
    } else {
        char buf[Guid::STRING_SIZE];
        Game::Lua::PushString(L, Guid::FormatAsString(comboTarget, buf, sizeof buf));
    }
    return 2;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("spell", "SpellIdentifier", "A spell ID, spell link, or spell name."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("isReady", "bool",
                   "True while the spell's combo point and aura-state requirements are met. "
                   "True for a spell with none; false for an unknown spell."),
    Game::Doc::Opt("comboTargetGUID", "WOWGUID", nullptr,
                   "The unit that holds the combo points, when the spell needs combo points "
                   "and the count is above 0. Compare it with UnitGUID(\"target\")."),
};
const Game::Doc::Function kIsSpellReactiveReady{
    "Returns whether a reactive spell's proc condition holds now (Overpower after a dodge, "
    "Revenge after a block, Execute on a low target), ignoring stance, form, power and "
    "cooldown. Changes are signalled by SPELL_UPDATE_USABLE.",
    kArgs, kRets, nullptr, true};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "IsSpellReactiveReady", &Script_IsSpellReactiveReady,
                                     &kIsSpellReactiveReady);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::Reactive
