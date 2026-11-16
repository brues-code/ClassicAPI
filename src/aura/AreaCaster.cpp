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

// Who cast a player's party area aura.
//
// A party area aura (Spell.dbc effect APPLY_AREA_AURA_PARTY at the caster:
// Leader of the Pack, the paladin auras, Trueshot Aura, Moonkin Aura) is put
// on the caster, and the server copies it onto the caster's group members in
// range. A 1.12 aura slot holds only the spell ID, so neither copy names the
// caster, and Leader of the Pack has no SMSG_SPELL_GO at all (the server casts
// it from the shapeshift; Era's log has no cast line for it either). Classic
// Era names the druid on every copy, its own included.
//
// Who could be the caster:
//   - the local player, exactly: it knows the spell (its known-spell bitmap)
//     and carries the aura, and the aura is on itself or a party member
//     (party1-4, which in a raid are the player's own subgroup, the reach of
//     a party aura);
//   - another player, by class: its race and class can learn the spell
//     (the engine's SkillLineAbility lookup, which also applies
//     SkillRaceClassInfo, so Trueshot Aura's class-less row still resolves to
//     hunters) and it carries the aura. The target itself wins, then the
//     nearest, as the totem rule does (Totem::Owner::OfAreaAura).
//
// A spell can learn through a spell of the same name: Leader of the Pack's
// aura (24932) has no SkillLineAbility row, but the druid talent that grants
// it (17007) shares its name, as Talent::SpellSet already matches talents to
// their spells. Of the 181 party area auras in the 1.12 Spell.dbc, 35 have
// their own row and 19 more match one by name (Leader of the Pack, Devotion
// and Retribution Aura ranks); the rest are totem, item and creature auras,
// which this rule leaves alone.
//
// The class rule guesses when two players of the class carry the same aura:
// two paladins on the same aura, or a druid without the talent beside one with
// it. The client cannot tell their copies apart.

#include "AreaCaster.h"

#include "Data.h"
#include "Game.h"
#include "Offsets.h"
#include "group/Roster.h"
#include "guid/Guid.h"
#include "object/Resolve.h"
#include "spell/Known.h"
#include "spell/Lookup.h"
#include "spell/SkillLine.h"
#include "unit/Identity.h"
#include "unit/Position.h"

#include <cfloat>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace Aura::AreaCaster {

namespace {

// The spells that teach `spellId`: those with a SkillLineAbility row that are
// the spell itself or share its name. Built once per spell; SkillLineAbility
// is loaded at boot.
const std::vector<uint32_t> &Teachers(uint32_t spellId, const char *name) {
    static std::unordered_map<uint32_t, std::vector<uint32_t>> cache;
    const auto found = cache.find(spellId);
    if (found != cache.end())
        return found->second;
    std::vector<uint32_t> &out = cache[spellId];
    const auto *const *records = Game::Read<const uint8_t *const *>(
        static_cast<uintptr_t>(Offsets::VAR_SKILL_LINE_ABILITY_RECORDS));
    const int count =
        Game::Read<int>(static_cast<uintptr_t>(Offsets::VAR_SKILL_LINE_ABILITY_COUNT));
    if (records == nullptr)
        return out;
    for (int i = 1; i <= count; ++i) {
        const uint8_t *row = records[i];
        if (row == nullptr)
            continue;
        const uint32_t rowSpell = Game::Read<uint32_t>(row, Offsets::OFF_SLA_SPELL_ID);
        if (rowSpell != spellId) {
            const char *rowName =
                Spell::Lookup::Name(Spell::Lookup::RecordForID(static_cast<int>(rowSpell)));
            if (rowName == nullptr || std::strcmp(rowName, name) != 0)
                continue;
        }
        bool seen = false;
        for (const uint32_t s : out)
            seen = seen || s == rowSpell;
        if (!seen)
            out.push_back(rowSpell);
    }
    return out;
}

bool PlayerKnows(const std::vector<uint32_t> &teachers) {
    for (const uint32_t s : teachers) {
        if (Spell::Known::IsKnown(s))
            return true;
    }
    return false;
}

bool ClassCanLearn(const void *unit, const std::vector<uint32_t> &teachers) {
    const uint8_t *fields = Object::Fields(unit);
    if (fields == nullptr)
        return false;
    const uint32_t race = Game::Read<uint8_t>(fields, Offsets::OFF_UNIT_DESCRIPTOR_RACE_BYTE);
    const uint32_t classId = Game::Read<uint8_t>(fields, Offsets::OFF_UNIT_DESCRIPTOR_CLASS_BYTE);
    for (const uint32_t s : teachers) {
        if (Spell::SkillLine::AbilityFor(race, classId, s) != nullptr)
            return true;
    }
    return false;
}

bool Carries(const void *unit, const char *name) {
    return Data::FindSlotBySpellName(static_cast<const uint8_t *>(unit), name, nullptr) >= 0;
}

} // namespace

uint64_t OfPartyAura(uint64_t targetGuid, uint32_t spellId) {
    const uint8_t *rec = Spell::Lookup::RecordForID(static_cast<int>(spellId));
    if (rec == nullptr ||
        !Spell::Lookup::HasEffect(rec, Offsets::SPELL_EFFECT_APPLY_AREA_AURA_PARTY))
        return 0;
    const char *name = Spell::Lookup::Name(rec);
    if (name == nullptr)
        return 0;
    const std::vector<uint32_t> &teachers = Teachers(spellId, name);
    if (teachers.empty())
        return 0;

    const uint64_t player = Unit::Identity::PlayerGuid();
    if ((targetGuid == player || Group::Roster::IsPartyMember(targetGuid)) &&
        PlayerKnows(teachers) && Carries(Unit::Identity::PlayerObject(), name))
        return player;

    float targetPos[3];
    if (!Unit::Position::Read(Object::ByGuid(Offsets::TYPEMASK_UNIT, targetGuid, nullptr, 0),
                              targetPos))
        return 0;
    uint64_t best = 0;
    float bestDistSq = FLT_MAX;
    Object::ForEachVisible([&](uint64_t guid) {
        // The local player was decided above, from what it knows.
        if (!Guid::IsPlayer(guid) || guid == player)
            return true;
        void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, nullptr, 0);
        if (unit == nullptr || !Carries(unit, name) || !ClassCanLearn(unit, teachers))
            return true;
        if (guid == targetGuid) {
            best = guid;
            return false; // the target's own aura: nothing beats it
        }
        float pos[3];
        if (!Unit::Position::Read(unit, pos))
            return true;
        const float distSq = Game::DistanceSq(pos, targetPos);
        if (distSq < bestDistSq) {
            best = guid;
            bestDistSq = distSq;
        }
        return true;
    });
    return best;
}

} // namespace Aura::AreaCaster
