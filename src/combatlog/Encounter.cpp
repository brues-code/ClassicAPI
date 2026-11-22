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

// The combat log file's ENCOUNTER_START / ENCOUNTER_END lines.
//
// Era writes them from its DungeonEncounter table, which 1.12 does not have
// and Turtle's own bosses would not be in. The client's own mark of a boss is
// the creature's rank: 3, world boss, in its creature cache record
// (OFF_CREATURE_RANK, the field UnitClassification reads). Raid and world
// bosses carry it, Turtle's custom raids included; most dungeon bosses are
// only elite, so dungeons get no encounter lines.
//
// The lines, as Era writes them:
//   ENCOUNTER_START,encounterID,"name",difficultyID,groupSize,instanceID,2
//   ENCOUNTER_END,encounterID,"name",difficultyID,groupSize,success
// Era's last ENCOUNTER_START value is 2 on all 226 of its lines; success is 1
// for a kill and 0 for a wipe (206 and 19 in its logs).
//   - encounterID: the boss's creature ID, since no encounter IDs exist here;
//     name: the boss's name;
//   - groupSize: Era writes the instance's size (40, or 20 for ZG and AQ20),
//     which 1.12's Map.dbc does not carry (docs/notes/map-dbc.md). This is
//     the group's size: the raid roster, else the party and the player;
//   - difficultyID: Era's 9 for a 40-player raid and 148 for a 20-player
//     one. A raid of more than 20 is 9, else 148, which matches Era unless a
//     40-player raid is run with 20 or fewer. A dungeon is 1 and anywhere
//     else 0;
//   - instanceID: the map ID, as Era writes it.
//
// When:
//   - start: the first entry between a hostile world boss (by the entry's
//     reaction flags) and a unit of the group (the player, a party or raid
//     member, or a pet of theirs, by the entry's affiliation flags), other
//     than a kill or an aura falling off. Later bosses the group fights join
//     the encounter (Twin Emperors, the Four Horsemen);
//   - kill: an engaged boss dies and every engaged boss is dead, friendly
//     (Majordomo Executus surrenders) or gone from view (Lord Victor
//     Nefarius leaves before Nefarian dies). A boss that turns friendly
//     without a death ends it the same way, at the next frame;
//   - wipe: the group has been seen in combat and no member in view is in
//     combat now (UNIT_FLAG_IN_COMBAT), with an engaged boss neither dead nor
//     friendly. Seeing combat first keeps the pull's first frames, before the
//     server's combat flag arrives, from ending it. Members out of view count
//     as out of combat, so a logger who releases while the raid fights on
//     sees a wipe;
//   - a loading screen ends it as a wipe.
// The end line goes only into the log file that has the start line, so
// turning logging on mid-fight writes neither.
// The kill's ENCOUNTER_END comes before the boss's UNIT_DIED line, as in
// Era's log (Razorgore, Vaelastrasz).
//
// The encounter's bosses, from their start or join until the end, are also the
// units of the `bossN` tokens (unit/Boss.cpp).

#include "combatlog/Encounter.h"

#include "Game.h"
#include "Offsets.h"
#include "combatlog/Combatant.h"
#include "combatlog/File.h"
#include "creature/Info.h"
#include "event/EnteringWorld.h"
#include "group/Roster.h"
#include "guid/Guid.h"
#include "map/Instance.h"
#include "object/Resolve.h"
#include "tick/FrameTick.h"
#include "unit/Boss.h"
#include "unit/CreatureID.h"
#include "unit/Identity.h"
#include "unit/Relation.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace CombatLog::Encounter {

namespace {

constexpr uint32_t kWorldBossRank = 3;
constexpr int kStartTail = 2;
constexpr int kInstanceDungeon = 1;
constexpr int kInstanceRaid = 2;
constexpr int kDifficultyDungeon = 1;
constexpr int kDifficultyRaid40 = 9;
constexpr int kDifficultyRaid20 = 148;
constexpr int kRaid20Size = 20;

struct Boss {
    uint64_t guid;
    bool dead;
};

bool g_active = false;
uint32_t g_serial = 0; // counts encounter starts
bool g_sawFighting = false;
// The log file the start line went to (File::Session), 0 when logging was off.
uint32_t g_session = 0;
uint32_t g_id = 0;
std::string g_name;
int g_difficulty = 0;
int g_groupSize = 0;
std::vector<Boss> g_bosses;

bool IsWorldBoss(uint64_t guid) {
    if (!Guid::IsCreature(guid))
        return false;
    const uint8_t *rec = Creature::Info::CacheRecord(Unit::CreatureID::ForGuid(guid));
    return rec != nullptr && Game::Read<uint32_t>(rec, Offsets::OFF_CREATURE_RANK) == kWorldBossRank;
}

void *UnitOf(uint64_t guid) { return Object::ByGuid(Offsets::TYPEMASK_UNIT, guid); }

bool Friendly(const void *unit) {
    const void *player = Unit::Identity::PlayerObject();
    return unit != nullptr && player != nullptr && ::Unit::Relation::Reaction(unit, player) >= 4;
}

bool InCombat(uint64_t guid) {
    const uint8_t *fields = Object::Fields(UnitOf(guid));
    return fields != nullptr && (Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FLAGS) &
                                 Offsets::UNIT_FLAG_IN_COMBAT) != 0;
}

// Every engaged boss is beaten: dead or friendly, and with `goneCounts` also
// gone from view.
bool Beaten(bool goneCounts) {
    for (const Boss &b : g_bosses) {
        if (b.dead)
            continue;
        const void *unit = UnitOf(b.guid);
        if (unit == nullptr ? !goneCounts : !Friendly(unit))
            return false;
    }
    return true;
}

