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

// `C_LossOfControl` — the loss-of-control effects on the LOCAL PLAYER, as the
// 3.80.2 client keeps and serves them.
//
// 3.80.2 gets every effect from the server: CLIENT_LOSS_OF_CONTROL_AURA_UPDATE
// (0x46011C, one entry per aura effect: slot, effect, type, mechanic) and
// CLIENT_ADD_LOSS_OF_CONTROL (0x46011D, a timed entry with no aura: type,
// mechanic, spell, timer, lockout school). It keeps one list per unit
// (FUN_14197ebc0 inserts, FUN_14197e4e0 expires the timed ones). 1.12 sends
// no such packet, so the entries are found here:
//
//   1. SCHOOL_INTERRUPT — the Counterspell/Kick school lockout. The server
//      (Player::ProhibitSpellSchool) delivers it as a single
//      SMSG_SPELL_COOLDOWN carrying every spell of the interrupted school,
//      all with the same duration. That "player-GUID packet, >=2 spells, one
//      uniform duration" shape is produced by nothing else, so matching it
//      yields an exact school and duration. The interrupting spell isn't in
//      the packet, so spellID is 0. These are 3.80.2's timed entries (slot
//      -1), removed when the timer ends.
//
//   2. Aura effects — one entry per effect of a player debuff whose aura type
//      is a control loss (possess, confuse, charm, fear, stun, pacify, root,
//      silence, pacify-silence, disarm), keyed like 3.80.2's by (slot,
//      effect). The mechanic is the effect's, else the spell's. Timing is the
//      server's own for the player's auras (`Aura::Data::PlayerAuraTiming`).
//      As in 3.80.2, an aura entry stays until the aura goes, with
//      timeRemaining 0 once its timer has run out.
//
// The once-per-frame check below keeps the list the way 3.80.2's insert does
// and fires the events as 3.80.2 does at each insert; the getters read that
// list, as 3.80.2's read the list its packets keep.

#include "Game.h"
#include "Offsets.h"
#include "aura/Data.h"
#include "aura/Instance.h"
#include "duration/Object.h"
#include "event/Custom.h"
#include "lossofcontrol/Lockout.h"
#include "net/PacketDispatch.h"
#include "net/PacketReader.h"
#include "spell/Lookup.h"
#include "tick/WorldTick.h"
#include "time/Clock.h"
#include "unit/Identity.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace LossOfControl {

