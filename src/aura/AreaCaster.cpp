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

// Who cast a party area aura.
//
// A party area aura (Spell.dbc effect APPLY_AREA_AURA_PARTY at the caster) is
// put on the caster, and the server copies it onto the caster's group members
// in range (vmangos AreaAura::Update). A 1.12 aura slot holds only the spell
// ID, so no copy names the caster. The caster keeps its own copy in a visible
// slot, so the search is over the visible units that carry the aura, matched
// by its localized name: a target below the spell's level gets a lower rank of
// the same line (SelectAuraRankForLevel), and every rank shares the name.
//
// Two kinds of caster, the totems first:
//   - a totem (Strength of Earth, the resistance totems), attributed to its
//     owner as every totem aura is (Totem::Owner). The totem keeps its copy:
//     IsNeedVisibleSlot's "passive auras (except totem auras) do not get
//     placed in caster slot", on both vmangos and tortoise-wow;
//   - a player (Leader of the Pack, the paladin auras, Trueshot Aura, Moonkin
//     Aura). Leader of the Pack has no SMSG_SPELL_GO at all: the server casts
//     it from the shapeshift, and Era's log has no cast line for it either.
//     Classic Era names the druid on every copy, its own included. The local
//     player is the caster when it knows the spell (its known-spell bitmap),
//     carries the aura, and the aura is on itself or a party member (party1-4,
//     which in a raid are its own subgroup, the reach of a party aura).
//     Another player can be the caster when its race and class can learn the
//     spell (the engine's SkillLineAbility lookup, which also applies
//     SkillRaceClassInfo, so Trueshot Aura's class-less row still resolves to
//     hunters).
// Of the candidates of a kind, the target's own wins, then the nearest. The
// client cannot see a stranger's group, so two groups' identical totems side
// by side, or two players of a class carrying the same aura (two paladins on
// one aura, a druid without the talent beside one with it), are told apart
// only by that order.
//
// A spell can learn through a spell of the same name: Leader of the Pack's
// aura (24932) has no SkillLineAbility row, but the druid talent that grants
// it (17007) shares its name. Of the 181 party area auras in the 1.12
// Spell.dbc, 35 have their own row and 19 more match one by name (Leader of
// the Pack, Devotion and Retribution Aura ranks); the rest are totem, item
// and creature auras.

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
#include "totem/Owner.h"
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
    for (const uint32_t s : teachers) {
        if (Spell::SkillLine::AbilityForUnit(fields, s) != nullptr)
            return true;
    }
    return false;
}

bool Carries(const void *unit, const char *name) {
    return Data::FindSlotBySpellName(static_cast<const uint8_t *>(unit), name, nullptr) >= 0;
}

// The best candidate of one kind: the target's own, else the nearest.
struct Best {
    uint64_t caster = 0;
    float distSq = FLT_MAX;
    bool own = false;

    void Offer(uint64_t candidate, const void *unit, uint64_t target, const float targetPos[3]) {
        if (own)
            return;
        if (candidate == target) {
            caster = candidate;
            own = true;
            return;
        }
        float pos[3];
        if (!Unit::Position::Read(const_cast<void *>(unit), pos))
            return;
        const float d = Game::DistanceSq(pos, targetPos);
        if (d < distSq) {
            caster = candidate;
            distSq = d;
        }
    }
};

} // namespace

uint64_t OfPartyAura(uint64_t targetGuid, uint32_t spellId) {
    const uint8_t *rec = Spell::Lookup::RecordForID(static_cast<int>(spellId));
    if (rec == nullptr ||
        !Spell::Lookup::HasEffect(rec, Offsets::SPELL_EFFECT_APPLY_AREA_AURA_PARTY))
        return 0;
    const char *name = Spell::Lookup::Name(rec);
    if (name == nullptr)
        return 0;
    float targetPos[3];
    if (!Unit::Position::Read(Object::ByGuid(Offsets::TYPEMASK_UNIT, targetGuid, nullptr, 0),
                              targetPos))
        return 0;
    const std::vector<uint32_t> &teachers = Teachers(spellId, name);
    const uint64_t player = Unit::Identity::PlayerGuid();

    Best totems, players;
    Object::ForEachVisible([&](uint64_t guid) {
        uint64_t caster = 0;
        if (Guid::IsCreature(guid)) {
            caster = Totem::Owner::OfTotem(guid);
        } else if (Guid::IsPlayer(guid) && guid != player && !teachers.empty()) {
            // The local player is decided below, from what it knows.
            caster = guid;
        }
        if (caster == 0)
            return true;
        void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, nullptr, 0);
        if (unit == nullptr)
            return true;
        const bool totem = caster != guid;
        if (!totem && !ClassCanLearn(unit, teachers))
            return true;
        if (!Carries(unit, name))
            return true;
        (totem ? totems : players).Offer(caster, unit, targetGuid, targetPos);
        return !totems.own; // the target's own totem: nothing beats it
    });
    if (totems.caster != 0)
        return totems.caster;
    if ((targetGuid == player || Group::Roster::IsPartyMember(targetGuid)) &&
        PlayerKnows(teachers) && Carries(Unit::Identity::PlayerObject(), name))
        return player;
    return players.caster;
}

} // namespace Aura::AreaCaster
