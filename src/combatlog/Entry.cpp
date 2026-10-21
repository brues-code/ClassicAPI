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

// The combat log entry and its events — 4.3.4's UnitCombatLog_C.cpp:
// GetCombatLogEntry, GetCombatLogObjectFlags / RaidFlags, CombatLogEntry::
// PushEvent and SendCombatLogEvents, plus the modern
// `CombatLogGetCurrentEventInfo()`, which returns the same values while the
// events are being sent and nothing otherwise.
//
// The engine pieces:
//   - names are the 1.12 combat log's own (FUN_COMBAT_LOG_NAME): the object,
//     else the name or creature cache, else UNKNOWNOBJECT — what 4.3.4's
//     GetObjectName resolves, without its wait for a name still in flight;
//   - flags read the same fields 4.3.4 reads (charmer / creator / summoner,
//     a game object's creator, the unit's reaction to the player, the party
//     and raid rosters, the target, the focus, the raid marks). 1.12 has no
//     main tank / main assist roles, so those two bits are never set;
//   - the events are fired with Lua values (Event::Custom::FireValues), as
//     4.3.4's FrameScript_SignalEvent(id, L, count) does: booleans and nils
//     land where 4.3.4 puts them.
//
// The timestamp is 4.3.4's: the Unix time the log started plus the
// milliseconds since, in seconds.

#include "combatlog/Entry.h"

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "guid/Guid.h"
#include "object/Resolve.h"
#include "spell/Lookup.h"
#include "time/Clock.h"
#include "unit/Focus.h"
#include "unit/Identity.h"

#include <ctime>

