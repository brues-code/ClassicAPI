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

// The combat log file, in Classic Era's COMBAT_LOG_VERSION 9 layout, written
// while LoggingCombat is on.
//
// The oracle is a real Era 1.15.7 raid log (40 players, BWL + MC + Onyxia):
// every line shape below was taken from it, subevent by subevent, not from a
// format description. Era writes advanced logging on; with it off, retail
// writes the same lines without the advanced block.
//
// The pieces:
//   - the switch is LoggingCombat's own flag, so /combatlog and
//     LoggingCombat() turn both files on and off, and logging out (the chat
//     teardown in CGGameUI::ShutdownGame, 0x00498700, clears the flag) stops
//     both. 1.12's own Logs\WoWCombatLog.txt keeps its chat-text lines, which
//     1.12 log readers parse;
//   - each time the flag turns on, a new file starts, named as retail names
//     it: Logs\WoWCombatLog-MMDDYY_HHMMSS.txt, the local time logging started;
//   - the file is written through LogFile, whose stamp is Era's
//     ("5/24/2025 20:40:21.890-5  ");
//   - the header line (COMBAT_LOG_VERSION, ADVANCED_LOG_ENABLED, BUILD_VERSION,
//     PROJECT_ID) starts the file and is written again on every world entry,
//     as Era writes it after each loading screen. A ZONE_CHANGE follows each
//     one (map ID, the real zone name, the difficulty the encounter lines
//     use), and another comes with a new zone entered without a loading
//     screen. BUILD_VERSION is the
//     client's GetBuildInfo version (VAR_BUILD_VERSION_STRING); PROJECT_ID is
//     Era's 2, WOW_PROJECT_CLASSIC;
//   - GUIDs are written as retail's strings (Player-0-0000ABCD,
//     Creature-0-0-<map>-0-<npc id>-<spawn>), with 0 for the server and
//     instance numbers 1.12 does not have. Player names carry "-<realm>"; the
//     region retail adds after it is unknown to a 1.12 client and left off.
//
// Values a 1.12 client is never sent, and what is written for them:
//   - baseAmount (damage and heals, and the second ABSORB amount): the
//     amount itself. Era's value is the server's pre-mitigation amount;
//   - overEnergize: 0.
//
// Advanced logging (the advancedCombatLogging cvar, retail's name, help,
// category and default) adds Era's advanced block after the spell prefix:
//   infoGUID, ownerGUID, currentHP, maxHP, attackPower, spellPower, armor,
//   absorb, 0, powerType, currentPower, maxPower, powerCost, positionX,
//   positionY, uiMapID, facing, level
// on the subevents Era gives it, and a SWING_DAMAGE_LANDED line after each
// SWING_DAMAGE. The unit it describes is the source for SPELL_CAST_SUCCESS and
// SWING_DAMAGE and the dest for every other one, SWING_DAMAGE_LANDED
// included: in the Era log every line with distinct source and dest follows
// that rule, and a unit the client cannot resolve gets a block of zeros, as
// Era writes for one it cannot. Field by field:
//   - ownerGUID: the unit's charmer, creator or summoner (Era sets it on pets,
//     guardians and totems);
//   - HP: a player's as a percent of 100, by the server's own rule for a
//     player outside the group (floor(health * 100 / max), at least 1 while
//     alive); a creature's raw. Era writes both that way;
//   - attack power, spell power, armor, absorb: 0, as Era writes them;
//   - power: a player's power type, current and max in stored units (rage in
//     tenths), as MoP Classic 5.5.4's log writes them. Era leaves them empty
//     (-1,0,0,0) and writes that for every other unit, as MoP does. powerCost
//     stays 0: no 1.12 source for it is verified;
//   - position: the world x and y with two decimals; uiMapID 0, as Era writes
//     it; facing in radians with four decimals;
//   - level: a creature's level; for a player, the average item level Era
//     writes there: 16 equipment slots (all but shirt, tabard and ranged),
//     with an empty off hand counting the main hand again, rounded down. This
//     matched 682 of the 756 COMBATANT_INFO gear lists in the Era log against
//     the same player's next advanced line (4.3.4's GetAverageItemLevel rule,
//     with the ranged slot, matched 351). While an item's record is missing
//     the level is 0 and the record is requested.
//
// The flag is read once a frame (FrameTick, which also runs on the glue
// screen, so a logout's cleared flag is seen there) and before each line.
// LoggingCombat itself is not hooked: other DLLs add their own logs to it.

