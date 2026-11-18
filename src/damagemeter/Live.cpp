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

// The live damage meter: retail's two cvars, the sessions, and the three
// DAMAGE_METER_* events, fed from the combat log entries (CombatLog::Queue).
// Nothing is collected while damageMeterEnabled is off, and turning it off
// drops everything.
//
// Sessions. Retail starts one each time combat starts; its rule is on the C
// side. Ours:
//   - during an encounter (CombatLog::Encounter: a hostile world boss fights
//     the group) the session is the encounter, named after the boss. An
//     encounter start always opens a new session, also in the middle of trash,
//     and the player's own death does not end it;
//   - otherwise a session runs while the player or a group member in view is
//     in combat (Encounter::GroupFighting). The first hit of a pull can come
//     before the server's combat flag, so a hit between an ally and an enemy
//     (a hostile or neutral unit) also opens one; such a session ends only
//     after the group was seen in combat, as Encounter's wipe test does;
//   - after an encounter ends, no new session opens until the group leaves
//     combat (our choice: the adds left after a kill stay out of a session of
//     their own).
//   - entering a world map that is an instance, other than the last instance
//     entered, drops every session while damageMeterResetOnNewInstance is on.
//
// Units. The meter credits what a pet or a guardian does to its owner, the
// combat log's own controller (CombatLog::Controller, the GUID its flags take
// their affiliation from), and names the minion in the spell's creatureName.
// A minion that is hit, or dies, stays its own unit.
// Players and pets are keyed by GUID and pushed with it; creatures are keyed
// by creature ID and pushed with sourceCreatureID only (our choice: the
// API's own lookup takes a creature ID, so one creature ID is one source).
//
// Events. Retail marks the three UniqueEvent; they are sent at most once a
// frame, at the frame's end. Blizzard_DamageMeter's session window expects a
// data change to send DAMAGE_METER_COMBAT_SESSION_UPDATED twice, "one for the
// overall session (with no sessionID) and one for the current session with
// its sessionID": (type, 0) and (type, sessionID), for each type whose
// storage changed.

#include "damagemeter/Live.h"
#include "damagemeter/Units.h"

#include "Game.h"
#include "Offsets.h"
#include "combatlog/Encounter.h"
#include "cvar/Factory.h"
#include "event/Custom.h"
#include "event/EnteringWorld.h"
#include "guid/Guid.h"
#include "map/Instance.h"
#include "object/Resolve.h"
#include "tick/FrameTick.h"
#include "time/Clock.h"
#include "unit/CreatureID.h"
#include "unit/Identity.h"

#include <cstdlib>
#include <map>
#include <string>

