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
// resistance totems): the totem casts the aura on itself, and the server then
// copies it onto the owner's group members in range (AreaAura::Update) with
// no packet that names the caster. Two facts recover it. The totem keeps its
// own copy in a visible aura slot — IsNeedVisibleSlot's "passive auras
// (except totem auras) do not get placed in caster slot", on both vmangos and
// tortoise-wow. And a target below the spell's level gets a lower rank of the
// same line (SelectAuraRankForLevel), so the line, not the ID, is the match:
// the localized spell name, which every rank shares. Of the visible totems
// carrying the aura, the target's own wins (the owner gets the copy too),
// then the nearest — the client cannot see a stranger's group, so two groups'
// identical totems side by side are told apart only by distance.

#include "Owner.h"

#include "Game.h"
#include "Offsets.h"
#include "aura/Data.h"
#include "guid/Guid.h"
#include "object/Resolve.h"
#include "spell/Lookup.h"
#include "unit/Identity.h"
#include "unit/Position.h"

#include <cfloat>
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
    const auto *desc = Object::Fields(unit);
    if (desc == nullptr)
        return 0;
    const auto summon = Game::Read<uint32_t>(desc, Offsets::OFF_UNIT_FIELD_CREATED_BY_SPELL);
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

uint64_t OfAreaAura(uint64_t targetGuid, uint32_t spellId) {
    const uint8_t *rec = Spell::Lookup::RecordForID(static_cast<int>(spellId));
    if (rec == nullptr ||
        !Spell::Lookup::HasEffect(rec, Offsets::SPELL_EFFECT_APPLY_AREA_AURA_PARTY))
        return 0;
    float targetPos[3];
    if (!Unit::Position::Read(Object::ByGuid(Offsets::TYPEMASK_UNIT, targetGuid, nullptr, 0),
                              targetPos))
        return 0;

    // Every rank of a line shares its name, so the name finds the totem's own
    // rank too.
    const char *name = Spell::Lookup::Name(rec);
    if (name == nullptr)
        return 0;
    uint64_t best = 0;
    float bestDistSq = FLT_MAX;
    Object::ForEachVisible([&](uint64_t guid) {
        if (!Guid::IsCreature(guid))
            return true;
        void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, nullptr, 0);
        if (unit == nullptr)
            return true;
        const uint64_t owner = OwnerOfObject(unit);
        if (owner == 0 ||
            Aura::Data::FindSlotBySpellName(static_cast<const uint8_t *>(unit), name, nullptr) < 0)
            return true;
        if (owner == targetGuid) {
            best = owner;
            return false; // the target's own totem: nothing beats it
        }
        float pos[3];
        if (!Unit::Position::Read(unit, pos))
            return true;
        const float distSq = Game::DistanceSq(pos, targetPos);
        if (distSq < bestDistSq) {
            best = owner;
            bestDistSq = distSq;
        }
        return true;
    });
    return best;
}

} // namespace Totem::Owner
