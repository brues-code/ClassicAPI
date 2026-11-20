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

#pragma once

// The damage meter's totals: sessions -> sources -> spells -> targets, for
// each of retail's storages (Enum.DamageMeterStorageType), fed with neutral
// events. Nothing here knows where an event comes from: the live front end
// (damagemeter/Live.cpp) feeds it from the combat log entries, and the replay
// can feed it from a recording with its own unit keys. The caller resolves
// each unit's identity and owner; the meter applies retail's rules for which
// storage an event fills.
//
// Times are milliseconds on the caller's clock, any epoch, as long as it only
// moves forward.

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>

namespace DamageMeter {

// Enum.DamageMeterType.
enum Type : int {
    TYPE_DAMAGE_DONE = 0,
    TYPE_DPS = 1,
    TYPE_HEALING_DONE = 2,
    TYPE_HPS = 3,
    TYPE_ABSORBS = 4,
    TYPE_INTERRUPTS = 5,
    TYPE_DISPELS = 6,
    TYPE_DAMAGE_TAKEN = 7,
    TYPE_AVOIDABLE_DAMAGE_TAKEN = 8,
    TYPE_DEATHS = 9,
    TYPE_ENEMY_DAMAGE_TAKEN = 10,
    TYPE_COUNT = 11,
};

// Enum.DamageMeterSessionType.
enum SessionType : int {
    SESSION_OVERALL = 0,
    SESSION_CURRENT = 1,
    SESSION_EXPIRED = 2,
    SESSION_TYPE_COUNT = 3,
};

// Enum.DamageMeterSourceDisplayType.
enum SourceDisplayType : int {
    DISPLAY_NONE = 0,
    DISPLAY_ALLY = 1,
    DISPLAY_ENEMY = 2,
};

// Enum.DamageMeterStorageType: what the meter keeps. Two types read one
// storage (DamageDone and Dps, HealingDone and Hps), as in retail.
enum Storage : int {
    STORAGE_DAMAGE = 0,
    STORAGE_HEALING_AND_ABSORBS = 1,
    STORAGE_ABSORBS = 2,
    STORAGE_INTERRUPTS = 3,
    STORAGE_DISPELS = 4,
    STORAGE_DAMAGE_TAKEN = 5,
    STORAGE_AVOIDABLE_DAMAGE_TAKEN = 6,
    STORAGE_DEATHS = 7,
    STORAGE_ENEMY_DAMAGE_TAKEN = 8,
    STORAGE_COUNT = 9,
};

// The storage a DamageMeterType reads, -1 for a value that is not one.
int StorageOf(int type);

// A unit as the meter shows it. `key` is the caller's identity for it: two
// units with one key are one source. The other fields are what the Lua
// tables carry.
struct MeterUnit {
    uint64_t key = 0;            // 0: no unit
    std::string guid;            // the WOWGUID string to push; empty pushes nil
    std::string name;
    std::string classFilename;   // a player's class token ("MAGE"), else ""
    std::string classification;  // UnitClassification's word
    std::string factionGroup;    // a player's "Alliance" / "Horde", else ""
    uint32_t creatureID = 0;     // 0 pushes nil
    bool isPlayer = false;
    bool isPet = false;
    bool isLocalPlayer = false;
    bool ally = false;    // the player, its group, or a unit one of them controls
    bool enemy = false; // hostile or neutral to the player
};

// One side of an event, its owner already resolved by the caller: `unit` is
// who the meter credits (a pet's or a guardian's owner, as retail's
// RedirectSourceToOwner), and `petName` the name of the minion that acted
// when it was one, else "".
struct Actor {
    MeterUnit unit;
    std::string petName;
};

enum class EventKind {
    Damage,    // amount, overkill
    Heal,      // amount, overhealing
    Interrupt, // the spell the source interrupted
    Dispel,    // the spell the source dispelled
    Death,     // dest died
};

// One combat event. The meter does not check the kind's fields against the
// subevent: the caller maps its own events (a combat log entry, a replay
// line) to these. Deaths take no spell; the meter keeps the killing hit.
struct MeterEvent {
    EventKind kind = EventKind::Damage;
    int64_t timeMs = 0;
    Actor source;
    Actor dest;
    uint32_t spellID = 0;
    double amount = 0;
    double overkill = 0;
    double overhealing = 0;
    int deathRecapID = 0; // a Death's C_DeathRecap ID; 0 for none
};

// The amount one spell of a source did to, or took from, one other unit.
struct Target {
    MeterUnit unit;
    double amount = 0;
    double overkill = 0;
    bool deadly = false;
};

// A spell of a source: its ID and, for a minion's spell credited to its
// owner, the minion's name.
struct SpellKey {
    uint32_t spellID = 0;
    std::string creatureName;
    bool operator<(const SpellKey &o) const {
        return spellID != o.spellID ? spellID < o.spellID : creatureName < o.creatureName;
    }
};

struct Spell {
    SpellKey key;
    double amount = 0;
    double overkill = 0;
    bool deadly = false;
    std::unordered_map<uint64_t, Target> targets; // by Unit::key
};

struct Source {
    MeterUnit unit;
    double total = 0;
    int64_t deathMs = 0; // the time of its last death, for STORAGE_DEATHS
    int deathRecapID = 0; // that death's C_DeathRecap ID; 0 for none
    std::map<SpellKey, Spell> spells;
};

// Every storage's sources, by Unit::key.
struct Totals {
    std::unordered_map<uint64_t, Source> storages[STORAGE_COUNT];
    bool Empty() const;
    void Merge(const Totals &from);
};

struct Session {
    uint32_t id = 0;
    std::string name; // "" until an ally fights an enemy
    int64_t startMs = 0;
    int64_t endMs = 0;
    bool open = false;
    Totals totals;
    int64_t DurationMs(int64_t nowMs) const;
};

// What a C_DamageMeter query reads: totals over a duration. `totals` null is
// an empty session. `hasDuration` false pushes durationSeconds nil;
// `timedDeaths` false pushes deathTimeSeconds -1 (retail's Overall: "no
// timestamps for overall session", DamageMeterEntry.lua), else the seconds
// from `startMs`.
struct View {
    const Totals *totals = nullptr;
    double seconds = 0;
    bool hasDuration = false;
    bool timedDeaths = false;
    int64_t startMs = 0;
};

class Meter {
public:
    // Retail keeps 30 sessions, oldest first; the 31st drops the oldest.
    static constexpr size_t kMaxSessions = 30;