#include "combatlog/File.h"

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "combatlog/Encounter.h"
#include "combatlog/LogFile.h"
#include "event/EnteringWorld.h"
#include "event/SignalHook.h"
#include "guid/Guid.h"
#include "item/Data.h"
#include "item/ID.h"
#include "object/Resolve.h"
#include "spell/Lookup.h"
#include "tick/FrameTick.h"
#include "unit/CreatureID.h"
#include "unit/Identity.h"
#include "unit/Position.h"
#include "unit/Power.h"

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <unordered_map>

namespace CombatLog::File {

namespace {

constexpr int kLogVersion = 9;
constexpr int kProjectId = 2; // WOW_PROJECT_CLASSIC, as Era writes it
constexpr int kSpellEffects = 3;
constexpr int kPowerTypes = 5;
// 1.12 stores pet happiness as power type 4; the file writes retail's
// Enum.PowerType.Happiness, as Era does.
constexpr int kHappiness = 4;
constexpr int kHappinessModern = 27;

// Implicit targets that name one unit. A damage effect with a radius is AOE
// unless it aims at one of these. Checked against the Era log: of the 300
// damage spells it tags, all 300 match this rule over the 1.12 Spell.dbc
// (Effect, EffectImplicitTargetA, EffectRadiusIndex). The radius alone gets
// one wrong: 19632, radius 13 at TARGET_UNIT_ENEMY, which Era tags ST.
constexpr int kSingleUnitTargets[] = {
    5,  // TARGET_UNIT_CASTER_PET
    6,  // TARGET_UNIT_ENEMY
    21, // TARGET_UNIT_FRIEND
    25, // TARGET_UNIT
    27, // TARGET_UNIT_CASTER_MASTER
    35, // TARGET_UNIT_PARTY
    45, // TARGET_UNIT_FRIEND_CHAIN_HEAL
    57, // TARGET_UNIT_RAID
};

// Era's environmental type names (its ENVIRONMENTAL_DAMAGE lines: Falling,
// Lava, Fire), in the server's type order.
constexpr const char *kEnvironmentalNames[] = {"Fatigue", "Drowning", "Falling",
                                               "Lava",    "Slime",    "Fire"};

LogFile g_log;
uint32_t g_session = 0; // files opened this session
std::string g_fileTime; // the open file's MMDDYY_HHMMSS

// The template id in the GUID string of each creature, pet and game object
// written since the last header. Retail's GUID string packs it, so a unit's
// string never changes; 1.12's only comes from the live object: a pet's GUID
// packs the pet number, and a creature's packs the spawn row's first id,
// not the rolled one (Unit::CreatureID). Kept from the first line, the
// string stays the same after the unit leaves view (UNIT_DIED for a pet
// gone at once, an aura removed after a dismiss, UNIT_GONE).
std::unordered_map<uint64_t, uint32_t> g_templates;

CVar::Factory::Handle g_advancedCvar = nullptr;
// ADVANCED_LOG_ENABLED of the last header written.
bool g_headerAdvanced = false;

bool Logging() { return Game::Read<int>(Offsets::VAR_COMBAT_LOGGING) != 0; }

void RegisterCVars() {
    g_advancedCvar = CVar::Factory::Register(
        "advancedCombatLogging", "Whether we want advanced combat log data sent from the server",
        Offsets::CVAR_CATEGORY_GAME, "0");
}

const Game::ModuleAutoRegister _autoreg{&RegisterCVars};

void Emit(const std::string &body) { g_log.Emit(body); }

// The zone the last ZONE_CHANGE named.
int g_zoneMap = -1;
std::string g_zoneName;

// The real zone name (GetRealZoneText), or Era's "UNKNOWN AREA" while it is
// empty.
std::string ZoneName() {
    const char *name = Game::Read<const char *>(Offsets::VAR_REAL_ZONE_TEXT);
    return name != nullptr && *name != '\0' ? name : "UNKNOWN AREA";
}

// ZONE_CHANGE,mapID,"zone",difficultyID
void ZoneChange() {
    g_zoneMap = Game::Read<int>(Offsets::VAR_CURRENT_MAP_ID);
    g_zoneName = ZoneName();
    char text[32];
    std::snprintf(text, sizeof text, "ZONE_CHANGE,%d", g_zoneMap);
    std::string line = text;
    AppendQuoted(line, g_zoneName.c_str());
    std::snprintf(text, sizeof text, ",%d", Encounter::Difficulty(Encounter::GroupSize()));
    line += text;
    Emit(line);
}

// The header line, and the ZONE_CHANGE Era writes after each one.
void Header() {
    const char *version = reinterpret_cast<const char *>(
        static_cast<uintptr_t>(Offsets::VAR_BUILD_VERSION_STRING));
    g_headerAdvanced = Advanced();
    g_templates.clear();
    char text[128];
    std::snprintf(text, sizeof text,
                  "COMBAT_LOG_VERSION,%d,ADVANCED_LOG_ENABLED,%d,BUILD_VERSION,%s,PROJECT_ID,%d",
                  kLogVersion, g_headerAdvanced ? 1 : 0, version, kProjectId);
    Emit(text);
    ZoneChange();
}

// A new log named for the local time now, and its header. True when it is open.
bool Open() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    if (localtime_s(&local, &now) != 0)
        return false;
    char fileTime[16];
    std::strftime(fileTime, sizeof fileTime, "%m%d%y_%H%M%S", &local);
    if (!g_log.Open("WoWCombatLog", fileTime))
        return false;
    g_fileTime = fileTime;
    ++g_session;
    Header();
    return true;
}

// Follows the flag: a new file when it turns on, the file closed when it
// turns off, and the buffer flushed once a second in between.
void OnFrame() {
    const bool logging = Logging();
    if (logging && !g_log.IsOpen())
        Open();
    else if (!logging)
        g_log.Close();
    g_log.Tick();
}

const Tick::FrameTick::AutoSubscribe _frame{&OnFrame};

// Era writes the header again after each loading screen.
void OnEnterWorld() {
    if (g_log.IsOpen())
        Header();
}

const Event::EnteringWorld::AutoObserve _enterWorld{&OnEnterWorld};

// A new zone without a loading screen (walking out of a city) gets its own
// ZONE_CHANGE, as in Era's log. The engine fires ZONE_CHANGED_NEW_AREA after
// storing the zone names; one the last line already named writes nothing.
bool OnSignal(int eventID) {
    if (eventID == Offsets::EVENT_ZONE_CHANGED_NEW_AREA && g_log.IsOpen() &&
        (Game::Read<int>(Offsets::VAR_CURRENT_MAP_ID) != g_zoneMap || ZoneName() != g_zoneName))
        ZoneChange();
    return false;
}

const Event::SignalHook::AutoSubscribe _zone{&OnSignal};

void Plain(std::string &line, const char *text) {
    line += ',';
    line += text;
}

void Quoted(std::string &line, const char *text) { AppendQuoted(line, text); }

void Number(std::string &line, int value) {
    char text[16];
    std::snprintf(text, sizeof text, ",%d", value);
    line += text;
}

// Energize amounts: Era writes them with four decimals ("737.0000").
void Decimal(std::string &line, int value) {
    char text[24];
    std::snprintf(text, sizeof text, ",%d.0000", value);
    line += text;
}

void Flag(std::string &line, bool value) { line += value ? ",1" : ",nil"; }

// The template id now: the live object's when it is in view, else the one
// the GUID packs (none for a pet).
uint32_t LiveTemplate(uint64_t guid, ::Guid::Type type) {
    if (type != ::Guid::Type::GameObject)
        return Unit::CreatureID::ForGuid(guid);
    if (void *object = Object::ByGuid(Offsets::TYPEMASK_GAMEOBJECT, guid))
        return Object::Entry(object);
    return static_cast<uint32_t>(guid >> 24) & 0xFFFFFF;
}

uint32_t TemplateId(uint64_t guid, ::Guid::Type type) {
    const auto it = g_templates.find(guid);
    if (it != g_templates.end())
        return it->second;
    const uint32_t id = LiveTemplate(guid, type);
    if (id != 0)
        g_templates.emplace(guid, id);
    return id;
}

// Retail's GUID strings. 1.12 has no server, instance or zone numbers, so
// those parts are 0; the map is the one the player is on, which every object
// in view shares. The spawn part is the GUID's per-spawn counter.
void GuidPart(std::string &line, uint64_t guid) {
    const uint32_t low = static_cast<uint32_t>(guid);
    const uint32_t spawn = low & 0xFFFFFF;
    const int map = Game::Read<int>(Offsets::VAR_CURRENT_MAP_ID);
    char text[64];
    const ::Guid::Type type = ::Guid::Classify(guid);
    switch (type) {
    case ::Guid::Type::Player:
        std::snprintf(text, sizeof text, ",Player-0-%08X", low);
        break;
    case ::Guid::Type::Creature:
        std::snprintf(text, sizeof text, ",Creature-0-0-%d-0-%u-%010X", map,
                      TemplateId(guid, type), spawn);
        break;
    case ::Guid::Type::Pet:
        std::snprintf(text, sizeof text, ",Pet-0-0-%d-0-%u-%010X", map,
                      TemplateId(guid, type), spawn);
        break;
    case ::Guid::Type::GameObject:
        std::snprintf(text, sizeof text, ",GameObject-0-0-%d-0-%u-%010X", map,
                      TemplateId(guid, type), spawn);
        break;
    default:
        std::snprintf(text, sizeof text, ",%016llX", static_cast<unsigned long long>(guid));
        break;
    }
    line += text;
}

using RealmName_t = const char *(*)();

// A player's name with "-<realm>", as retail writes it (without the region).
void UnitName(std::string &line, uint64_t guid, bool named, const std::string &name) {
    if (!named) {
        Plain(line, "nil");
        return;
    }
    if (!::Guid::IsPlayer(guid)) {
        Quoted(line, name.c_str());
        return;
    }
    std::string full = name;
    const char *realm =
        reinterpret_cast<RealmName_t>(static_cast<uintptr_t>(Offsets::FUN_GET_REALM_NAME))();
    if (realm != nullptr && *realm != '\0') {
        full += '-';
        full += realm;
    }
    Quoted(line, full.c_str());
}

void UnitPart(std::string &line, uint64_t guid, bool named, const std::string &name,
              uint32_t flags, uint32_t raidFlags) {
    GuidPart(line, guid);
    UnitName(line, guid, named, name);
    AppendHex(line, flags);
    AppendHex(line, raidFlags);
}

bool SingleUnitTarget(int target) {
    for (const int t : kSingleUnitTargets) {
        if (t == target)
            return true;
    }
    return false;
}

// Era's ST / AOE tag on spell damage and misses.
const char *DamageTag(const uint8_t *spell) {
    if (spell == nullptr)
        return "ST";
    for (int i = 0; i < kSpellEffects; ++i) {
        const int offset = i * 4;
        if (Game::Read<int>(spell, Offsets::OFF_SPELL_RECORD_EFFECT + offset) == 0 ||
            Game::Read<int>(spell, Offsets::OFF_SPELL_RECORD_EFFECT_RADIUS_INDEX + offset) == 0)
            continue;
        if (!SingleUnitTarget(
                Game::Read<int>(spell, Offsets::OFF_SPELL_RECORD_EFFECT_IMPLICIT_TARGET_A + offset)))
            return "AOE";
    }
    return "ST";
}

bool Tagged(int event) {
    switch (event) {
    case SPELL_DAMAGE:
    case SPELL_PERIODIC_DAMAGE:
    case RANGE_DAMAGE:
    case DAMAGE_SHIELD:
    case DAMAGE_SPLIT:
    case SPELL_MISSED:
    case SPELL_PERIODIC_MISSED:
    case DAMAGE_SHIELD_MISSED:
        return true;
    default:
        return false;
    }
}

// UNIT_DIED and its kin end with unconsciousOnDeath, always 0 in 1.12.
bool Death(int event) {
    return event == UNIT_DIED || event == UNIT_DESTROYED || event == PARTY_KILL ||
           event == SPELL_INSTAKILL;
}

int PowerType(int type) { return type == kHappiness ? kHappinessModern : type; }

// The stored maximum of a power on the unit, 0 when it is out of view.
int MaxPower(uint64_t guid, int type) {
    if (type < 0 || type >= kPowerTypes)
        return 0;
    const uint8_t *fields = Object::Fields(Object::ByGuid(Offsets::TYPEMASK_UNIT, guid));
    return fields != nullptr ? static_cast<int>(Unit::Power::StoredMax(fields, type)) : 0;
}

const char *EnvironmentalTitle(int type) {
    return (type >= 0 && type < 6) ? kEnvironmentalNames[type] : kEnvironmentalNames[0];
}

// amount, baseAmount, overkill, school, resisted, blocked, absorbed,
// critical, glancing, crushing. Era writes overkill -1 when the hit does not
// kill (no 0 in 108,000 damage lines) and 0 on every environmental line.
void Damage(std::string &line, const Entry &e) {
    Number(line, e.v[1]);
    Number(line, e.v[1]);
    Number(line, e.v[2] > 0 ? e.v[2] : (e.event == ENVIRONMENTAL_DAMAGE ? 0 : -1));
    Number(line, e.v[3]);
    Number(line, e.v[5]);
    Number(line, e.v[6]);
    Number(line, e.v[4]);
    Flag(line, e.critical);
    Flag(line, e.glancing);
    Flag(line, e.crushing);
}

// The subevents with an advanced block, and which unit it describes.
enum class Info { None, Source, Dest };

Info InfoUnit(int event) {
    switch (event) {
    case SPELL_CAST_SUCCESS:
    case SWING_DAMAGE:
        return Info::Source;
    case SPELL_DAMAGE:
    case SPELL_PERIODIC_DAMAGE:
    case RANGE_DAMAGE:
    case DAMAGE_SHIELD:
    case ENVIRONMENTAL_DAMAGE:
    case SPELL_HEAL:
    case SPELL_PERIODIC_HEAL:
    case SPELL_ENERGIZE:
    case SPELL_PERIODIC_ENERGIZE:
    case SPELL_DRAIN:
    case SPELL_PERIODIC_DRAIN:
    // Era logs no leech; retail's leech carries the drain's block.
    case SPELL_LEECH:
    case SPELL_PERIODIC_LEECH:
        return Info::Dest;
    default:
        return Info::None;
    }
}

void Fixed(std::string &line, float value, int decimals) {
    char text[32];
    std::snprintf(text, sizeof text, ",%.*f", decimals, static_cast<double>(value));
    line += text;
}

// The server's percent for a player outside the group (tortoise-wow
// Object::BuildValuesUpdate): floor(health * 100 / max), at most 100, at
// least 1 while alive.
int HealthPercent(uint32_t health, uint32_t max) {
    if (health == 0 || max == 0)
        return 0;
    const auto pct = static_cast<uint32_t>(static_cast<float>(health) * 100.0f / max);
    return pct > 100 ? 100 : (pct == 0 ? 1 : static_cast<int>(pct));
}

// The average item level Era writes for a player (see the file's comment), or
// 0 while an item's record is missing, after requesting it.
int ItemLevel(void *player) {
    uint32_t sum = 0;
    bool complete = true;
    const auto level = [&](int invSlot) -> uint32_t {
        const uint32_t itemId = Item::ID::Visible(player, invSlot - 1);
        if (itemId == 0)
            return 0;
        const uint8_t *record = Item::Data::RecordOrRequest(itemId);
        if (record == nullptr) {
            complete = false;
            return 0;
        }
        return Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_ITEM_LEVEL);
    };
    for (int slot = Offsets::EQUIPMENT_SLOT_FIRST; slot <= Offsets::EQUIPMENT_SLOT_LAST; ++slot) {
        if (slot == Offsets::INVSLOT_BODY || slot == Offsets::INVSLOT_TABARD ||
            slot == Offsets::INVSLOT_RANGED || slot == Offsets::INVSLOT_OFFHAND)
            continue;
        sum += level(slot);
    }
    const bool offHand = Item::ID::Visible(player, Offsets::INVSLOT_OFFHAND - 1) != 0;
    sum += offHand ? level(Offsets::INVSLOT_OFFHAND) : level(Offsets::INVSLOT_MAINHAND);
    return complete ? static_cast<int>(sum / 16) : 0;
}