namespace CombatLog {

namespace {

constexpr const char *kEventNames[EVENT_COUNT] = {
    "ENVIRONMENTAL_DAMAGE",
    "SWING_DAMAGE",
    "SWING_MISSED",
    "RANGE_DAMAGE",
    "RANGE_MISSED",
    "SPELL_CAST_START",
    "SPELL_CAST_SUCCESS",
    "SPELL_CAST_FAILED",
    "SPELL_MISSED",
    "SPELL_DAMAGE",
    "SPELL_HEAL",
    "SPELL_ENERGIZE",
    "SPELL_DRAIN",
    "SPELL_LEECH",
    "SPELL_INSTAKILL",
    "SPELL_SUMMON",
    "SPELL_CREATE",
    "SPELL_INTERRUPT",
    "SPELL_EXTRA_ATTACKS",
    "SPELL_DURABILITY_DAMAGE",
    "SPELL_DURABILITY_DAMAGE_ALL",
    "SPELL_AURA_APPLIED",
    "SPELL_AURA_APPLIED_DOSE",
    "SPELL_AURA_REMOVED_DOSE",
    "SPELL_AURA_REMOVED",
    "SPELL_AURA_REFRESH",
    "SPELL_DISPEL",
    "SPELL_STOLEN",
    "SPELL_AURA_BROKEN",
    "SPELL_AURA_BROKEN_SPELL",
    "DAMAGE_AURA_BROKEN",
    "ENCHANT_APPLIED",
    "ENCHANT_REMOVED",
    "SPELL_PERIODIC_MISSED",
    "SPELL_PERIODIC_DAMAGE",
    "SPELL_PERIODIC_HEAL",
    "SPELL_PERIODIC_ENERGIZE",
    "SPELL_PERIODIC_DRAIN",
    "SPELL_PERIODIC_LEECH",
    "SPELL_DISPEL_FAILED",
    "DAMAGE_SHIELD",
    "DAMAGE_SHIELD_MISSED",
    "DAMAGE_SPLIT",
    "PARTY_KILL",
    "UNIT_DIED",
    "UNIT_DESTROYED",
    "SPELL_RESURRECT",
    "SPELL_BUILDING_DAMAGE",
    "SPELL_BUILDING_HEAL",
    "UNIT_DISSIPATES",
};

constexpr const char *kMissNames[MISS_COUNT] = {
    "NONE", "MISS", "RESIST", "DODGE", "PARRY", "BLOCK",
    "EVADE", "IMMUNE", "IMMUNE", "DEFLECT", "ABSORB", "REFLECT",
};

constexpr const char *kEnvironmentalNames[] = {"FATIGUE", "DROWNING", "FALLING",
                                               "LAVA",    "SLIME",    "FIRE"};

// Spell.dbc Attributes bits 0x80 and 0x100: the 1.12 combat log's own
// formatters drop these spells too (benilla-re objectlayer/combatlogmessages).
constexpr uint32_t kHiddenAttributes = 0x180;
constexpr uint32_t kRangedAttributeEx3 = 0x8000;
constexpr int kSchoolCount = 7;

// COMBATLOG_OBJECT_* — 4.3.4's GetCombatLogObjectFlags.
constexpr uint32_t AFFILIATION_MINE = 0x1;
constexpr uint32_t AFFILIATION_PARTY = 0x2;
constexpr uint32_t AFFILIATION_RAID = 0x4;
constexpr uint32_t AFFILIATION_OUTSIDER = 0x8;
constexpr uint32_t AFFILIATION_MASK = 0xF;
constexpr uint32_t REACTION_FRIENDLY = 0x10;
constexpr uint32_t REACTION_NEUTRAL = 0x20;
constexpr uint32_t REACTION_HOSTILE = 0x40;
constexpr uint32_t REACTION_MASK = 0xF0;
constexpr uint32_t CONTROL_PLAYER = 0x100;
constexpr uint32_t CONTROL_NPC = 0x200;
constexpr uint32_t TYPE_PLAYER = 0x400;
constexpr uint32_t TYPE_NPC = 0x800;
constexpr uint32_t TYPE_PET = 0x1000;
constexpr uint32_t TYPE_GUARDIAN = 0x2000;
constexpr uint32_t TYPE_OBJECT = 0x4000;
constexpr uint32_t TARGET = 0x10000;
constexpr uint32_t FOCUS = 0x20000;
constexpr uint32_t NONE = 0x80000000;

uint32_t g_startTime = 0;
uint32_t g_startTimeMs = 0;
bool g_started = false;

// The entry being sent: CombatLogGetCurrentEventInfo answers only while
// COMBAT_LOG_EVENT / COMBAT_LOG_EVENT_UNFILTERED run (Classic Era returns
// nothing outside them).
const Entry *g_sending = nullptr;

uint32_t High(uint64_t guid) { return static_cast<uint32_t>(guid >> 32); }

// 4.3.4's player test on a GUID: high nibble 0 and not the empty GUID.
bool IsPlayerGuid(uint64_t guid) {
    const uint32_t high = High(guid);
    return (high & 0xF0000000) == 0 &&
           ((high & 0xF07FFFFF) != 0 || static_cast<uint32_t>(guid) != 0);
}

const uint8_t *Fields(const void *object) {
    return object == nullptr ? nullptr
                             : Game::Read<const uint8_t *>(object, Offsets::OFF_UNIT_DESCRIPTOR);
}

uint64_t ReadGuidField(const void *object, int offset) {
    const uint8_t *fields = Fields(object);
    return fields == nullptr ? 0 : Game::Read<uint64_t>(fields, offset);
}

bool InParty(uint64_t guid) {
    for (int i = 0; i < Offsets::PARTY_MAX_SLOTS; ++i) {
        const uint64_t member =
            Game::Read<uint64_t>(Offsets::VAR_PARTY_GUIDS + i * sizeof(uint64_t));
        if (member != 0 && member == guid)
            return true;
    }
    return false;
}

bool HasParty() {
    for (int i = 0; i < Offsets::PARTY_MAX_SLOTS; ++i) {
        if (Game::Read<uint64_t>(Offsets::VAR_PARTY_GUIDS + i * sizeof(uint64_t)) != 0)
            return true;
    }
    return false;
}

bool InRaid(uint64_t guid) {
    for (int i = 0; i < Offsets::RAID_MAX_SLOTS; ++i) {
        const auto *member =
            Game::Read<const uint8_t *>(Offsets::VAR_RAID_MEMBER_PTRS + i * sizeof(void *));
        if (member != nullptr && Game::Read<uint64_t>(member, 0) == guid)
            return true;
    }
    return false;
}

int Reaction(void *unit, void *other) {
    using Reaction_t = int(__thiscall *)(void *unit, void *other);
    return reinterpret_cast<Reaction_t>(static_cast<uintptr_t>(Offsets::FUN_UNIT_REACTION))(
        unit, other);
}

uint32_t ObjectFlags(uint64_t guid) {
    if (guid == 0)
        return NONE;
    const bool inRaid = Game::Read<int>(Offsets::VAR_RAID_MEMBER_COUNT) != 0;
    const bool inGroup = inRaid || HasParty();
    const uint64_t player = Unit::Identity::PlayerGuid();

    uint64_t owner = guid;
    void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid);
    if (unit != nullptr) {
        const uint64_t controller = Unit::Identity::OwnerGuid(unit);
        if (controller != 0)
            owner = controller;
    } else if (void *object = Object::ByGuid(Offsets::TYPEMASK_GAMEOBJECT, guid)) {
        owner = ReadGuidField(object, Offsets::OFF_GAMEOBJECT_FIELD_CREATED_BY);
    }
    // A minion's minion answers to its master's controller (a totem's totem).
    if (owner != guid && !IsPlayerGuid(owner)) {
        void *ownerUnit = Object::ByGuid(Offsets::TYPEMASK_UNIT, owner);
        const uint64_t controller = Unit::Identity::OwnerGuid(ownerUnit);
        if (controller != 0)
            owner = controller;
    }

