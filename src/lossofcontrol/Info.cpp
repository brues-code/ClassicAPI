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

// `C_LossOfControl` — active loss-of-control effects on the LOCAL PLAYER.
// Vanilla 1.12 has no such aggregation layer (it's a MoP addition), so we
// synthesize it from two sources:
//
//   1. SCHOOL_INTERRUPT — the Counterspell/Kick school lockout. The server
//      (Player::ProhibitSpellSchool) delivers it as a single
//      SMSG_SPELL_COOLDOWN carrying every spell of the interrupted school,
//      all with the same duration. That "player-GUID packet, >=2 spells, one
//      uniform duration" shape is produced by nothing else (normal cooldowns
//      are single-spell or applied client-side), so co-hooking the cooldown
//      handler and matching it yields an authoritative school + duration.
//      The interrupting spell isn't in the packet, so spellID is 0 and
//      displayText is a fixed "Interrupted" — the mechanically-relevant data
//      (school, duration, timing) is exact.
//
//   2. CC debuffs — stun / fear / root / confuse / charm / possess / silence /
//      pacify / disarm are real debuff auras. We scan the player's debuffs and
//      classify each by its spell's EffectApplyAuraName. Timing is best-effort
//      from the Aura::Source cache (nil when the cast wasn't observed — the
//      LoC timing fields are nullable, so that's contract-valid).
//
// Placeholders for fields with no vanilla equivalent: priority = 0,
// displayType = 2 (show for full duration), auraInstanceID = nil. iconTexture
// is the spell's icon PATH (vanilla has no FileDataIDs; a path is the
// functional equivalent).

#include "Game.h"
#include "Offsets.h"
#include "aura/Data.h"
#include "aura/Source.h"
#include "dbc/Lookup.h"
#include "event/Custom.h"
#include "net/PacketDispatch.h"
#include "net/PacketReader.h"
#include "spell/CrowdControl.h"
#include "spell/Lookup.h"
#include "tick/WorldTick.h"
#include "time/Clock.h"
#include "unit/Identity.h"

#include <cstdint>