// Era's advanced block for the unit `guid`.
void AdvancedBlock(std::string &line, uint64_t guid) {
    void *unit = guid != 0 ? Object::ByGuid(Offsets::TYPEMASK_UNIT, guid) : nullptr;
    const uint8_t *fields = Object::Fields(unit);
    float pos[3];
    if (fields == nullptr || !Unit::Position::Read(unit, pos)) {
        line += ",0000000000000000,0000000000000000,0,0,0,0,0,0,0,-1,0,0,0,0.00,0.00,0,0.0000,0";
        return;
    }
    const bool player = ::Guid::IsPlayer(guid);
    GuidPart(line, guid);
    GuidPart(line, Unit::Identity::OwnerGuid(unit));
    const auto health = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_HEALTH);
    const auto maxHealth = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_MAXHEALTH);
    if (player) {
        Number(line, HealthPercent(health, maxHealth));
        Number(line, 100);
    } else {
        Number(line, static_cast<int>(health));
        Number(line, static_cast<int>(maxHealth));
    }
    line += ",0,0,0,0,0"; // attack power, spell power, armor, absorb, and the slot after it
    const int powerType = Game::Read<uint8_t>(fields, Offsets::OFF_UNIT_DESCRIPTOR_POWER_TYPE_BYTE);
    if (player && powerType < kPowerTypes) {
        Number(line, PowerType(powerType));
        Number(line, static_cast<int>(Unit::Power::Stored(fields, powerType)));
        Number(line, static_cast<int>(Unit::Power::StoredMax(fields, powerType)));
        Number(line, 0);
    } else {
        line += ",-1,0,0,0";
    }
    Fixed(line, pos[0], 2);
    Fixed(line, pos[1], 2);
    Number(line, 0); // uiMapID
    Fixed(line, Unit::Position::Facing(unit), 4);
    Number(line, player ? ItemLevel(unit) : Game::Read<int>(fields, Offsets::OFF_UNIT_FIELD_LEVEL));
}