namespace {

using Time::Clock::NowMs;
using Time::Clock::Reached;

// ---- 3.80.2's per-type tables (0x1444e2e50 / 0x1444e2ed0 / 0x1444e2f10) ----

enum Type {
    TYPE_NONE = 0,
    TYPE_POSSESS = 1,
    TYPE_CONFUSE = 2,
    TYPE_CHARM = 3,
    TYPE_FEAR = 4,
    TYPE_STUN = 5,
    TYPE_PACIFY = 6,
    TYPE_ROOT = 7,
    TYPE_SILENCE = 8,
    TYPE_PACIFYSILENCE = 9,
    TYPE_DISARM = 10,
    TYPE_SCHOOL_INTERRUPT = 11,
    TYPE_COUNT = 14, // 12 STUN_MECHANIC, 13 FEAR_MECHANIC: server-chosen only
};

struct TypeInfo {
    const char *locType;
    int priority;
    int displayType; // 0 none, 1 alert, 2 full
};

const TypeInfo kTypes[TYPE_COUNT] = {
    {"NONE", 0, 0},         {"POSSESS", 5, 2},          {"CONFUSE", 5, 2},
    {"CHARM", 5, 2},        {"FEAR", 5, 2},             {"STUN", 5, 2},
    {"PACIFY", 3, 1},       {"ROOT", 1, 0},             {"SILENCE", 3, 1},
    {"PACIFYSILENCE", 4, 1}, {"DISARM", 3, 1},          {"SCHOOL_INTERRUPT", 2, 1},
    {"STUN_MECHANIC", 5, 2}, {"FEAR_MECHANIC", 5, 2},
};

// The control-loss type of an aura effect, by its aura type; NONE otherwise.
int TypeForAura(int aura) {
    switch (aura) {
    case Offsets::SPELL_AURA_MOD_POSSESS: return TYPE_POSSESS;
    case Offsets::SPELL_AURA_MOD_CONFUSE: return TYPE_CONFUSE;
    case Offsets::SPELL_AURA_MOD_CHARM: return TYPE_CHARM;
    case Offsets::SPELL_AURA_MOD_FEAR: return TYPE_FEAR;
    case Offsets::SPELL_AURA_MOD_STUN: return TYPE_STUN;
    case Offsets::SPELL_AURA_MOD_PACIFY: return TYPE_PACIFY;
    case Offsets::SPELL_AURA_MOD_ROOT: return TYPE_ROOT;
    case Offsets::SPELL_AURA_MOD_SILENCE: return TYPE_SILENCE;
    case Offsets::SPELL_AURA_MOD_PACIFY_SILENCE: return TYPE_PACIFYSILENCE;
    case Offsets::SPELL_AURA_MOD_DISARM: return TYPE_DISARM;
    default: return TYPE_NONE;
    }
}

// 3.80.2's mechanic names (0x143cffc90), the keys of the display strings. The
// ids are 1.12's SpellMechanic ids too. Id 8 is 1.12's Pacify; 3.80.2 marks
// that id "zzDEPRECATED / REUSE".
const char *const kMechanicNames[] = {
    "",                    "Charm",        "Disorient",       "Disarm",
    "Distract",            "Fear",         "Death Grip/Knockback", "Root",
    "Pacify",              "Silence",      "Sleep",           "Snare",
    "Stun",                "Freeze",       "Incapacitate",    "Bleed",
    "Provoke",             "Polymorph",    "Banish",          "HW: Shield",
    "Shackle Undead",      "Mounted",      "Encounter 0",     "Turn Undead",
    "Horror",              "Invulnerability", "Interrupt",    "Daze",
    "Discovery (alchemy)", "Magical Immunity", "Sap",         "Enrage",
    "Wound",               "Encounter 1",  "Encounter 2",     "Encounter 3",
    "Taunt",
};
constexpr int MECHANIC_COUNT = sizeof(kMechanicNames) / sizeof(kMechanicNames[0]);

// 3.80.2's display text: the global string LOSS_OF_CONTROL_DISPLAY_<name>, the
// mechanic's name (1..36) upper-cased with every non-letter made `_`, else the
// locType; the key itself when that global is missing or empty.
const char *DisplayText(int type, int mechanic) {
    static char key[96];
    const char *name = (mechanic > 0 && mechanic < MECHANIC_COUNT) ? kMechanicNames[mechanic]
                                                                   : kTypes[type].locType;
    int n = std::snprintf(key, sizeof(key), "LOSS_OF_CONTROL_DISPLAY_");
    for (const char *p = name; *p != '\0' && n < static_cast<int>(sizeof(key)) - 1; ++p)
        key[n++] = std::isalpha(static_cast<unsigned char>(*p))
                       ? static_cast<char>(std::toupper(static_cast<unsigned char>(*p)))
                       : '_';
    key[n] = '\0';
    return Game::Lua::GlobalTextOr(key, key);
}

// ---- School-interrupt lockout state (fed by SMSG_SPELL_COOLDOWN) -----------

// One active lockout per school index (0..6); active until `Reached(endMs)`.
// Unsigned, like every engine-tick store. See `Time::Clock`.
struct SchoolLock {
    uint32_t startMs;
    uint32_t endMs;
};
SchoolLock g_schoolLock[Offsets::SPELL_SCHOOL_COUNT] = {};

int SpellSchoolIndex(const uint8_t *rec) {
    return Game::Read<int>(rec, Offsets::OFF_SPELL_RECORD_SCHOOL);
}

// Subscribe to the SMSG dispatch funnel rather than co-hooking the cooldown
// handler (0x006E9460) directly — nampower detours that leaf handler, so
// sharing its prologue is the documented MinHook/hadesmem collision risk;
// the funnel is a chokepoint nobody else hooks. The dispatcher positions the
// cursor at the body (after the u16 opcode) and restores it afterward, so we
// just read.
void CooldownSub(uint32_t opcode, Net::CDataStore *packet) {
    if (opcode != Offsets::SMSG_SPELL_COOLDOWN || packet == nullptr)
        return;
    const uint64_t guid = Net::Read<uint64_t>(packet);
    if (guid == 0 || guid != Unit::Identity::PlayerGuid())
        return;
    int count = 0;
    int firstDur = 0;
    bool uniform = true;
    int schoolIdx = -1;
    while (packet->m_read + 8 <= packet->m_size && count < 256) {
        const uint32_t spellID = Net::Read<uint32_t>(packet);
        const int dur = static_cast<int>(Net::Read<uint32_t>(packet));
        if (count == 0)
            firstDur = dur;
        else if (dur != firstDur)
            uniform = false;
        if (schoolIdx < 0) {
            const uint8_t *rec = Spell::Lookup::RecordForID(static_cast<int>(spellID));
            if (rec != nullptr) {
                const int s = SpellSchoolIndex(rec);
                if (s >= 0 && s < Offsets::SPELL_SCHOOL_COUNT)
                    schoolIdx = s;
            }
        }
        ++count;
    }
    // A uniform-duration batch of >=2 same-school spells is a school lockout
    // (ProhibitSpellSchool); single/varied packets are normal cooldowns and
    // ignored.
    if (count >= 2 && uniform && firstDur > 0 && schoolIdx >= 0) {
        const uint32_t now = NowMs();
        g_schoolLock[schoolIdx].startMs = now;
        g_schoolLock[schoolIdx].endMs = now + static_cast<uint32_t>(firstDur);
    }
}

const Net::PacketDispatch::AutoSubscribe _cooldownSub{&CooldownSub};

// ---- The list (3.80.2's LossOfControlEvent, 0x38 bytes) --------------------

struct Entry {
    int type;
    int spellID;          // 0 for a school lockout
    int slot;             // the debuff's player aura slot; -1 for a school lockout
    int effect;           // the aura effect 0..2; for a school lockout, its school index
    int mechanic;
    int lockoutSchool;    // the locked school mask; 0 for an aura effect
    uint32_t endMs;       // 0 = no timer
    uint32_t durationMs;
    uint32_t auraInstanceID; // 0 = none
};

std::vector<Entry> g_list;

bool SameEntry(const Entry &a, const Entry &b) {
    return a.slot == b.slot && a.effect == b.effect && a.spellID == b.spellID;
}

// 3.80.2's insert (FUN_14197ebc0): a full-screen entry goes before the first
// entry with a lower priority, one that is not full screen, or one with the
// same priority and a timer that ends before the new one's; every other entry
// goes at the back. Returns the 0-based position.
int Insert(const Entry &e) {
    const TypeInfo &t = kTypes[e.type];
    if (t.displayType == 2) {
        for (size_t i = 0; i < g_list.size(); ++i) {
            const Entry &n = g_list[i];
            const TypeInfo &nt = kTypes[n.type];
            if (nt.priority < t.priority || nt.displayType != 2 ||
                (nt.priority == t.priority && n.endMs != 0 && !Reached(n.endMs, e.endMs))) {
                g_list.insert(g_list.begin() + static_cast<std::ptrdiff_t>(i), e);
                return static_cast<int>(i);
            }
        }
    }
    g_list.push_back(e);
    return static_cast<int>(g_list.size()) - 1;
}

// The entries that are true now: live school lockouts, then every
// control-loss effect of the player's debuffs in UnitDebuff's order.
void Collect(std::vector<Entry> &out) {
    const uint32_t now = NowMs();
    for (int s = 0; s < Offsets::SPELL_SCHOOL_COUNT; ++s) {
        const SchoolLock &lock = g_schoolLock[s];
        if (lock.endMs == 0 || Reached(now, lock.endMs))
            continue;
        out.push_back(Entry{TYPE_SCHOOL_INTERRUPT, 0, -1, s, 0, 1 << s, lock.endMs,
                            lock.endMs - lock.startMs, 0});
    }

    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return;
    int order[Offsets::UNIT_AURA_TOTAL];
    Aura::Data::WalkOrder(player, Aura::Data::Filter::Harmful, order);
    for (int i = 0; i < Offsets::UNIT_AURA_TOTAL; ++i) {
        const int slot = order[i];
        if (!Aura::Data::SlotMatchesFilter(player, slot, Aura::Data::Filter::Harmful,
                                           Aura::Data::Match{}))
            continue;
        const uint32_t spellID = Aura::Data::ReadSpellID(player, slot);
        const uint8_t *rec =
            spellID != 0 ? Spell::Lookup::RecordForID(static_cast<int>(spellID)) : nullptr;
        if (rec == nullptr)
            continue;
        uint32_t endMs = 0, durMs = 0;
        if (!Aura::Data::PlayerAuraTiming(slot, &endMs, &durMs))
            endMs = durMs = 0;
        for (int fx = 0; fx < Offsets::SPELL_RECORD_EFFECT_COUNT; ++fx) {
            const int type = TypeForAura(
                Game::Read<int>(rec, Offsets::OFF_SPELL_RECORD_EFFECT_APPLY_AURA_NAME + fx * 4));
            if (type == TYPE_NONE)
                continue;
            int mechanic = Game::Read<int>(rec, Offsets::OFF_SPELL_RECORD_EFFECT_MECHANIC + fx * 4);
            if (mechanic == 0)
                mechanic = Game::Read<int>(rec, Offsets::OFF_SPELL_RECORD_MECHANIC);
            out.push_back(Entry{type, static_cast<int>(spellID), slot, fx, mechanic, 0, endMs,
                                durMs, Aura::Instance::IDForSlot(player, slot)});
        }
    }
}

// ---- LOSS_OF_CONTROL_ADDED / _UPDATE ---------------------------------------
//
// 1.12 sends no loss-of-control packet, and an effect's end (a lockout timing
// out, a debuff falling off) has no packet or callback of its own, so the
// list is brought up to date once per frame. Firing from the WorldTick tail
// keeps the Lua handlers out of the mid-packet contexts the state is updated
// in (the OnTooltipSetItem fire-safety lesson).

const Game::Doc::Field kAddedPayload[] = {
    Game::Doc::Req("unitTarget", "UnitToken", "Always \"player\"."),
    Game::Doc::Req("effectIndex", "luaIndex",
                   "The effect's index for C_LossOfControl.GetActiveLossOfControlData."),
};
const Game::Doc::Event kAddedDoc{
    "LossOfControl",
    "Fires when a loss-of-control effect lands on the player, or a running one is extended.",
    kAddedPayload};
const Game::Doc::Field kUpdatePayload[] = {
    Game::Doc::Req("unitTarget", "UnitToken", "Always \"player\"."),
};
const Game::Doc::Event kUpdateDoc{
    "LossOfControl",
    "Fires once per frame in which the player's loss-of-control effects were added, changed, "
    "fell off or expired.",
    kUpdatePayload};

const Event::Custom::AutoReserve _reserveAdded{"LOSS_OF_CONTROL_ADDED", &kAddedDoc};
const Event::Custom::AutoReserve _reserveUpdate{"LOSS_OF_CONTROL_UPDATE", &kUpdateDoc};

void OnWorldTick() {
    std::vector<Entry> now;
    Collect(now);
    bool changed = false;

    // Gone: an aura effect whose aura left, a lockout whose timer ran out.
    for (size_t i = 0; i < g_list.size();) {
        bool live = false;
        for (const Entry &e : now)
            live = live || SameEntry(g_list[i], e);
        if (live) {
            ++i;
        } else {
            g_list.erase(g_list.begin() + static_cast<std::ptrdiff_t>(i));
            changed = true;
        }
    }

    const uint32_t tick = NowMs();
    for (const Entry &e : now) {
        size_t at = g_list.size();
        for (size_t i = 0; i < g_list.size(); ++i)
            if (SameEntry(g_list[i], e))
                at = i;

        if (at == g_list.size()) {
            // 3.80.2's add (FUN_14197eed0): not already over, and an aura or
            // a timer to hold it.
            if (e.endMs != 0 && Reached(tick, e.endMs))
                continue;
            const int index = Insert(e);
            changed = true;
            Event::Custom::Fire(_reserveAdded.Slot(), "%s%d", "player", index + 1);
            continue;
        }

        const Entry old = g_list[at];
        // The timer read answers "none" once it has run out; the entry keeps
        // its timer, as 3.80.2's stays until the aura goes.
        if (e.endMs == 0 && old.endMs != 0 && Reached(tick, old.endMs))
            continue;
        if (old.endMs == e.endMs && old.durationMs == e.durationMs &&
            old.auraInstanceID == e.auraInstanceID)
            continue;
        // A refresh: 3.80.2 unlinks the entry and inserts it again, firing
        // ADDED only when the new end is later.
        g_list.erase(g_list.begin() + static_cast<std::ptrdiff_t>(at));
        const int index = Insert(e);
        changed = true;
        if (!Reached(old.endMs, e.endMs))
            Event::Custom::Fire(_reserveAdded.Slot(), "%s%d", "player", index + 1);
    }

    if (changed)
        Event::Custom::Fire(_reserveUpdate.Slot(), "%s", "player");
}

static const Tick::WorldTick::AutoSubscribe _tick{&OnWorldTick};

// ---- Which spells an effect blocks ------------------------------------------

// The caster validator's (`FUN_CHECK_CASTER_STATE_FOR_SPELL`) refusal arm a
// control-loss effect falls under, as that arm's exemption scan; 0 when the
// effect stops no cast of this PreventionType. Stun / fear / confuse / charm
// / possess gate every cast; silence and pacify gate by the spell's
// PreventionType (UNIT_FLAG_SILENCED ↔ SPELL_PREVENTION_TYPE_SILENCE,
// UNIT_FLAG_PACIFIED ↔ _PACIFY). Root and disarm stop no cast.
uintptr_t ArmScan(int type, int preventionType) {
    switch (type) {
    case TYPE_STUN: return Offsets::FUN_SCAN_STUN_AURAS;
    case TYPE_FEAR: return Offsets::FUN_SCAN_FEAR_AURAS;
    case TYPE_CONFUSE: return Offsets::FUN_SCAN_CONFUSE_AURAS;
    case TYPE_CHARM:
    case TYPE_POSSESS: return Offsets::FUN_SCAN_CHARM_AURAS;
    default: break;
    }
    const bool both = type == TYPE_PACIFYSILENCE;
    if ((both || type == TYPE_SILENCE) &&
        preventionType == Offsets::SPELL_PREVENTION_TYPE_SILENCE)
        return Offsets::FUN_SCAN_SILENCE_AURAS;
    if ((both || type == TYPE_PACIFY) && preventionType == Offsets::SPELL_PREVENTION_TYPE_PACIFY)
        return Offsets::FUN_SCAN_PACIFY_AURAS;
    return 0;
}

using ScanAuras_t = uint8_t(__fastcall *)(const void *unit, const uint8_t *castRecord,
                                          uint32_t *mechanic);

// Whether an effect blocks casting `castRec`. A school lockout gates by the
// spell's school (in 1.12 the lockout is a cooldown on each spell of the
// school, as 3.80.2's per-spell view reads it from the cooldown list).
// Anything else asks its validator arm's own scan, which lets through a spell
// granting immunity to every aura of the arm — a druid form under Polymorph.
bool Blocks(const Entry &e, const uint8_t *castRec, int schoolIdx, int preventionType) {
    if (e.type == TYPE_SCHOOL_INTERRUPT)
        return (e.lockoutSchool & (1 << schoolIdx)) != 0;
    const uintptr_t scan = ArmScan(e.type, preventionType);
    if (scan == 0)
        return false;
    const uint8_t *player = Unit::Identity::PlayerObject();
    return player == nullptr || reinterpret_cast<ScanAuras_t>(scan)(player, castRec, nullptr) == 0;
}

// ---- Lua ---------------------------------------------------------------------

// 3.80.2's index argument (FUN_140a6bf10): a number 0..4294967295, less one.
bool ReadIndex(void *L, int idx, const char *usage, int &out) {
    if (!Game::Lua::IsNumber(L, idx)) {
        Game::Lua::ArgError(L, idx, usage);
        return false;
    }
    const double v = Game::Lua::ToNumber(L, idx);
    if (!(v >= 0.0 && v <= 4294967295.0)) {
        Game::Lua::ArgError(L, idx, usage);
        return false;
    }
    out = static_cast<int>(static_cast<uint32_t>(v)) - 1;
    return true;
}

// The unit token argument: true when it names the player, the one unit this
// client's list is for.
bool ReadUnitIsPlayer(void *L, const char *usage, bool &isPlayer) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::ArgError(L, 1, usage);
        return false;
    }
    const uint64_t guid = Unit::Identity::GuidForToken(Game::Lua::ToString(L, 1));
    isPlayer = guid != 0 && guid == Unit::Identity::PlayerGuid();
    return true;
}

