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

// The combat log file's COMBATANT_INFO lines: one for the player and each
// group member in view, right after an ENCOUNTER_START, as Era writes them
// (756 lines over its 19 pulls, one per raid member).
//
// Era's 32 fields, read off all 756 of its lines:
//   COMBATANT_INFO, playerGUID, faction (0 Horde, 1 Alliance),
//   strength, agility, stamina, intellect, spirit,
//   15 zeros, armor, 0, (talent points per tree), (),
//   [19 gear entries], [auras], 0, 0, ()
// A gear entry is (itemID, itemLevel, (permanent enchant, temporary enchant,
// 0), (), ()), with () for an item with neither enchant and (0,0,(),(),())
// for an empty slot, in equipment-slot order, shirt and tabard included. The
// auras are flat casterGUID, spellID, 1 triples.
//
// What a 1.12 client knows:
//   - stats, armor and talent points: the player's own only. The server sends
//     UNIT_FIELD_STAT0..4 and RESISTANCES to the unit itself (tortoise-wow
//     Player::updateVisualBits leaves them out), and nobody else's talents.
//     Another player's line has 0 for them and (0,0,0) for the talents;
//   - gear: every player in view, from the broadcast visible items, with both
//     enchants. The item level is the item record's; while a record is not
//     cached it is 0 and the record is requested;
//   - auras: Era lists a curated set (world buffs, Resist Fire, warrior
//     stances) chosen by a flag in modern spell data that 1.12 does not
//     have. This lists every helpful aura the player carries, a superset, with
//     the caster Aura::Source knows (0000000000000000 when none).
// Field 24 (0 on 430 of Era's lines, small numbers on the rest) is not known
// and is written 0.

#include "combatlog/Combatant.h"

#include "Game.h"
#include "Offsets.h"
#include "aura/Data.h"
#include "aura/Source.h"
#include "combatlog/File.h"
#include "creature/Info.h"
#include "group/Roster.h"
#include "item/Data.h"
#include "item/ID.h"
#include "object/Resolve.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace CombatLog::Combatant {

namespace {

constexpr int kStatCount = 5;
constexpr int kUnknownStats = 15;
constexpr int kTalentTrees = 3;

void Number(std::string &line, long long value) {
    char text[24];
    std::snprintf(text, sizeof text, ",%lld", value);
    line += text;
}

// 0 Horde, 1 Alliance, by the race's faction group.
int Faction(const uint8_t *fields) {
    const char *english = nullptr;
    const char *localized = nullptr;
    return Creature::Info::FactionGroupForRace(Unit::Identity::Race(fields), &english,
                                               &localized) &&
                   std::strcmp(english, "Alliance") == 0
               ? 1
               : 0;
}

// The player's points in each talent tree (TabInfo pointsSpent).
std::string TalentPoints(bool own) {
    std::string out = "(";
    const int tabs = own ? Game::Read<int>(Offsets::VAR_TALENT_TAB_COUNT) : 0;
    const auto *const *info = Game::Read<const uint8_t *const *>(
        static_cast<uintptr_t>(Offsets::VAR_TALENT_TAB_INFO_ARRAY));
    for (int i = 0; i < kTalentTrees; ++i) {
        int points = 0;
        if (i < tabs && info != nullptr && info[i] != nullptr)
            points = Game::Read<int>(info[i], Offsets::OFF_TABINFO_POINTS_SPENT);
        char text[16];
        std::snprintf(text, sizeof text, i == 0 ? "%d" : ",%d", points);
        out += text;
    }
    return out + ")";
}

std::string Gear(void *player) {
    std::string out = ",[";
    for (int slot = Offsets::EQUIPMENT_SLOT_FIRST; slot <= Offsets::EQUIPMENT_SLOT_LAST; ++slot) {
        if (slot != Offsets::EQUIPMENT_SLOT_FIRST)
            out += ',';
        const uint8_t *entry = Item::ID::VisibleEntry(player, slot - 1);
        const uint32_t itemId =
            entry != nullptr ? Game::Read<uint32_t>(entry, Offsets::OFF_VISIBLE_ITEM_ITEM_ID) : 0;
        if (itemId == 0) {
            out += "(0,0,(),(),())";
            continue;
        }
        const uint8_t *record = Item::Data::RecordOrRequest(itemId);
        const uint32_t level =
            record != nullptr ? Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_ITEM_LEVEL) : 0;
        const uint32_t permanent = Game::Read<uint32_t>(entry, Offsets::OFF_VISIBLE_ITEM_ENCHANTS);
        const uint32_t temporary =
            Game::Read<uint32_t>(entry, Offsets::OFF_VISIBLE_ITEM_ENCHANTS + 4);
        char text[96];
        if (permanent == 0 && temporary == 0)
            std::snprintf(text, sizeof text, "(%u,%u,(),(),())", itemId, level);
        else
            std::snprintf(text, sizeof text, "(%u,%u,(%u,%u,0),(),())", itemId, level, permanent,
                          temporary);
        out += text;
    }
    return out + "]";
}