// encounterID,"name",difficultyID,groupSize
std::string Fields() {
    char text[48];
    std::snprintf(text, sizeof text, ",%u", g_id);
    std::string line = text;
    File::AppendQuoted(line, g_name.c_str());
    std::snprintf(text, sizeof text, ",%d,%d", g_difficulty, g_groupSize);
    line += text;
    return line;
}

void Start(uint64_t boss, const std::string &name) {
    g_active = true;
    ++g_serial;
    g_sawFighting = false;
    g_id = Unit::CreatureID::ForGuid(boss);
    g_name = name;
    g_groupSize = GroupSize();
    g_difficulty = Difficulty(g_groupSize);
    g_bosses.assign(1, Boss{boss, false});
    Unit::Boss::Changed(boss);
    g_session = File::Session();
    char tail[32];
    std::snprintf(tail, sizeof tail, ",%d,%d", Game::Read<int>(Offsets::VAR_CURRENT_MAP_ID),
                  kStartTail);
    File::WriteLine("ENCOUNTER_START" + Fields() + tail);
    Combatant::WriteGroup();
}

void End(bool success) {
    // Only into the file that has the start line: logging turned on, or
    // restarted, mid-fight leaves no end without its start.
    if (g_session != 0 && File::Session() == g_session)
        File::WriteLine("ENCOUNTER_END" + Fields() + (success ? ",1" : ",0"));
    g_active = false;
    // The bossN tokens keep every boss, dead or not, until here.
    std::vector<Boss> gone;
    gone.swap(g_bosses);
    for (const Boss &b : gone)
        Unit::Boss::Changed(b.guid);
}

Boss *Engaged(uint64_t guid) {
    for (Boss &b : g_bosses) {
        if (b.guid == guid)
            return &b;
    }
    return nullptr;
}

bool Dead(const void *unit) {
    const uint8_t *fields = Object::Fields(unit);
    return fields != nullptr && Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_HEALTH) == 0;
}

// A boss in an entry with a unit of the group, hostile by the entry's own
// reaction flags: a boss that evades or turns neutral starts nothing. A dead
// boss starts nothing either: the 1.12 server sends the killing blow's damage
// after the death, and a dead boss's area spells (Gehennas's Rain of Fire)
// keep hitting.
void Engage(uint64_t boss, uint32_t bossFlags, const std::string &name) {
    if ((bossFlags & ObjectFlag::REACTION_HOSTILE) == 0 || !IsWorldBoss(boss) || Dead(UnitOf(boss)))
        return;
    if (!g_active)
        Start(boss, name);
    else if (Engaged(boss) == nullptr) {
        g_bosses.push_back(Boss{boss, false});
        Unit::Boss::Changed(boss);
    }
}

void OnFrame() {
    if (!g_active)
        return;
    if (Beaten(false)) {
        End(true);
        return;
    }
    if (GroupFighting())
        g_sawFighting = true;
    else if (g_sawFighting)
        End(false);
}

const Tick::FrameTick::AutoSubscribe _frame{&OnFrame};

void OnEnterWorld() {
    if (g_active)
        End(false);
}

const Event::EnteringWorld::AutoObserve _enterWorld{&OnEnterWorld};

} // namespace

bool GroupFighting() {
    if (InCombat(Unit::Identity::PlayerGuid()))
        return true;
    for (int i = 0; i < Group::Roster::RaidCount(); ++i) {
        if (InCombat(Group::Roster::RaidGuid(i)))
            return true;
    }
    for (int i = 0; i < Offsets::PARTY_MAX_SLOTS; ++i) {
        const uint64_t guid = Group::Roster::PartyGuid(i);
        if (guid != 0 && InCombat(guid))
            return true;
    }
    return false;
}

bool Active() { return g_active; }

uint32_t Serial() { return g_serial; }

const std::string &BossName() { return g_name; }

uint64_t EngagedBoss(int index) {
    if (index < 0 || index >= static_cast<int>(g_bosses.size()))
        return 0;
    return g_bosses[index].guid;
}

int GroupSize() {
    const int raid = Group::Roster::RaidCount();
    return raid != 0 ? raid : Group::Roster::PartyCount() + 1;
}

int Difficulty(int groupSize) {
    switch (Map::Instance::CurrentType()) {
    case kInstanceDungeon:
        return kDifficultyDungeon;
    case kInstanceRaid:
        return groupSize > kRaid20Size ? kDifficultyRaid40 : kDifficultyRaid20;
    default:
        return 0;
    }
}

void Observe(const Entry &e) {
    if (e.event == UNIT_DIED || e.event == UNIT_DESTROYED) {
        Boss *b = g_active ? Engaged(e.dest) : nullptr;
        if (b == nullptr)
            return;
        b->dead = true;
        if (Beaten(true))
            End(true);
        return;
    }
    // A kill, or an aura falling off, is no pull.
    if (e.event == PARTY_KILL || e.event == SPELL_AURA_REMOVED ||
        e.event == SPELL_AURA_REMOVED_DOSE)
        return;
    if (e.source != 0 && (e.destFlags & ObjectFlag::AFFILIATION_GROUP) != 0)
        Engage(e.source, e.sourceFlags, e.sourceName);
    if (e.dest != 0 && (e.sourceFlags & ObjectFlag::AFFILIATION_GROUP) != 0)
        Engage(e.dest, e.destFlags, e.destName);
}

} // namespace CombatLog::Encounter