namespace DamageMeter::Live {

namespace {

namespace Flag = CombatLog::ObjectFlag;

constexpr int kOpenWorld = 0;

Meter g_meter;

// --- The clock --------------------------------------------------------------

uint32_t g_lastTick = 0;
int64_t g_clockMs = 0;
bool g_clockStarted = false;

int64_t Clock() {
    const uint32_t now = Time::Clock::NowMs();
    if (!g_clockStarted) {
        g_clockStarted = true;
        g_lastTick = now;
    }
    g_clockMs += Time::Clock::Elapsed(g_lastTick, now);
    g_lastTick = now;
    return g_clockMs;
}

// --- Events -----------------------------------------------------------------

const Game::Doc::Field kUpdatedPayload[] = {
    Game::Doc::Req("type", "DamageMeterType"),
    Game::Doc::Req("sessionID", "number"),
};
const Game::Doc::Event kUpdatedDoc{"DamageMeter",
                                   "Fires when the data of a combat session changes.",
                                   kUpdatedPayload};
const Game::Doc::Event kCurrentDoc{"DamageMeter", "Fires when a new combat session starts.", {}};
const Game::Doc::Event kResetDoc{"DamageMeter", "Fires when all combat sessions are cleared.",
                                 {}};
const Event::Custom::AutoReserve _updated{"DAMAGE_METER_COMBAT_SESSION_UPDATED", &kUpdatedDoc};
const Event::Custom::AutoReserve _current{"DAMAGE_METER_CURRENT_SESSION_UPDATED", &kCurrentDoc};
const Event::Custom::AutoReserve _reset{"DAMAGE_METER_RESET", &kResetDoc};

bool g_pendingReset = false;
bool g_pendingCurrent = false;
// The storages changed since the last send, by session ID.
std::map<uint32_t, uint32_t> g_dirty;

void SendUpdates() {
    uint32_t overall = 0;
    for (const auto &[id, storages] : g_dirty)
        overall |= storages;
    const int slot = _updated.Slot();
    for (int type = 0; type < TYPE_COUNT; ++type) {
        const uint32_t bit = 1u << StorageOf(type);
        if ((overall & bit) == 0)
            continue;
        Event::Custom::Fire(slot, "%d%d", type, 0);
        for (const auto &[id, storages] : g_dirty) {
            if (storages & bit)
                Event::Custom::Fire(slot, "%d%u", type, id);
        }
    }
    g_dirty.clear();
}

void Send() {
    if (g_pendingReset) {
        g_pendingReset = false;
        Event::Custom::Fire(_reset.Slot(), "");
    }
    if (g_pendingCurrent) {
        g_pendingCurrent = false;
        Event::Custom::Fire(_current.Slot(), "");
    }
    if (!g_dirty.empty())
        SendUpdates();
}

// --- Sessions ---------------------------------------------------------------

// The serial (Encounter::Serial) of the encounter the open session is, 0
// for a session that is not one.
uint32_t g_sessionEncounter = 0;
bool g_sawFighting = false;
bool g_waitCombatDrop = false;
int g_lastInstanceMap = -1;

void Open(const std::string &name, uint32_t encounter, bool sawFighting) {
    g_meter.Open(Clock(), name);
    g_sessionEncounter = encounter;
    g_sawFighting = sawFighting;
    g_pendingCurrent = true;
}

void Close() {
    g_meter.Close(Clock());
    g_sessionEncounter = 0;
}

// Drops every session. `announce` false sends no DAMAGE_METER_RESET when
// there was nothing to drop.
void Clear(bool announce) {
    if (announce || !g_meter.Sessions().empty())
        g_pendingReset = true;
    g_meter.Reset();
    g_sessionEncounter = 0;
    g_sawFighting = false;
    g_dirty.clear();
    g_pendingCurrent = false;
}

// The encounter under way has its session: opened by the entry that starts
// it (Encounter::Observe runs first), or at the next frame after a reset.
bool OpenEncounter() {
    if (!CombatLog::Encounter::Active())
        return false;
    const uint32_t serial = CombatLog::Encounter::Serial();
    if (!g_meter.IsOpen() || g_sessionEncounter != serial)
        Open(CombatLog::Encounter::BossName(), serial, true);
    return true;
}

// The encounter's session ends at the end of the frame its encounter ended
// in, not at the entry that ended it: the 1.12 server sends a boss's killing
// blow after its death (Encounter.cpp), and the kill ends the encounter at
// the death. Returns true while an encounter decides the session.
bool EncounterBoundary() {
    if (OpenEncounter())
        return true;
    if (g_sessionEncounter != 0) {
        Close();
        g_waitCombatDrop = true;
        return true;
    }
    return false;
}

void CombatBoundary() {
    const bool fighting = CombatLog::Encounter::GroupFighting();
    if (g_meter.IsOpen()) {
        if (fighting)
            g_sawFighting = true;
        else if (g_sawFighting)
            Close();
        return;
    }
    if (!fighting)
        g_waitCombatDrop = false;
    else if (!g_waitCombatDrop)
        Open(std::string(), 0, true);
}

CVar::Factory::Handle g_enabledCvar = nullptr;
CVar::Factory::Handle g_resetCvar = nullptr;

bool Enabled() { return CVar::Factory::GetInt(g_enabledCvar) != 0; }

void OnFrame() {
    if (Enabled() && !EncounterBoundary())
        CombatBoundary();
    Send();
}

const Tick::FrameTick::AutoSubscribe _frame{&OnFrame};

void OnEnterWorld() {
    const int map = Game::Read<int>(Offsets::VAR_CURRENT_MAP_ID);
    if (Map::Instance::CurrentType() <= kOpenWorld || map == g_lastInstanceMap)
        return;
    g_lastInstanceMap = map;
    if (Enabled() && CVar::Factory::GetInt(g_resetCvar) != 0)
        Clear(true);
}

const Event::EnteringWorld::AutoObserve _enterWorld{&OnEnterWorld};

// A logout (the return to the glue screens, once per glue boot) ends the
// character's meter: the sessions live in this process, which outlives the
// login, so the next character, or account, would see them. Nothing is sent:
// the glue Lua has no DAMAGE_METER_* events, and the next world's meter
// starts empty, as a fresh login does.
void ClearOnLogout() {
    g_meter.Reset();
    g_sessionEncounter = 0;
    g_sawFighting = false;
    g_waitCombatDrop = false;
    g_lastInstanceMap = -1;
    g_pendingReset = false;
    g_pendingCurrent = false;
    g_dirty.clear();
}

const Game::GlueModuleAutoRegister _clearOnLogout{&ClearOnLogout};

// --- Cvars ------------------------------------------------------------------

// Turning the meter off drops everything. The engine also runs this at
// registration, with the value it already has.
int __fastcall OnEnabledChanged(CVar::Factory::Handle, const char *, const char *newValue,
                                void *) {
    if (newValue == nullptr || std::atoi(newValue) == 0)
        Clear(false);
    return 1;
}

// Retail's help, category (4, Game) and default, from its cvar list
// (Ketho/BlizzardInterfaceResources, live, Resources/CVars.lua).
void RegisterCVars() {
    g_enabledCvar = CVar::Factory::Register("damageMeterEnabled",
                                            "If true, show the damage meter UI.",
                                            Offsets::CVAR_CATEGORY_GAME, "0", &OnEnabledChanged);
    g_resetCvar = CVar::Factory::Register(
        "damageMeterResetOnNewInstance",
        "If true, reset the damage meter any time the player enters a new instance.",
        Offsets::CVAR_CATEGORY_GAME, "0");
}

const Game::ModuleAutoRegister _autoreg{&RegisterCVars};

// --- Units ------------------------------------------------------------------

// A player's class token and faction group: from its object in view, else
// from the name cache.
void PlayerDetails(uint64_t guid, MeterUnit &u) {
    uint32_t classId = 0;
    uint32_t race = 0;
    if (const uint8_t *fields = Object::Fields(Object::ByGuid(Offsets::TYPEMASK_UNIT, guid))) {
        classId = Unit::Identity::Class(fields);
        race = Unit::Identity::Race(fields);
    } else if (const uint8_t *rec = Unit::Identity::PlayerInfoRecord(guid)) {
        classId = Game::Read<uint32_t>(rec, Offsets::OFF_PLAYER_INFO_CLASS);
        race = Game::Read<uint32_t>(rec, Offsets::OFF_PLAYER_INFO_RACE);
    }
    SetPlayerDetails(u, classId, race);
}

std::string GuidString(uint64_t guid) {
    char text[Guid::STRING_SIZE];
    return Guid::FormatAsString(guid, text, sizeof text);
}

MeterUnit UnitOf(uint64_t guid, const std::string &name, uint32_t flags) {
    MeterUnit u;
    if (guid == 0)
        return u;
    u.name = name;
    u.ally = (flags & Flag::AFFILIATION_GROUP) != 0;
    u.enemy = (flags & Flag::REACTION_ENEMY) != 0;
    u.isPlayer = Guid::IsPlayer(guid);
    u.isPet = Guid::IsPet(guid) || (flags & (Flag::TYPE_PET | Flag::TYPE_GUARDIAN)) != 0;
    u.isLocalPlayer = guid == Unit::Identity::PlayerGuid();
    if (u.isPlayer)
        PlayerDetails(guid, u);
    if (!u.isPet && Guid::IsCreature(guid))
        u.creatureID = Unit::CreatureID::ForGuid(guid);
    // A pet and a player have no creature record, so rank 0 (FUN_00605620).
    u.classification = Classification(u.creatureID);
    if (u.creatureID != 0) {
        u.key = CreatureKey(u.creatureID);
    } else {
        u.key = guid;
        u.guid = GuidString(guid);
    }
    return u;
}

// The side of an entry as the meter credits it: a minion's to its
// controller, with the minion's name kept.
Actor ActorOf(uint64_t guid, const std::string &name, uint32_t flags) {
    Actor a;
    const uint64_t owner = guid != 0 ? CombatLog::Controller(guid) : 0;
    if (owner == 0 || owner == guid) {
        a.unit = UnitOf(guid, name, flags);
        return a;
    }
    a.unit = UnitOf(owner, CombatLog::NameOf(owner),
                    flags & ~(Flag::TYPE_PET | Flag::TYPE_GUARDIAN));
    a.petName = name;
    return a;
}

// The meter's event for an entry; false for an entry it does not count.
// Environmental damage has no source and is left out (our choice).
bool EventOf(const CombatLog::Entry &e, DamageMeter::MeterEvent &out) {
    using namespace CombatLog;
    out.spellID = static_cast<uint32_t>(e.spellId);
    switch (e.event) {
    case SWING_DAMAGE:
        out.spellID = kMeleeSpellID;
        [[fallthrough]];
    case RANGE_DAMAGE:
    case SPELL_DAMAGE:
    case SPELL_PERIODIC_DAMAGE:
    case DAMAGE_SHIELD:
    case DAMAGE_SPLIT:
        out.kind = EventKind::Damage;
        out.amount = e.v[1];
        out.overkill = e.v[2];
        break;
    case SPELL_HEAL:
    case SPELL_PERIODIC_HEAL:
        out.kind = EventKind::Heal;
        out.amount = e.v[0];
        out.overhealing = e.v[1];
        break;
    // Our choice: an interrupt or a dispel is listed under the spell it
    // stopped or removed (the extra spell), which 1.12 always names; the
    // dispelling cast is unnamed when the client saw no cast (Auras.cpp).
    case SPELL_INTERRUPT:
        out.kind = EventKind::Interrupt;
        out.spellID = static_cast<uint32_t>(e.v[0]);
        break;
    case SPELL_DISPEL:
        out.kind = EventKind::Dispel;
        out.spellID = static_cast<uint32_t>(e.v[0]);
        break;
    case UNIT_DIED:
        out.kind = EventKind::Death;
        break;
    default:
        return false;
    }
    if (out.kind != EventKind::Death && e.source == 0)
        return false;
    out.timeMs = Clock();
    // Only the source is credited to its owner (RedirectSourceToOwner): a
    // pet that takes a hit or dies is its own unit.
    out.source = ActorOf(e.source, e.sourceName, e.sourceFlags);
    out.dest.unit = UnitOf(e.dest, e.destName, e.destFlags);
    return true;
}

// A pull: damage between an ally and an enemy, by the entry's flags.
bool Pull(const CombatLog::Entry &e, const DamageMeter::MeterEvent &event) {
    if (event.kind != EventKind::Damage)
        return false;
    const auto fights = [](uint32_t a, uint32_t b) {
        return (a & Flag::AFFILIATION_GROUP) != 0 && (b & Flag::REACTION_ENEMY) != 0;
    };
    return fights(e.sourceFlags, e.destFlags) || fights(e.destFlags, e.sourceFlags);
}

} // namespace

void Observe(const CombatLog::Entry &entry) {
    if (!Enabled())
        return;
    DamageMeter::MeterEvent event;
    if (!EventOf(entry, event))
        return;
    if (!OpenEncounter() && !g_meter.IsOpen() && !g_waitCombatDrop && Pull(entry, event))
        Open(std::string(), 0, false);
    const uint32_t changed = g_meter.Feed(event);
    if (changed != 0)
        g_dirty[g_meter.Current()->id] |= changed;
}

const Meter &Sessions() { return g_meter; }

int64_t NowMs() { return Clock(); }

void ResetAll() { Clear(true); }

} // namespace DamageMeter::Live