namespace LossOfControl {

namespace {

using Time::Clock::NowMs;
using Time::Clock::Reached;

const char *SpellName(const uint8_t *rec) {
    const int locale = *reinterpret_cast<int *>(Offsets::VAR_LOCALE_INDEX);
    const char *n =
        *reinterpret_cast<const char *const *>(rec + Offsets::OFF_SPELL_NAMES + locale * 4);
    return (n != nullptr) ? n : "";
}

const char *SpellIconPath(const uint8_t *rec) {
    const int iconID = *reinterpret_cast<const int *>(rec + Offsets::OFF_SPELL_RECORD_ICON_ID);
    const uint8_t *iconRec = DBC::Record(Offsets::VAR_SPELL_ICON_RECORDS,
                                         Offsets::VAR_SPELL_ICON_COUNT,
                                         static_cast<uint32_t>(iconID));
    if (iconRec == nullptr)
        return "";
    const char *p = *reinterpret_cast<const char *const *>(iconRec + 0x04);
    return (p != nullptr) ? p : "";
}

int SpellSchoolIndex(const uint8_t *rec) {
    return *reinterpret_cast<const int *>(rec + Offsets::OFF_SPELL_SCHOOL);
}

// The control-loss classifier lives in `Spell::CrowdControl::Classify` so the
// `C_UnitAuras` CROWD_CONTROL aura filter shares this hard-control set (that
// filter additionally counts snares, which are crowd control but not LoC).

// ---- School-interrupt lockout state (fed by SMSG_SPELL_COOLDOWN) -----------

// One active lockout per school index (0..6); active until `Reached(endMs)`.
// Self-expiring, per-process — a stale entry from before a relog is already
// in the past on the shared uptime clock, so no reset is needed. Unsigned,
// like every engine-tick store: the OS counter wraps at 2^32 ms and a signed
// `int` goes negative past 2^31 ms (~24.9 days uptime). See `Time::Clock`.
struct SchoolLock {
    uint32_t startMs;
    uint32_t endMs;
};
SchoolLock g_schoolLock[Offsets::SPELL_SCHOOL_COUNT] = {};

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
            const uint8_t *rec =
                Spell::Lookup::RecordForID(static_cast<int>(spellID));
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

// ---- Aggregated active-effect list ----------------------------------------

struct LocEntry {
    const char *locType;
    int spellID;        // 0 = unknown (school interrupt)
    const uint8_t *rec; // spell record for name/icon; null for school interrupt
    int schoolMask;     // lockoutSchool
    uint32_t startMs;   // 0 = unknown
    uint32_t endMs;     // 0 = unknown / no timing
};

int BuildList(LocEntry *out, int maxOut) {
    const uint32_t now = NowMs();
    int n = 0;

    // School-interrupt lockouts first.
    for (int s = 0; s < Offsets::SPELL_SCHOOL_COUNT && n < maxOut; ++s) {
        if (g_schoolLock[s].endMs != 0 && !Reached(now, g_schoolLock[s].endMs)) {
            LocEntry &e = out[n++];
            e.locType = "SCHOOL_INTERRUPT";
            e.spellID = 0;
            e.rec = nullptr;
            e.schoolMask = 1 << s;
            e.startMs = g_schoolLock[s].startMs;
            e.endMs = g_schoolLock[s].endMs;
        }
    }

    // CC debuffs on the player.
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player != nullptr) {
        const uint64_t playerGuid = Unit::Identity::PlayerGuid();
        for (int i = 1; n < maxOut; ++i) {
            const int slot = Aura::Data::FindNthSlot(
                player, i, Aura::Data::Filter::Harmful, Aura::Data::Match{});
            if (slot < 0)
                break;
            const uint32_t spellID = Aura::Data::ReadSpellID(player, slot);
            if (spellID == 0)
                continue;
            const uint8_t *rec =
                Spell::Lookup::RecordForID(static_cast<int>(spellID));
            if (rec == nullptr)
                continue;
            const char *locType = Spell::CrowdControl::Classify(rec);
            if (locType == nullptr)
                continue;

            LocEntry &e = out[n++];
            e.locType = locType;
            e.spellID = static_cast<int>(spellID);
            e.rec = rec;
            e.schoolMask = 0;
            e.startMs = 0;
            e.endMs = 0;
            uint64_t caster = 0;
            uint32_t expMs = 0, durMs = 0;
            if (Aura::Source::Get(playerGuid, spellID, slot, &caster, &expMs,
                                  &durMs) &&
                expMs != 0) {
                e.endMs = expMs;
                if (durMs != 0)
                    e.startMs = expMs - durMs;
            }
        }
    }
    return n;
}

// ---- LOSS_OF_CONTROL_ADDED / _UPDATE events (WorldTick poll-and-diff) ------
//
// Effect *expiry* (a school lockout timing out, a CC aura falling off) has no
// engine packet or callback, so a per-frame diff is the only complete signal.
// Firing from the WorldTick tail also keeps the Lua handlers out of the
// mid-packet contexts the state is updated in (the OnTooltipSetItem fire-safety
// lesson). The scan is bounded (16 debuff slots) and early-outs when idle, so
// the per-frame cost is negligible.

constexpr const char *kEventAdded = "LOSS_OF_CONTROL_ADDED";
constexpr const char *kEventUpdate = "LOSS_OF_CONTROL_UPDATE";

// Identity of an active effect for frame-to-frame diffing: a CC is keyed by
// spellID, a school interrupt by its school mask (spellID 0).
struct EffectKey {
    int spellID;
    int schoolMask;
};
EffectKey g_prev[32];
int g_prevCount = 0;

bool ContainsKey(const EffectKey *list, int count, EffectKey key) {
    for (int i = 0; i < count; ++i)
        if (list[i].spellID == key.spellID &&
            list[i].schoolMask == key.schoolMask)
            return true;
    return false;
}

void OnWorldTick() {
    LocEntry entries[32];
    const int n = BuildList(entries, 32);

    EffectKey cur[32];
    for (int i = 0; i < n; ++i)
        cur[i] = EffectKey{entries[i].spellID, entries[i].schoolMask};

    bool changed = false;
    for (int i = 0; i < n; ++i) {
        if (!ContainsKey(g_prev, g_prevCount, cur[i])) {
            // A newly-applied effect -> LOSS_OF_CONTROL_ADDED(eventIndex).
            Event::Custom::Fire(Event::Custom::Lookup(kEventAdded), "%d", i + 1);
            changed = true;
        }
    }
    for (int i = 0; i < g_prevCount && !changed; ++i) {
        if (!ContainsKey(cur, n, g_prev[i]))
            changed = true; // an effect ended (fell off / lockout expired)
    }
    // Any add or removal fires one coalesced "re-scan" signal. The payload is
    // the affected unit — always "player" here (vanilla only exposes the local
    // player's control-loss state), but passing it keeps the event shape
    // forward-compatible with per-unit tracking and matches modern WoW, whose
    // LOSS_OF_CONTROL_UPDATE carries the unit token.
    if (changed)
        Event::Custom::Fire(Event::Custom::Lookup(kEventUpdate), "%s", "player");

    for (int i = 0; i < n; ++i)
        g_prev[i] = cur[i];
    g_prevCount = n;
}

static const Event::Custom::AutoReserve _reserveAdded{kEventAdded};
static const Event::Custom::AutoReserve _reserveUpdate{kEventUpdate};
static const Tick::WorldTick::AutoSubscribe _tick{&OnWorldTick};

// `C_LossOfControl.GetActiveLossOfControlDataCount()` -> number.
int __fastcall Script_GetActiveLossOfControlDataCount(void *L) {
    LocEntry entries[32];
    const int n = BuildList(entries, 32);
    Game::Lua::PushNumber(L, static_cast<double>(n));
    return 1;
}

// `C_LossOfControl.GetActiveLossOfControlData(index)` -> LossOfControlData, or
// nil if index is out of range.
int __fastcall Script_GetActiveLossOfControlData(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(
            L, "Usage: C_LossOfControl.GetActiveLossOfControlData(index)");
        return 0;
    }
    const int index = static_cast<int>(Game::Lua::ToNumber(L, 1));
    LocEntry entries[32];
    const int n = BuildList(entries, 32);
    if (index < 1 || index > n)
        return 0; // nil