    // 4.3.4's tests read `(high & 0xF0F00000) == 0xF1300000` and the like,
    // which no GUID can pass; the types are the GUID prefixes (Guid::Classify).
    const bool isPlayer = IsPlayerGuid(guid);
    const bool isPet = Guid::IsPet(guid);
    uint32_t flags;
    if (isPlayer || isPet || Guid::IsCreature(guid)) {
        if (owner == guid) {
            flags = isPlayer ? TYPE_PLAYER : (isPet ? TYPE_PET : TYPE_NPC);
        } else {
            uint64_t master = ReadGuidField(unit, Offsets::OFF_UNIT_FIELD_CHARMEDBY);
            if (master == 0)
                master = ReadGuidField(unit, Offsets::OFF_UNIT_FIELD_SUMMONEDBY);
            flags = master != 0 ? TYPE_PET : TYPE_GUARDIAN;
        }
    } else {
        flags = TYPE_OBJECT;
    }
    flags |= IsPlayerGuid(owner) ? CONTROL_PLAYER : CONTROL_NPC;

    if (owner == player) {
        flags |= AFFILIATION_MINE | REACTION_FRIENDLY;
    } else {
        bool hostile = false;
        void *playerUnit = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
        if (unit != nullptr && playerUnit != nullptr) {
            const int reaction = Reaction(unit, playerUnit);
            if (reaction >= 4) {
                flags |= REACTION_FRIENDLY;
            } else if (reaction <= 1) {
                flags |= REACTION_HOSTILE;
                hostile = true;
            }
        }
        if (!hostile && inGroup) {
            if (InParty(owner))
                flags |= AFFILIATION_PARTY | REACTION_FRIENDLY;
            else if (inRaid && InRaid(owner))
                flags |= AFFILIATION_RAID | REACTION_FRIENDLY;
        }
        if ((flags & AFFILIATION_MASK) == 0)
            flags |= AFFILIATION_OUTSIDER;
        if ((flags & REACTION_MASK) == 0)
            flags |= REACTION_NEUTRAL;
    }