    // Drops every session, Overall and Expired. Session IDs keep counting.
    void Reset();

    // Ends the open session, if any, and opens a new one; returns its ID.
    uint32_t Open(int64_t nowMs, const std::string &name);
    // Ends the open session.
    void Close(int64_t nowMs);
    bool IsOpen() const;

    // Adds the event to the open session and to Overall. Returns the
    // storages it changed as bits (1 << Storage); 0 while no session is
    // open, or when the event fills none. Deaths need the hit that killed:
    // the meter keeps each ally's last damage taken, also between sessions.
    uint32_t Feed(const MeterEvent &event);

    // The sessions kept, oldest first.
    const std::deque<Session> &Sessions() const { return sessions_; }
    // The latest session, or null.
    const Session *Current() const;
    const Session *Find(uint32_t sessionID) const;

    View ViewOfType(int sessionType, int64_t nowMs) const;
    View ViewOfSession(uint32_t sessionID, int64_t nowMs) const;

private:
    void Add(int storage, const Actor &subject, const Actor &other, uint32_t spellID,
             const std::string &creatureName, double amount, double overkill, bool deadly,
             int64_t timeMs, int deathRecapID);
    void Name(const MeterEvent &event);

    struct LastHit {
        Actor attacker;
        uint32_t spellID = 0;
        double amount = 0;
        double overkill = 0;
    };

    std::deque<Session> sessions_;
    uint32_t nextID_ = 1;
    Totals overall_;
    int64_t overallClosedMs_ = 0; // the ended sessions' time since the reset
    bool overallAny_ = false;     // a session opened since the reset
    Totals expired_;              // the sessions dropped since the reset
    int64_t expiredMs_ = 0;
    bool expiredAny_ = false;
    std::unordered_map<uint64_t, LastHit> lastHits_;
};

} // namespace DamageMeter