std::string Auras(const uint8_t *unit, uint64_t guid) {
    uint32_t spells[Offsets::UNIT_AURA_TOTAL] = {};
    for (int slot = 0; slot < Offsets::UNIT_AURA_TOTAL; ++slot)
        spells[slot] = Aura::Data::ReadSpellID(unit, slot);
    uint64_t casters[Offsets::UNIT_AURA_TOTAL] = {};
    Aura::Source::CastersForSlots(guid, spells, casters);
    std::string out = ",[";
    bool first = true;
    for (int slot = 0; slot < Offsets::UNIT_AURA_TOTAL; ++slot) {
        if (!Aura::Data::IsSlotPopulated(unit, slot) || Aura::Data::IsSlotHarmful(unit, slot))
            continue;
        std::string triple;
        File::AppendGuid(triple, casters[slot]);
        Number(triple, spells[slot]);
        Number(triple, 1);
        out += first ? triple.substr(1) : triple;
        first = false;
    }
    return out + "]";
}

void Write(uint64_t guid) {
    void *unit = Object::ByGuid(Offsets::TYPEMASK_PLAYER, guid);
    const uint8_t *fields = Object::Fields(unit);
    if (fields == nullptr)
        return;
    const bool own = guid == Unit::Identity::PlayerGuid();
    std::string line = "COMBATANT_INFO";
    File::AppendGuid(line, guid);
    Number(line, Faction(fields));
    for (int i = 0; i < kStatCount; ++i)
        Number(line, own ? Game::Read<int>(fields, Offsets::OFF_UNIT_FIELD_STAT0 + i * 4) : 0);
    for (int i = 0; i < kUnknownStats; ++i)
        line += ",0";
    Number(line, own ? Game::Read<int>(fields, Offsets::OFF_UNIT_FIELD_RESISTANCE_ARMOR) : 0);
    line += ",0,";
    line += TalentPoints(own);
    line += ",()";
    line += Gear(unit);
    line += Auras(static_cast<const uint8_t *>(unit), guid);
    line += ",0,0,()";
    File::WriteLine(line);
}

} // namespace

void WriteGroup() {
    Write(Unit::Identity::PlayerGuid());
    const int raid = Group::Roster::RaidCount();
    for (int i = 0; i < raid; ++i) {
        const uint64_t guid = Group::Roster::RaidGuid(i);
        if (guid != Unit::Identity::PlayerGuid())
            Write(guid);
    }
    if (raid != 0)
        return;
    for (int i = 0; i < Offsets::PARTY_MAX_SLOTS; ++i) {
        if (const uint64_t guid = Group::Roster::PartyGuid(i))
            Write(guid);
    }
}

} // namespace CombatLog::Combatant