const Entry *At(int index0) {
    return index0 >= 0 && index0 < static_cast<int>(g_list.size()) ? &g_list[index0] : nullptr;
}

// The table 3.80.2's FUN_140d208c0 pushes.
void PushData(void *L, const Entry &e) {
    const TypeInfo &t = kTypes[e.type];
    const uint8_t *rec = e.spellID != 0 ? Spell::Lookup::RecordForID(e.spellID) : nullptr;
    const char *icon = rec != nullptr ? Spell::Lookup::IconPath(rec, false) : nullptr;

    Game::Lua::NewTable(L);
    Game::Lua::SetFieldString(L, "locType", t.locType);
    Game::Lua::SetFieldNumber(L, "spellID", static_cast<double>(e.spellID));
    Game::Lua::SetFieldString(L, "displayText", DisplayText(e.type, e.mechanic));
    Game::Lua::SetFieldString(L, "iconTexture", icon != nullptr ? icon : "");
    if (e.endMs != 0) {
        const uint32_t now = NowMs();
        Game::Lua::SetFieldNumber(L, "startTime",
                                  static_cast<double>(e.endMs - e.durationMs) * 0.001);
        Game::Lua::SetFieldNumber(L, "timeRemaining",
                                  static_cast<double>(Time::Clock::Remaining(now, e.endMs)) *
                                      0.001);
        Game::Lua::SetFieldNumber(L, "duration", static_cast<double>(e.durationMs) * 0.001);
    }
    Game::Lua::SetFieldNumber(L, "lockoutSchool", static_cast<double>(e.lockoutSchool));
    Game::Lua::SetFieldNumber(L, "priority", t.priority);
    Game::Lua::SetFieldNumber(L, "displayType", t.displayType);
    if (e.auraInstanceID != 0)
        Game::Lua::SetFieldNumber(L, "auraInstanceID", static_cast<double>(e.auraInstanceID));
}