// missType, isOffHand, then the amount for a resist or block, and the amount
// and baseAmount for an absorb.
void Miss(std::string &line, const Entry &e) {
    Plain(line, MissName(e.v[0]));
    Flag(line, e.offHand);
    if (e.info & INFO_MISS_RESISTED)
        Number(line, e.v[5]);
    if (e.info & INFO_MISS_BLOCKED)
        Number(line, e.v[6]);
    if (e.info & INFO_MISS_ABSORBED) {
        Number(line, e.v[4]);
        Number(line, e.v[4]);
    }
}

// The entry's line. `landed` makes the SWING_DAMAGE_LANDED twin of a
// SWING_DAMAGE, whose advanced block describes the dest.
std::string Pack(const Entry &e, bool advanced, bool landed) {
    std::string line = landed ? "SWING_DAMAGE_LANDED" : Name(e.event);
    UnitPart(line, e.source, e.sourceNamed, e.sourceName, e.sourceFlags, e.sourceRaidFlags);
    UnitPart(line, e.dest, e.destNamed, e.destName, e.destFlags, e.destRaidFlags);
    const uint8_t *spell = nullptr;
    if (e.spellPrefix) {
        spell = SpellRecord(e.spellId);
        Number(line, e.spellId);
        Quoted(line, Spell::Lookup::Name(spell));
        AppendHex(line, spell != nullptr ? SpellSchoolMask(spell) : e.spellSchool);
    }
    const Info info = landed ? Info::Dest : InfoUnit(e.event);
    if (advanced && info != Info::None)
        AdvancedBlock(line, info == Info::Source ? e.source : e.dest);
    if (e.info & INFO_COUNT)
        Number(line, e.v[0]);
    if (e.info & INFO_TEXT)
        Quoted(line, e.text.c_str());
    if (e.info & INFO_MISS)
        Miss(line, e);
    if (e.info & INFO_ENVIRONMENTAL)
        Plain(line, EnvironmentalTitle(e.v[0]));
    if (e.info & INFO_DAMAGE)
        Damage(line, e);
    if (e.info & INFO_HEAL) {
        // amount, baseAmount, overhealing, absorbed, critical
        Number(line, e.v[0]);
        Number(line, e.v[0]);
        Number(line, e.v[1]);
        Number(line, e.v[2]);
        Flag(line, e.critical);
    }
    if (e.info & INFO_ENERGIZE) {
        // amount, overEnergize, powerType, maxPower
        Decimal(line, e.v[0]);
        Decimal(line, 0);
        Number(line, PowerType(e.v[1]));
        Number(line, MaxPower(e.dest, e.v[1]));
    }
    if (e.info & INFO_DRAIN) {
        // amount, powerType, extraAmount, maxPower
        Number(line, e.v[0]);
        Number(line, PowerType(e.v[1]));
        Number(line, e.v[2]);
        Number(line, MaxPower(e.dest, e.v[1]));
    }
    if (e.info & INFO_EXTRA_SPELL) {
        const uint8_t *extra = SpellRecord(e.v[0]);
        Number(line, e.v[0]);
        Quoted(line, Spell::Lookup::Name(extra));
        Number(line, static_cast<int>(SpellSchoolMask(extra)));
    }
    if (e.info & INFO_ITEM) {
        Number(line, e.v[0]);
        Quoted(line, e.itemNamed ? e.itemName.c_str() : nullptr);
    }
    if (e.info & INFO_AURA_TYPE)
        Plain(line, e.harmful ? "DEBUFF" : "BUFF");
    if (e.info & INFO_AURA_AMOUNT)
        Number(line, e.v[2]);
    if (Tagged(e.event))
        Plain(line, DamageTag(spell));
    if (Death(e.event))
        Number(line, 0);
    return line;
}

} // namespace

