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

// Who an aura from a totem belongs to — a ClassicAPI extension: the aura's
// caster on the server is the totem, and ClassicAPI names the shaman instead,
// so `sourceUnit` reads "player" for your own totems' buffs and debuffs.
//
// A totem is the unit whose UNIT_CREATED_BY_SPELL is a totem-drop spell
// (Spell.dbc effect SUMMON_TOTEM_SLOT1..4); the server stamps that field with
// the summon spell and UNIT_FIELD_CREATEDBY with the shaman (vmangos /
// tortoise-wow EffectSummonTotem). Both fields are broadcast, so this works for
// every shaman's totems in view, not only the player's.
//
// Most totem auras are party area auras (Strength of Earth, Grace of Air, the
// resistance totems), which no packet attributes; Aura::AreaCaster finds the
// totem that carries one and names its owner through OfTotem.

#include "Owner.h"

#include "Game.h"
#include "Offsets.h"
#include "guid/Guid.h"
#include "object/Resolve.h"
#include "spell/Lookup.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Totem::Owner {

namespace {

bool IsTotemSummon(const uint8_t *rec) {
    if (rec == nullptr)
        return false;
    for (int e = Offsets::SPELL_EFFECT_SUMMON_TOTEM_SLOT1;
         e <= Offsets::SPELL_EFFECT_SUMMON_TOTEM_SLOT4; ++e)
        if (Spell::Lookup::HasEffect(rec, e))
            return true;
    return false;
}

// The owner of the unit object `unit` when it is a totem, else 0.
uint64_t OwnerOfObject(const void *unit) {
    const uint32_t summon = Unit::Identity::CreatedBySpell(unit);
    if (summon == 0 || !IsTotemSummon(Spell::Lookup::RecordForID(static_cast<int>(summon))))
        return 0;
    return Unit::Identity::OwnerGuid(unit);
}

} // namespace

uint64_t OfTotem(uint64_t guid) {
    if (!Guid::IsCreature(guid))
        return 0;
    const void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, nullptr, 0);
    return unit != nullptr ? OwnerOfObject(unit) : 0;
}

} // namespace Totem::Owner