#define USAGE_DATA "Usage: local event = C_LossOfControl.GetActiveLossOfControlData(index)"
#define USAGE_DATA_BY_UNIT                                                                    \
    "Usage: local event = C_LossOfControl.GetActiveLossOfControlDataByUnit(unitToken, index)"
#define USAGE_COUNT_BY_UNIT                                                                   \
    "Usage: local count = C_LossOfControl.GetActiveLossOfControlDataCountByUnit(unitToken)"
#define USAGE_DURATION                                                                        \
    "Usage: local duration = C_LossOfControl.GetActiveLossOfControlDuration(unitToken, index)"

int __fastcall Script_GetActiveLossOfControlDataCount(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(g_list.size()));
    return 1;
}

int __fastcall Script_GetActiveLossOfControlDataCountByUnit(void *L) {
    bool isPlayer = false;
    if (!ReadUnitIsPlayer(L, USAGE_COUNT_BY_UNIT, isPlayer))
        return 0;
    Game::Lua::PushNumber(L, isPlayer ? static_cast<double>(g_list.size()) : 0.0);
    return 1;
}

int __fastcall Script_GetActiveLossOfControlData(void *L) {
    int index0 = 0;
    if (!ReadIndex(L, 1, USAGE_DATA, index0))
        return 0;
    const Entry *e = At(index0);
    if (e == nullptr)
        Game::Lua::PushNil(L);
    else
        PushData(L, *e);
    return 1;
}