    const uint64_t target = Game::Read<uint64_t>(Offsets::VAR_CURRENT_SELECTION_GUID_LO);
    if (target != 0 && guid == target)
        flags |= TARGET;
    const uint64_t focus = Unit::Focus::Get();
    if (focus != 0 && guid == focus)
        flags |= FOCUS;
    return flags;
}

// COMBATLOG_OBJECT_RAIDTARGET1..8: the mark the object wears, in a group.
uint32_t RaidFlags(uint64_t guid) {
    if (guid == 0)
        return NONE;
    if (Game::Read<int>(Offsets::VAR_RAID_MEMBER_COUNT) == 0 && !HasParty())
        return 0;
    for (int i = 0; i < 8; ++i) {
        if (Game::Read<uint64_t>(Offsets::VAR_RAID_TARGET_GUIDS + i * sizeof(uint64_t)) == guid)
            return 1u << i;
    }
    return 0;
}

using GetText_t = const char *(__fastcall *)(const char *name, int edx, int gender);
using CacheRecord_t = const uint8_t *(__thiscall *)(void *cache, uint32_t id, const uint64_t *guid,
                                                    void *callback, void *userData, char dedup);
using NameCache_t = const uint8_t *(__thiscall *)(void *cache, uint32_t lo, uint32_t hi,
                                                  const uint64_t *guid, void *callback,
                                                  void *userData, char dedup);
using ObjectName_t = const char *(__thiscall *)(void *object, int *outFlags);
using GameObjectName_t = const char *(__fastcall *)(void *object);

void *QueryDone() { return reinterpret_cast<void *>(Offsets::FUN_COMBAT_LOG_NAME_QUERY_DONE); }

// The combat log namer's steps (0x006264E0): the object's own name (a unit,
// a game object, an item's record), else a player's from the name cache or a
// creature's from the creature cache, each asked so a miss sends the query.
// The creature's entry is the GUID's (Guid::CreatureEntry): the namer takes
// the high dword's low word, which is not the entry on this server's GUIDs.
const char *ObjectName(uint64_t guid) {
    if (void *object = Object::ByGuid(Offsets::TYPEMASK_OBJECT, guid)) {
        const auto *fields = Game::Read<const uint8_t *>(object, Offsets::OFF_UNIT_GUID_PTR);
        const uint32_t type =
            fields != nullptr ? Game::Read<uint32_t>(fields, Offsets::OFF_OBJECT_FIELD_TYPE) : 0;
        if (type & Offsets::TYPEMASK_UNIT)
            return reinterpret_cast<ObjectName_t>(Offsets::FUN_OBJECT_GET_NAME)(object, nullptr);
        if (type & Offsets::TYPEMASK_GAMEOBJECT)
            return reinterpret_cast<GameObjectName_t>(Offsets::FUN_GAMEOBJECT_GET_NAME)(object);
        if (type & Offsets::TYPEMASK_ITEM) {
            const uint64_t none = 0;
            const uint8_t *rec = reinterpret_cast<CacheRecord_t>(
                Offsets::FUN_DBCACHE_ITEMSTATS_GET_RECORD)(
                reinterpret_cast<void *>(Offsets::VAR_ITEMDB_CACHE),
                Game::Read<uint32_t>(fields, Offsets::OFF_OBJECT_FIELD_ENTRY), &none, nullptr,
                nullptr, 0);
            return rec != nullptr ? Game::Read<const char *>(rec, Offsets::OFF_ITEMSTATS_NAME)
                                  : nullptr;
        }
        return nullptr;
    }
    if (Guid::IsPlayer(guid)) {
        const uint8_t *rec = reinterpret_cast<NameCache_t>(Offsets::FUN_PLAYER_INFO_LOOKUP)(
            reinterpret_cast<void *>(Offsets::VAR_PLAYER_NAME_CACHE), static_cast<uint32_t>(guid),
            static_cast<uint32_t>(guid >> 32), &guid, QueryDone(), nullptr, 0);
        return rec != nullptr
                   ? reinterpret_cast<const char *>(rec + Offsets::OFF_PLAYER_INFO_NAME)
                   : nullptr;
    }
    const uint32_t entry = Guid::CreatureEntry(guid);
    if (entry != 0) {
        const uint8_t *rec = reinterpret_cast<CacheRecord_t>(Offsets::FUN_CREATURE_GET_RECORD)(
            reinterpret_cast<void *>(Offsets::VAR_CREATURE_CACHE), entry, &guid, QueryDone(),
            nullptr, 0);
        return rec != nullptr ? Game::Read<const char *>(rec, 0) : nullptr;
    }
    return nullptr;
}

void SetName(uint64_t guid, bool &named, std::string &name) {
    const char *n = ObjectName(guid);
    if (n == nullptr || *n == '\0') {
        n = reinterpret_cast<GetText_t>(Offsets::FUN_FRAMESCRIPT_GET_TEXT)("UNKNOWNOBJECT", -1, 0);
        if (n == nullptr || *n == '\0')
            n = "UKNOWNOBJECT"; // the namer's own last resort, spelling and all
    }
    named = true;
    name = n;
}

const char *SpellName(const uint8_t *spell) {
    if (spell == nullptr)
        return nullptr;
    const int locale = Game::Read<int>(Offsets::VAR_LOCALE_INDEX);
    const char *n = Game::Read<const char *>(spell, Offsets::OFF_SPELL_NAMES + locale * 4);
    return (n == nullptr || *n == '\0') ? nullptr : n;
}

void PushNumberOrNil(void *L, int value) {
    if (value != 0)
        Game::Lua::PushNumber(L, value);
    else
        Game::Lua::PushNil(L);
}

void PushFlag(void *L, bool value) {
    if (value)
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
}

void PushName(void *L, bool named, const std::string &name) {
    if (named)
        Game::Lua::PushString(L, name.c_str());
    else
        Game::Lua::PushNil(L);
}

// CombatLogEntry::PushEvent, the blocks 1.12 fills.
int Push(void *L, const Entry &e) {
    using namespace Game::Lua;
    char guid[Guid::STRING_SIZE];
    const double timestamp =
        static_cast<double>(e.timeMs - g_startTimeMs) * 0.001 + static_cast<double>(g_startTime);
    PushNumber(L, timestamp);
    PushString(L, Name(e.event));
    PushBool(L, e.hideCaster);
    PushString(L, Guid::FormatAsString(e.source, guid, sizeof guid));
    PushName(L, e.sourceNamed, e.sourceName);
    PushNumber(L, e.sourceFlags);
    PushNumber(L, e.sourceRaidFlags);
    PushString(L, Guid::FormatAsString(e.dest, guid, sizeof guid));
    PushName(L, e.destNamed, e.destName);
    PushNumber(L, e.destFlags);
    PushNumber(L, e.destRaidFlags);
    int count = 11;

    if (e.spellPrefix) {
        const uint8_t *spell = SpellRecord(e.spellId);
        PushNumber(L, e.spellId);
        const char *name = SpellName(spell);
        if (name != nullptr)
            PushString(L, name);
        else
            PushNil(L);
        PushNumber(L, spell != nullptr
                          ? SchoolMask(Game::Read<uint32_t>(spell, Offsets::OFF_SPELL_RECORD_SCHOOL))
                          : e.spellSchool);
        count += 3;
    }
    if (e.info & INFO_MISS) {
        const int miss = e.v[0];
        PushString(L, (miss >= 0 && miss < MISS_COUNT) ? kMissNames[miss] : kMissNames[0]);
        PushFlag(L, e.offHand);
        count += 2;
    }
    if (e.info & INFO_ENVIRONMENTAL) {
        const int type = e.v[0];
        PushString(L, (type >= 0 && type < 6) ? kEnvironmentalNames[type] : kEnvironmentalNames[0]);
        ++count;
    }
    if (e.info & INFO_DAMAGE) {
        PushNumber(L, e.v[1]);
        PushNumber(L, e.v[2]);
        PushNumber(L, e.v[3]);
        PushNumberOrNil(L, e.v[5]);
        PushNumberOrNil(L, e.v[6]);
        PushNumberOrNil(L, e.v[4]);
        PushFlag(L, e.critical);
        PushFlag(L, e.glancing);
        PushFlag(L, e.crushing);
        PushFlag(L, e.offHand);
        count += 10;
    }
    if (e.info & INFO_MISS_ABSORBED) {
        PushNumber(L, e.v[4]);
        ++count;
    }
    if (e.info & INFO_MISS_RESISTED) {
        PushNumber(L, e.v[5]);
        ++count;
    }
    if (e.info & INFO_MISS_BLOCKED) {
        PushNumber(L, e.v[6]);
        ++count;
    }
    if (e.info & INFO_HEAL) {
        PushNumber(L, e.v[0]);
        PushNumber(L, e.v[1]);
        PushNumber(L, e.v[2]);
        PushFlag(L, e.critical);
        count += 4;
    }
    if (e.info & INFO_ENERGIZE) {
        PushNumber(L, e.v[0]);
        PushNumber(L, e.v[1]);
        if (e.v[2] < 0)
            PushNil(L);
        else
            PushNumber(L, e.v[2]);
        count += 3;
    }
    if (e.info & INFO_DRAIN) {
        PushNumber(L, e.v[0]);
        PushNumber(L, e.v[1]);
        PushNumberOrNil(L, e.v[2]);
        if (e.v[3] < 0)
            PushNil(L);
        else
            PushNumber(L, e.v[3]);
        count += 4;
    }
    return count;
}

const Game::Doc::Field kEventPayload[] = {
    Game::Doc::Req("timestamp", "number", "The Unix time of the event, in seconds."),
    Game::Doc::Req("subevent", "string", "What happened, for example SPELL_DAMAGE."),
    Game::Doc::Req("hideCaster", "bool", "True when the event has no source."),
    Game::Doc::Req("sourceGUID", "WOWGUID"),
    Game::Doc::Opt("sourceName", "string"),
    Game::Doc::Req("sourceFlags", "number", "COMBATLOG_OBJECT_* bits of the source."),
    Game::Doc::Req("sourceRaidFlags", "number", "COMBATLOG_OBJECT_RAIDTARGET* bit of the source."),
    Game::Doc::Req("destGUID", "WOWGUID"),
    Game::Doc::Opt("destName", "string"),
    Game::Doc::Req("destFlags", "number", "COMBATLOG_OBJECT_* bits of the target."),
    Game::Doc::Req("destRaidFlags", "number", "COMBATLOG_OBJECT_RAIDTARGET* bit of the target."),
    Game::Doc::Vararg("any", "The subevent's own values."),
};
const Game::Doc::Event kFilteredDoc{
    "CombatLog", "Fires for each combat log event that matches the combat log filter.",
    kEventPayload};
const Game::Doc::Event kUnfilteredDoc{"CombatLog", "Fires for each combat log event.",
                                      kEventPayload};
const Event::Custom::AutoReserve _filtered{"COMBAT_LOG_EVENT", &kFilteredDoc};
const Event::Custom::AutoReserve _unfiltered{"COMBAT_LOG_EVENT_UNFILTERED", &kUnfilteredDoc};

// The most values Push makes: 11 + 3 + 10 + 3.
constexpr int kMaxValues = 32;

void Send(const Entry &e) {
    const int filtered = _filtered.Slot();
    const int unfiltered = _unfiltered.Slot();
    const bool toFiltered = Event::Custom::HasListeners(filtered);
    const bool toUnfiltered = Event::Custom::HasListeners(unfiltered);
    if (!toFiltered && !toUnfiltered)
        return;
    void *L = Game::Lua::State();
    if (L == nullptr || Game::Lua::CheckStack(L, kMaxValues) == 0)
        return;
    const int top = Game::Lua::GetTop(L);
    const int count = Push(L, e);
    const Entry *outer = g_sending; // a handler's own action can log again
    g_sending = &e;
    // With no filter set every entry matches, so COMBAT_LOG_EVENT gets all.
    if (toFiltered)
        Event::Custom::FireValues(filtered, L, top + 1, count);
    if (toUnfiltered)
        Event::Custom::FireValues(unfiltered, L, top + 1, count);
    g_sending = outer;
    Game::Lua::SetTop(L, top);
}

int __fastcall Script_CombatLogGetCurrentEventInfo(void *L) {
    if (g_sending == nullptr || Game::Lua::CheckStack(L, kMaxValues) == 0)
        return 0;
    return Push(L, *g_sending);
}

const Game::Doc::Field kInfoReturns[] = {
    Game::Doc::Vararg("CombatLogEventInfo",
                      "timestamp, subevent, hideCaster, sourceGUID, sourceName, sourceFlags, "
                      "sourceRaidFlags, destGUID, destName, destFlags, destRaidFlags, then the "
                      "subevent's own values."),
};
const Game::Doc::Function kInfoDoc{"Returns the values of the combat log event that is being sent.",
                                   {}, kInfoReturns, "CombatLogGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("CombatLogGetCurrentEventInfo",
                                      &Script_CombatLogGetCurrentEventInfo, &kInfoDoc);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

const char *Name(int event) {
    return (event >= 0 && event < EVENT_COUNT) ? kEventNames[event] : "";
}

const uint8_t *SpellRecord(int spellId) { return Spell::Lookup::RecordForID(spellId); }

bool Hidden(const uint8_t *spell) {
    return spell == nullptr ||
           (Game::Read<uint32_t>(spell, Offsets::OFF_SPELL_RECORD_ATTRIBUTES) & kHiddenAttributes) !=
               0 ||
           SpellName(spell) == nullptr;
}

bool Ranged(const uint8_t *spell) {
    return spell != nullptr &&
           (Game::Read<uint32_t>(spell, Offsets::OFF_SPELL_RECORD_ATTRIBUTES_EX3) &
            kRangedAttributeEx3) != 0;
}

uint32_t SchoolMask(uint32_t school) { return school < kSchoolCount ? 1u << school : 0; }

Entry Begin(uint64_t source, uint64_t dest, int event, const uint8_t *spell) {
    Entry e;
    e.event = event;
    if (source != 0) {
        e.source = source;
        SetName(source, e.sourceNamed, e.sourceName);
        e.sourceFlags = ObjectFlags(source);
        e.sourceRaidFlags = RaidFlags(source);
        e.hideCaster = false;
    }
    if (dest != 0) {
        e.dest = dest;
        SetName(dest, e.destNamed, e.destName);
        e.destFlags = ObjectFlags(dest);
        e.destRaidFlags = RaidFlags(dest);
    }
    if (spell != nullptr) {
        e.spellPrefix = true;
        e.spellId = Game::Read<int>(spell, 0);
    }
    return e;
}

void SetMissInfo(Entry &e, int missType, int amount, bool offHand) {
    e.info |= INFO_MISS;
    e.offHand = offHand;
    e.v[0] = missType;
    switch (missType) {
    case MISS_RESIST:
        e.info |= INFO_MISS_RESISTED;
        e.v[5] = amount;
        break;
    case MISS_BLOCK:
        e.info |= INFO_MISS_BLOCKED;
        e.v[6] = amount;
        break;
    case MISS_ABSORB:
        e.info |= INFO_MISS_ABSORBED;
        e.v[4] = amount;
        break;
    default:
        break;
    }
}

void Queue(Entry &e) {
    e.timeMs = Time::Clock::NowMs();
    if (!g_started) {
        g_started = true;
        g_startTime = static_cast<uint32_t>(std::time(nullptr));
        g_startTimeMs = e.timeMs;
    }
    Send(e);
}

} // namespace CombatLog