    const LocEntry &e = entries[index - 1];
    const uint32_t now = NowMs();

    Game::Lua::NewTable(L);
    Game::Lua::SetFieldString(L, "locType", e.locType);
    Game::Lua::SetFieldNumber(L, "spellID", static_cast<double>(e.spellID));
    Game::Lua::SetFieldString(
        L, "displayText", (e.rec != nullptr) ? SpellName(e.rec) : "Interrupted");
    Game::Lua::SetFieldString(
        L, "iconTexture", (e.rec != nullptr) ? SpellIconPath(e.rec) : "");

    // Timing (seconds, GetTime() epoch). Nullable — leave a field unset (nil)
    // when we don't know it, per the modern contract.
    if (e.endMs != 0 && !Reached(now, e.endMs)) {
        Game::Lua::SetFieldNumber(L, "timeRemaining",
                                  static_cast<double>(e.endMs - now) * 0.001);
        if (e.startMs != 0) {
            Game::Lua::SetFieldNumber(L, "startTime",
                                      static_cast<double>(e.startMs) * 0.001);
            Game::Lua::SetFieldNumber(
                L, "duration", static_cast<double>(e.endMs - e.startMs) * 0.001);
        }
    }

    Game::Lua::SetFieldNumber(L, "lockoutSchool",
                              static_cast<double>(e.schoolMask));
    Game::Lua::SetFieldNumber(L, "priority", 0);
    Game::Lua::SetFieldNumber(L, "displayType", 2); // show for full duration
    // auraInstanceID: nil (vanilla has no aura instance IDs) — field omitted.
    return 1;
}

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction(
        "C_LossOfControl", "GetActiveLossOfControlDataCount",
        &Script_GetActiveLossOfControlDataCount);
    Game::Lua::RegisterTableFunction("C_LossOfControl",
                                     "GetActiveLossOfControlData",
                                     &Script_GetActiveLossOfControlData);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace LossOfControl