namespace {

// The file is open for a line, with a header that says whether advanced
// logging is on. False while combat logging is off.
bool Ready() {
    // A line in the frame logging turned on comes before the frame's check.
    if (!Logging() || (!g_log.IsOpen() && !Open()))
        return false;
    // The setting changed since the last header: say so before the line.
    if (Advanced() != g_headerAdvanced)
        Header();
    return true;
}

} // namespace

void Write(const Entry &entry) {
    if (!Ready())
        return;
    const bool advanced = g_headerAdvanced;
    Emit(Pack(entry, advanced, false));
    if (advanced && entry.event == SWING_DAMAGE)
        Emit(Pack(entry, advanced, true));
}

void WriteLine(const std::string &body) {
    if (Ready())
        Emit(body);
}

void AppendGuid(std::string &line, uint64_t guid) { GuidPart(line, guid); }

uint32_t Session() { return Logging() && g_log.IsOpen() ? g_session : 0; }

const std::string &FileTime() { return g_fileTime; }

bool Advanced() { return CVar::Factory::GetInt(g_advancedCvar) != 0; }

void AppendQuoted(std::string &line, const char *text) {
    if (text == nullptr) {
        line += ",nil";
        return;
    }
    line += ",\"";
    for (const char *c = text; *c != '\0'; ++c) {
        if (*c == '"' || *c == '\\')
            line += '\\';
        line += *c;
    }
    line += '"';
}

void AppendHex(std::string &line, uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof text, ",0x%x", value);
    line += text;
}

void AppendUnsigned(std::string &line, uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof text, ",%u", value);
    line += text;
}

void AppendFixed(std::string &line, float value, int decimals) {
    char text[32];
    std::snprintf(text, sizeof text, ",%.*f", decimals, static_cast<double>(value));
    line += text;
}

} // namespace CombatLog::File