int __fastcall Script_GetActiveLossOfControlDataByUnit(void *L) {
    bool isPlayer = false;
    int index0 = 0;
    if (!ReadUnitIsPlayer(L, USAGE_DATA_BY_UNIT, isPlayer) ||
        !ReadIndex(L, 2, USAGE_DATA_BY_UNIT, index0))
        return 0;
    const Entry *e = isPlayer ? At(index0) : nullptr;
    if (e == nullptr)
        Game::Lua::PushNil(L);
    else
        PushData(L, *e);
    return 1;
}

// 3.80.2 builds the object from the entry's end and duration
// (FUN_141ba4870(obj, end, duration, 1.0)).
int __fastcall Script_GetActiveLossOfControlDuration(void *L) {
    bool isPlayer = false;
    int index0 = 0;
    if (!ReadUnitIsPlayer(L, USAGE_DURATION, isPlayer) ||
        !ReadIndex(L, 2, USAGE_DURATION, index0))
        return 0;
    const Entry *e = isPlayer ? At(index0) : nullptr;
    if (e == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Duration::Object::PushFromStart(L, e->endMs != 0 ? e->endMs - e->durationMs : 0,
                                    e->endMs != 0 ? e->durationMs : 0);
    return 1;
}

// `C_LossOfControl.GetSchoolLockout([filterMask])` -> lockedMask, seconds. A
// ClassicAPI extension: the school-interrupt slice of the list, read from
// `g_schoolLock` directly, so it allocates no table. Cheap enough for
// per-frame conditional evaluation.
//
// `lockedMask` ORs *every* currently locked school (`1 << schoolIndex`, the
// same shape as `lockoutSchool`). Two schools can be locked at once — each
// SMSG_SPELL_COOLDOWN batch locks one — so a caller that stops at the first
// SCHOOL_INTERRUPT entry silently drops the rest. `seconds` runs until every
// school in `lockedMask` is clear; nil when nothing is locked. `filterMask`
// narrows the scan to those schools; omitted or 0 means all schools.
int __fastcall Script_GetSchoolLockout(void *L) {
    int filter = ~0;
    if (Game::Lua::IsNumber(L, 1)) {
        const int arg = static_cast<int>(Game::Lua::ToNumber(L, 1));
        if (arg != 0)
            filter = arg;
    }

    const uint32_t now = NowMs();
    int locked = 0;
    uint32_t longest = 0;
    for (int s = 0; s < Offsets::SPELL_SCHOOL_COUNT; ++s) {
        const int bit = 1 << s;
        if ((filter & bit) == 0)
            continue;
        const uint32_t endMs = g_schoolLock[s].endMs;
        if (endMs == 0 || Reached(now, endMs))
            continue;
        locked |= bit;
        // Compare durations, never raw ticks -- see `Time::Clock`.
        const uint32_t remain = Time::Clock::Remaining(now, endMs);
        if (remain > longest)
            longest = remain;
    }

    Game::Lua::PushNumber(L, static_cast<double>(locked));
    if (locked == 0)
        return 1;
    Game::Lua::PushNumber(L, static_cast<double>(longest) * 0.001);
    return 2;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kCountRets[] = {
    Game::Doc::Req("count", "number"),
};
const Game::Doc::Function kGetActiveLossOfControlDataCount{
    "Returns the number of loss-of-control effects on the player.", {}, kCountRets};

const Game::Doc::Field kUnitArgs[] = {
    Game::Doc::Req("unitToken", "UnitToken"),
};
const Game::Doc::Function kGetActiveLossOfControlDataCountByUnit{
    "Returns the number of loss-of-control effects on a unit; 0 for any unit but the player.",
    kUnitArgs, kCountRets};

const Game::Doc::Field kDataFields[] = {
    Game::Doc::Req("locType", "cstring",
                   "\"POSSESS\", \"CONFUSE\", \"CHARM\", \"FEAR\", \"STUN\", \"PACIFY\", "
                   "\"ROOT\", \"SILENCE\", \"PACIFYSILENCE\", \"DISARM\" or "
                   "\"SCHOOL_INTERRUPT\"."),
    Game::Doc::Req("spellID", "number", "0 for a school interrupt."),
    Game::Doc::Req("displayText", "cstring",
                   "The LOSS_OF_CONTROL_DISPLAY_ global string of the effect's mechanic, else "
                   "of its locType."),
    Game::Doc::Req("iconTexture", "string", "Icon texture path; \"\" for a school interrupt."),
    Game::Doc::Opt("startTime", "number", nullptr, "In GetTime() seconds; nil without a timer."),
    Game::Doc::Opt("timeRemaining", "number", nullptr,
                   "In seconds, 0 once the timer has run out; nil without a timer."),
    Game::Doc::Opt("duration", "number", nullptr, "In seconds; nil without a timer."),
    Game::Doc::Req("lockoutSchool", "number",
                   "The locked school mask for a school interrupt, else 0."),
    Game::Doc::Req("priority", "number", "By locType: 5 for the full-screen types, 1 to 4 for the others."),
    Game::Doc::Req("displayType", "number", "0 not shown, 1 alert, 2 full screen; by locType."),
    Game::Doc::Opt("auraInstanceID", "number", nullptr,
                   "The debuff's aura instance ID; nil for a school interrupt."),
};
const Game::Doc::Structure kLossOfControlData{"LossOfControlData", "LossOfControl", kDataFields,
                                              "One loss-of-control effect on the player."};

const Game::Doc::Field kDataArgs[] = {
    Game::Doc::Req("index", "luaIndex"),
};
const Game::Doc::Field kDataRets[] = {
    Game::Doc::Opt("event", "LossOfControlData", nullptr, "Nil for an index out of range."),
};
const Game::Doc::Function kGetActiveLossOfControlData{
    "Returns one loss-of-control effect on the player.", kDataArgs, kDataRets};

const Game::Doc::Field kUnitIndexArgs[] = {
    Game::Doc::Req("unitToken", "UnitToken"),
    Game::Doc::Req("index", "luaIndex"),
};
const Game::Doc::Function kGetActiveLossOfControlDataByUnit{
    "Returns one loss-of-control effect on a unit; nil for any unit but the player.",
    kUnitIndexArgs, kDataRets};

const Game::Doc::Field kDurationRets[] = {
    Game::Doc::Opt("duration", "LuaDurationObject", nullptr, "Nil for an index out of range."),
};
const Game::Doc::Function kGetActiveLossOfControlDuration{
    "Returns the timer of one loss-of-control effect on a unit as a duration object.",
    kUnitIndexArgs, kDurationRets};

const Game::Doc::Field kLockoutArgs[] = {
    Game::Doc::Opt("filterMask", "number", "0",
                   "Only these schools, as 1 << schoolIndex bits; 0 means every school."),
};
const Game::Doc::Field kLockoutRets[] = {
    Game::Doc::Req("lockedMask", "number",
                   "Every locked school as 1 << schoolIndex bits; 0 when none is locked."),
    Game::Doc::Opt("secondsRemaining", "number", nullptr,
                   "Until the last of those schools is clear; nil when none is locked."),
};
const Game::Doc::Function kGetSchoolLockout{
    "Returns which spell schools an interrupt has locked and for how long.", kLockoutArgs,
    kLockoutRets, nullptr, true};

// Constants.LossOfControlConsts (3.80.2: FUN_140d215f0).
const Game::Lua::EnumIntegerEntry kLossOfControlConsts[] = {
    {"LOSS_OF_CONTROL_ACTIVE_INDEX", 1},
};

} // namespace

bool LockoutForSpell(int spellID, uint32_t *startMs, uint32_t *endMs) {
    const uint8_t *rec = Spell::Lookup::RecordForID(spellID);
    if (rec == nullptr)
        return false;
    const int school = SpellSchoolIndex(rec);
    const int prevention = Game::Read<int>(rec, Offsets::OFF_SPELL_RECORD_PREVENTION_TYPE);

    // 3.80.2's walk (FUN_14197f180): skip what has run out; a blocking effect
    // with no timer answers at once, as now with no duration; else the
    // blocking effect that ends last.
    const uint32_t now = NowMs();
    bool found = false;
    for (const Entry &e : g_list) {
        if (e.endMs != 0 && Reached(now, e.endMs))
            continue;
        if (!Blocks(e, rec, school, prevention))
            continue;
        if (e.endMs == 0) {
            *startMs = now;
            *endMs = now;
            return true;
        }
        if (!found || !Reached(*endMs, e.endMs)) {
            // A duration 1.12 did not give leaves the start unknown (0).
            *startMs = e.durationMs != 0 ? e.endMs - e.durationMs : 0;
            *endMs = e.endMs;
            found = true;
        }
    }
    return found;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_LossOfControl", "GetActiveLossOfControlDataCount",
                                     &Script_GetActiveLossOfControlDataCount,
                                     &kGetActiveLossOfControlDataCount);
    Game::Lua::RegisterTableFunction("C_LossOfControl", "GetActiveLossOfControlDataCountByUnit",
                                     &Script_GetActiveLossOfControlDataCountByUnit,
                                     &kGetActiveLossOfControlDataCountByUnit);
    Game::Lua::RegisterTableFunction("C_LossOfControl", "GetActiveLossOfControlData",
                                     &Script_GetActiveLossOfControlData,
                                     &kGetActiveLossOfControlData);
    Game::Lua::RegisterTableFunction("C_LossOfControl", "GetActiveLossOfControlDataByUnit",
                                     &Script_GetActiveLossOfControlDataByUnit,
                                     &kGetActiveLossOfControlDataByUnit);
    Game::Lua::RegisterTableFunction("C_LossOfControl", "GetActiveLossOfControlDuration",
                                     &Script_GetActiveLossOfControlDuration,
                                     &kGetActiveLossOfControlDuration);
    Game::Lua::RegisterTableFunction("C_LossOfControl", "GetSchoolLockout",
                                     &Script_GetSchoolLockout, &kGetSchoolLockout);
    Game::Lua::RegisterConstants("LossOfControlConsts", kLossOfControlConsts, "LossOfControl");
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace LossOfControl
