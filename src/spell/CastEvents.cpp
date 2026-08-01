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

#include "CastEvents.h"

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "net/PacketReader.h"
#include "net/SendObserver.h"
#include "spell/Lookup.h"
#include "unit/Identity.h"

#include <cstdio>
#include <cstdint>
#include <ctime>

namespace Spell::CastEvents {

namespace {

// Spell.dbc localized string arrays (9 locale slots, 4 bytes each).
constexpr int OFF_NAME = 0x1E0; // Name[9]
constexpr int OFF_RANK = 0x204; // Rank[9]

constexpr const char *kStart = "UNIT_SPELLCAST_START";
constexpr const char *kStop = "UNIT_SPELLCAST_STOP";
constexpr const char *kDelayed = "UNIT_SPELLCAST_DELAYED";
constexpr const char *kChannelStart = "UNIT_SPELLCAST_CHANNEL_START";
constexpr const char *kChannelStop = "UNIT_SPELLCAST_CHANNEL_STOP";
constexpr const char *kChannelUpdate = "UNIT_SPELLCAST_CHANNEL_UPDATE";
constexpr const char *kSucceeded = "UNIT_SPELLCAST_SUCCEEDED";
constexpr const char *kInterrupted = "UNIT_SPELLCAST_INTERRUPTED";
constexpr const char *kFailed = "UNIT_SPELLCAST_FAILED";
constexpr const char *kFailedQuiet = "UNIT_SPELLCAST_FAILED_QUIET";
constexpr const char *kSent = "UNIT_SPELLCAST_SENT";
constexpr const char *kReticleTarget = "UNIT_SPELLCAST_RETICLE_TARGET";
constexpr const char *kReticleClear = "UNIT_SPELLCAST_RETICLE_CLEAR";

const Event::Custom::AutoReserve _rStart{kStart};
const Event::Custom::AutoReserve _rStop{kStop};
const Event::Custom::AutoReserve _rDelayed{kDelayed};
const Event::Custom::AutoReserve _rChannelStart{kChannelStart};
const Event::Custom::AutoReserve _rChannelStop{kChannelStop};
const Event::Custom::AutoReserve _rChannelUpdate{kChannelUpdate};
const Event::Custom::AutoReserve _rSucceeded{kSucceeded};
const Event::Custom::AutoReserve _rInterrupted{kInterrupted};
const Event::Custom::AutoReserve _rFailed{kFailed};
const Event::Custom::AutoReserve _rFailedQuiet{kFailedQuiet};
const Event::Custom::AutoReserve _rSent{kSent};
const Event::Custom::AutoReserve _rReticleTarget{kReticleTarget};
const Event::Custom::AutoReserve _rReticleClear{kReticleClear};

// Autoshot spams client-side failures while ramping — nampower filters it
// out of its failure event, so do we.
constexpr uint32_t kAutoShotSpellId = 75;

// SpellCastTargets mask bit for a unit target (a packed GUID follows).
constexpr uint16_t kTargetFlagUnit = 0x0002;

const char *LocalizedField(const uint8_t *rec, int fieldOffset) {
    const int locale = *reinterpret_cast<int *>(Offsets::VAR_LOCALE_INDEX);
    return *reinterpret_cast<const char *const *>(rec + fieldOffset + locale * 4);
}

// Cast UID for a REAL (Type-3) cast — player and remote alike. Per the retail
// spell-cast-GUID spec (warcraft.wiki.gg/wiki/GUID#Cast) for non-local-only
// casts: the low 23 bits are the cast's UNIX-epoch second modulo 2^23, and the
// higher bits are a counter incrementing for casts observed within the same
// second (Creature-GUID-like). Type-2 casts — local-only FAILED casts, "failed
// due to requirements not being met" — use a plain incrementing integer
// instead (`g_guidCounter`), matching the spec's local-cast rule.
int MakeCastUID() {
    const uint32_t nowSec = static_cast<uint32_t>(std::time(nullptr));
    static uint32_t s_lastSec = 0;
    static uint32_t s_perSec = 0;
    if (nowSec == s_lastSec)
        ++s_perSec;
    else {
        s_lastSec = nowSec;
        s_perSec = 0;
    }
    return static_cast<int>((s_perSec << 23) | (nowSec & 0x7FFFFFu));
}

// Modern-shaped castGUID:
// `Cast-<type>-<serverID>-<instanceID>-<zoneUID>-<spellID>-<castUID>`.
// Vanilla can't know server/instance/zone, so those three fields are 0. `type`
// is 3 for real casts (active abilities, channels — the common case) and 2 for
// local-only FAILED casts, per the spec's type table. The load-bearing parts
// are the spellID (field 6, which addons `strsplit("-")` out) and the unique
// per-cast castUID (field 7). Same (type, spellID, castUID) for every event of
// one cast → identical string, so START pairs with STOP.
void BuildCastGuid(char *out, size_t n, int type, int spellID, int castUID) {
    std::snprintf(out, n, "Cast-%d-0-0-0-%d-%010X", type, spellID,
                  static_cast<unsigned>(castUID));
}

// Fire a standard-shape event `(unitTarget, castGUID, spellID, spellName,
// rank)`. Gated on a listener so an unwatched event does no DBC lookups or
// string formatting. A null name/rank pushes `nil` through the dispatcher's
// `lua_pushstring(NULL) → pushnil` tail-jump, which is fine.
void Fire(const char *eventName, int spellID, int guidNum, int type = 3) {
    const int slot = Event::Custom::Lookup(eventName);
    if (!Event::Custom::HasListeners(slot))
        return;
    const uint8_t *rec = Spell::Lookup::RecordForID(spellID);
    if (rec == nullptr)
        return;
    char castGuid[48];
    BuildCastGuid(castGuid, sizeof(castGuid), type, spellID, guidNum);
    Event::Custom::Fire(slot, "%s%s%d%s%s", "player", castGuid, spellID,
                        LocalizedField(rec, OFF_NAME),
                        LocalizedField(rec, OFF_RANK));
}

// SENT is the one event whose shape differs from the rest, matching modern:
// `(unitTarget, target, castGUID, spellID, spellName, rank)` — the target is
// inserted at arg2. `target` is a unit token (e.g. "target") resolved from
// the cast packet, or "" for self / no-target casts.
void FireSent(int spellID, int guidNum, const char *target) {
    const int slot = Event::Custom::Lookup(kSent);
    if (!Event::Custom::HasListeners(slot))
        return;
    const uint8_t *rec = Spell::Lookup::RecordForID(spellID);
    if (rec == nullptr)
        return;
    char castGuid[48];
    BuildCastGuid(castGuid, sizeof(castGuid), /*type=*/3, spellID, guidNum);
    Event::Custom::Fire(slot, "%s%s%s%d%s%s", "player", target ? target : "",
                        castGuid, spellID, LocalizedField(rec, OFF_NAME),
                        LocalizedField(rec, OFF_RANK));
}

// Fire a RETICLE event `(unitTarget, castGUID, spellID, spellName, rank)`.
// There's no cast yet, so the castGUID slot is empty — retail pushes `nil`
// there, but the engine's event dispatcher can't emit a nil mid-format (a
// NULL `%s` corrupts every arg after it — the nil trick only survives as the
// LAST arg), so we push `""` instead. `unit` (arg1) and `spellID` (arg3) —
// the load-bearing fields — are exact; only arg2 differs (`""` vs `nil`),
// which is inconsequential for a reticle (no cast to identify). spellName /
// rank are our usual extension tail.
void FireReticle(const char *eventName, int spellID) {
    const int slot = Event::Custom::Lookup(eventName);
    if (!Event::Custom::HasListeners(slot))
        return;
    const uint8_t *rec = Spell::Lookup::RecordForID(spellID);
    if (rec == nullptr)
        return;
    Event::Custom::Fire(slot, "%s%s%d%s%s", "player", "", spellID,
                        LocalizedField(rec, OFF_NAME),
                        LocalizedField(rec, OFF_RANK));
}

// spellID whose reticle we last reported active (0 = no reticle up).
int s_reticleSpell = 0;
// Set when the reticle spell is actually cast (a CMSG_CAST_SPELL for it went
// out) — so the reticle is clearing by PLACEMENT, and RETICLE_CLEAR is
// suppressed (retail fires CLEAR only on a real cancel).
bool s_reticlePlaced = false;

// --- Previous-frame snapshot of the player's cast/channel --------------

// Incrementing integer for Type-2 (local-only FAILED) cast UIDs only. Real
// casts get a time-based UID from `MakeCastUID`.
int g_guidCounter = 0;

int g_castSpell = 0;   // 0 = not casting
int g_castStart = 0;   // startMs of the tracked cast (detects same-spell recast)
int g_castGuid = 0;    // castGUID counter value for the tracked cast
int g_castDelay = 0;   // last-seen accumulated pushback (ms)
bool g_castSucceeded = false; // SPELL_GO seen for the tracked cast this cast

int g_chanSpell = 0;
int g_chanGuid = 0;

// castGUID minted at SENT, pending until the matching downstream event
// (START / SUCCEEDED / FAILED for the same spell) consumes it — so all of
// a cast's events share one guid. 0 = none pending.
int g_pendingGuid = 0;
int g_pendingSpell = 0;

// The castGUID UID for a real (Type-3) cast of `spellID`: reuse the SENT-
// minted pending one when it matches (and consume it), else mint a fresh
// time-based UID. Casts with no observed SENT (some engine-internal paths)
// just get a fresh one.
int NextCastGuid(int spellID) {
    if (g_pendingGuid != 0 && g_pendingSpell == spellID) {
        const int g = g_pendingGuid;
        g_pendingGuid = 0;
        g_pendingSpell = 0;
        return g;
    }
    return MakeCastUID();
}

// Last cast that ended (fired STOP) + when, so SpellFailed_h can tell a
// mid-cast interruption (a started cast that's now aborting — the poll owns
// its INTERRUPTED) from a genuine pre-cast failure. The window covers the
// race where the poll's cast-end runs a frame before the engine's
// Spell_C_SpellFailed for the same cancel.
int g_endedSpell = 0;
int g_endedTMs = 0;
int g_endedGuid = 0;
constexpr int kEndWindowMs = 1000;

// SpellCastResult codes (nampower's game::SpellCastResult) that mean the
// cast is being INTERRUPTED rather than failing to start. Movement is the
// common one: while you hold the cast key and keep moving, the client
// retries and hits Spell_C_SpellFailed(MOVING) each time — modern surfaces
// each as a repeated UNIT_SPELLCAST_INTERRUPTED reusing the cast's guid.
constexpr int kSpellFailedInterrupted = 35;       // 0x23
constexpr int kSpellFailedInterruptedCombat = 36; // 0x24
constexpr int kSpellFailedMoving = 46;            // 0x2E

bool IsInterruptResult(int code) {
    return code == kSpellFailedInterrupted ||
           code == kSpellFailedInterruptedCombat || code == kSpellFailedMoving;
}

// SpellCastResult codes that modern surfaces as UNIT_SPELLCAST_FAILED_QUIET (a
// suppressed, no-error-text failure) rather than FAILED. This is a fixed
// whitelist, not a "was an error shown" heuristic: verified against the 3.3.5
// client's unit-spellcast event dispatch (FUN_007fecc0 selects the QUIET event
// for exactly CHARMED / DONT_REPORT / SPELL_IN_PROGRESS), then mapped to 1.12's
// SpellCastResult enum (FUN_006e23e0):
//   CHARMED (0x14) · DONT_REPORT (0x17) · SPELL_IN_PROGRESS (0x61)
// SPELL_IN_PROGRESS is the common one — casting while already casting, or a
// nampower spell-queue rejection.
constexpr int kSpellFailedCharmed = 20;         // 0x14
constexpr int kSpellFailedSpellInProgress = 97; // 0x61

bool IsQuietResult(int code) {
    return code == kSpellFailedCharmed ||
           code == Offsets::SPELL_FAILED_DONT_REPORT ||
           code == kSpellFailedSpellInProgress;
}

// A channel's SUCCEEDED is deferred so it fires AFTER CHANNEL_START, matching
// modern's order (SENT → CHANNEL_START → SUCCEEDED → CHANNEL_STOP). SPELL_GO
// (OnPlayerSucceeded) lands a poll before the channel is detected, so instead
// of firing SUCCEEDED there it stashes the channel spell here; PollPlayer
// fires it with the channel's guid immediately after CHANNEL_START. The window
// is a loss guard — if no CHANNEL_START ever claims it, the poll fires it
// standalone rather than dropping it.
int g_pendingChanSuccSpell = 0;
int g_pendingChanSuccTMs = 0;
constexpr int kChanSuccDeferMs = 500;

// Spell.dbc AttributesEx channel bits — same test Spell::Cast uses.
constexpr int OFF_ATTRIBUTES_EX = 0x1C;
constexpr uint32_t SPELL_ATTR_EX_CHANNELED = 0x4 | 0x40; // IS_CHANNELED | SELF

bool IsChanneledSpell(int spellID) {
    const uint8_t *rec = Spell::Lookup::RecordForID(spellID);
    return rec != nullptr &&
           (*reinterpret_cast<const uint32_t *>(rec + OFF_ATTRIBUTES_EX) &
            SPELL_ATTR_EX_CHANNELED) != 0;
}

int NowMs() {
    using TickMs_t = uint32_t(__fastcall *)();
    return static_cast<int>(reinterpret_cast<TickMs_t>(
        static_cast<uintptr_t>(Offsets::FUN_OS_TICKCOUNT_MS))());
}

// ---- UNIT_SPELLCAST_FAILED (client-side cast rejection) ----------------
// Co-hook `Spell_C_SpellFailed` — the local player's own cast failures
// (out of range, no mana, not-ready, LoS, …). Recipe (hook point +
// filters) from nampower's Spell_C_SpellFailedHook. We don't hook the
// SMSG failure PACKET handlers here (Spell::Cast owns those for its cache
// eviction); this is the distinct client-side entry.
using SpellFailed_t = void(__fastcall *)(uint32_t spellId, int result,
                                         int unk1, int unk2, int failedByServer);
SpellFailed_t g_origSpellFailed = nullptr;

void __fastcall SpellFailed_h(uint32_t spellId, int result, int unk1, int unk2,
                              int failedByServer) {
    g_origSpellFailed(spellId, result, unk1, unk2, failedByServer);
    if (spellId == kAutoShotSpellId)
        return; // autoshot ramp spams client failures — filter (like nampower)
    const int sid = static_cast<int>(spellId);
    // A "quiet" failure the engine shows NO error text for (already casting,
    // charmed, spammy retries, reticle cancels, Unleashed-Potential-style fake
    // fails, …). Modern surfaces those as UNIT_SPELLCAST_FAILED_QUIET rather
    // than FAILED — see IsQuietResult for the verified 3-code whitelist. Same
    // type-2 (local-only) castGUID as FAILED.
    if (IsQuietResult(result & 0xFF)) {
        Fire(kFailedQuiet, sid, ++g_guidCounter, /*type=*/2);
        return;
    }
    const int now = NowMs();
    const bool recentCast =
        g_endedSpell == sid && now - g_endedTMs < kEndWindowMs;

    // Interrupt-class result (moving / kicked): this is the cast being
    // INTERRUPTED, not failing to start. Modern re-fires INTERRUPTED for
    // each such retry while you hold the cast key + keep moving, all reusing
    // the interrupted cast's castGUID. Match that: fire INTERRUPTED with the
    // in-progress guid, or the just-ended cast's guid, or a fresh one.
    if (IsInterruptResult(result & 0xFF)) {
        int guid = g_castSpell == sid ? g_castGuid
                   : recentCast       ? g_endedGuid
                                      : NextCastGuid(sid);
        Fire(kInterrupted, sid, guid);
        return;
    }

    // Non-interrupt failure. If it's for a cast that was started (in
    // progress, or just ended), that cast's end is already reported by the
    // poll's INTERRUPTED — don't also fire FAILED. Only a failure with NO
    // started cast is a genuine pre-cast rejection (out of range, no mana,
    // on cooldown) → UNIT_SPELLCAST_FAILED. This is the spec's Type 2: a
    // local-only cast that never reached the server, so it gets a plain
    // incrementing-integer UID (not the time-based real-cast UID).
    if (g_castSpell == sid || recentCast)
        return;
    Fire(kFailed, sid, ++g_guidCounter, /*type=*/2);
}

const Game::HookAutoRegister _failedHook{
    Offsets::FUN_SPELL_C_SPELL_FAILED,
    reinterpret_cast<void *>(&SpellFailed_h),
    reinterpret_cast<void **>(&g_origSpellFailed)};

// ---- UNIT_SPELLCAST_SENT (CMSG_CAST_SPELL leaving the client) ----------
// Watch outgoing sends via the shared observer (no funnel hook of our own).
// This is the earliest point in a cast's life: mint the castGUID here and
// stash it pending so the matching START / SUCCEEDED / FAILED reuse it.
void OnSend(uint32_t opcode, Net::CDataStore *packet) {
    if (opcode != Offsets::OP_CMSG_CAST_SPELL)
        return;
    const int spellID = static_cast<int>(Net::Read<uint32_t>(packet));
    if (spellID == 0)
        return;
    // If this is the spell whose reticle is up, the player just PLACED it —
    // the reticle will clear by placement, not cancel, so suppress its
    // RETICLE_CLEAR (retail fires CLEAR only on a real cancel).
    if (s_reticleSpell != 0 && spellID == s_reticleSpell)
        s_reticlePlaced = true;
    // CMSG_CAST_SPELL body after spellId is SpellCastTargets: targetMask
    // (u16), then a packed unit GUID when the UNIT flag is set (self /
    // no-target casts carry none). Resolve that GUID to a unit token for
    // SENT's `target` arg. Layout confirmed against the server's
    // SpellCastTargets::read (tortoise-wow).
    char target[64];
    target[0] = '\0';
    const uint16_t mask = Net::Read<uint16_t>(packet);
    if (mask & kTargetFlagUnit) {
        const uint64_t targetGuid = Net::ReadPackedGuid(packet);
        if (targetGuid != 0)
            Unit::Identity::TokenFromGUID(targetGuid, target, sizeof(target));
    }
    const int guid = MakeCastUID(); // real cast (Type 3) → time-based UID
    g_pendingGuid = guid;
    g_pendingSpell = spellID;
    FireSent(spellID, guid, target);
}

const Net::SendObserver::AutoSubscribe _sendSub{&OnSend};

// ---- Remote (non-player) unit cast events ------------------------------

// Fire `eventName` for EVERY unit token currently mapping to `casterGuid`.
// The listener gate + DBC name/rank lookup + castGUID build happen once, then
// the payload is fanned out per token (`target` / `focus` / `nameplateN` /
// `party` / `raid` / `mouseover`). Same `(unit, castGUID, spellID, spellName,
// rank)` shape as the player events, just with a non-"player" unit.
void FireRemote(const char *eventName, uint64_t casterGuid, int spellID,
                int guidNum) {
    const int slot = Event::Custom::Lookup(eventName);
    if (!Event::Custom::HasListeners(slot))
        return;
    const uint8_t *rec = Spell::Lookup::RecordForID(spellID);
    if (rec == nullptr)
        return;
    char tokens[16][32];
    const int n = Unit::Identity::TokensForGUID(casterGuid, tokens, 16);
    if (n == 0)
        return;
    char castGuid[48];
    BuildCastGuid(castGuid, sizeof(castGuid), /*type=*/3, spellID, guidNum);
    const char *name = LocalizedField(rec, OFF_NAME);
    const char *rank = LocalizedField(rec, OFF_RANK);
    for (int i = 0; i < n; ++i)
        Event::Custom::Fire(slot, "%s%s%d%s%s", tokens[i], castGuid, spellID,
                            name, rank);
}

// One tracked cast per remote caster (a unit can't cast two at once), fired
// from the poll so ordering (START -> SUCCEEDED -> STOP) is centralized.
struct RemoteEvt {
    uint64_t guid;
    int spellID;
    int guidNum;
    int endMs;
    bool isChannel;
    bool pendingStart;  // START/CHANNEL_START not yet fired
    bool startFired;
    bool succeeded;     // SPELL_GO seen -> completion, not interrupt
    bool succeededFired;
    bool aborted;       // failure packet / ClearCastingSpell seen
    bool active;
};
constexpr int kRemoteEvtSlots = 64;
RemoteEvt g_remoteEvt[kRemoteEvtSlots];

RemoteEvt *FindRemoteEvt(uint64_t guid) {
    for (auto &e : g_remoteEvt)
        if (e.active && e.guid == guid)
            return &e;
    return nullptr;
}

RemoteEvt *AllocRemoteEvt(uint64_t guid) {
    if (RemoteEvt *existing = FindRemoteEvt(guid))
        return existing; // replace the caster's prior cast in place
    for (auto &e : g_remoteEvt)
        if (!e.active)
            return &e;
    return &g_remoteEvt[0]; // pool exhausted (64 casters) — reuse slot 0
}

} // namespace

void PollPlayer(int castSpellID, int castStartMs, int castDelayMs,
                int channelSpellID, int channelStartMs) {
    // ---- Regular cast -------------------------------------------------
    // A "new cast" is a different spell OR the same spell re-stamped at a
    // new start time (a chained same-spell recast — the engine bails
    // Spell_C_CastSpell on `spellID == current`, so only the fresh startMs
    // distinguishes it). Fire STOP for the outgoing cast first, then START.
    const bool newCast = castSpellID != 0 &&
                         (castSpellID != g_castSpell || castStartMs != g_castStart);
    if (g_castSpell != 0 && (castSpellID == 0 || newCast)) {
        // A cast that ended without a SPELL_GO for it was interrupted /
        // cancelled — fire INTERRUPTED before STOP. By this tick, a natural
        // completion's SPELL_GO (and thus OnPlayerSucceeded) has already run
        // a frame earlier, so the flag reliably tells the two apart even
        // though the engine routes both through the same abort choke point.
        if (!g_castSucceeded)
            Fire(kInterrupted, g_castSpell, g_castGuid);
        Fire(kStop, g_castSpell, g_castGuid);
        g_endedSpell = g_castSpell; // for SpellFailed_h's interrupt-vs-fail gate
        g_endedGuid = g_castGuid;   // repeated movement-interrupts reuse it
        g_endedTMs = NowMs();
        g_castSpell = 0;
    }
    if (newCast) {
        g_castGuid = NextCastGuid(castSpellID);
        g_castSpell = castSpellID;
        g_castStart = castStartMs;
        g_castDelay = castDelayMs;
        g_castSucceeded = false;
        Fire(kStart, castSpellID, g_castGuid);
    } else if (g_castSpell != 0 && castDelayMs > g_castDelay) {
        // Pushback grew (SMSG_SPELL_DELAYED extended the cast).
        g_castDelay = castDelayMs;
        Fire(kDelayed, g_castSpell, g_castGuid);
    }

    // ---- Channel ------------------------------------------------------
    // Detect a new channel by spellID ONLY (not startMs): a channel is
    // stamped twice at start — SMSG_SPELL_START, then MSG_CHANNEL_START
    // re-stamps with the server duration — which changes startMs but is the
    // SAME channel. A startMs check would spuriously fire STOP+START on the
    // re-stamp. A genuine re-channel of the same spell passes through
    // g_chanSpell==0 (CHANNEL_STOP) first, so spellID alone still catches it.
    (void)channelStartMs;
    const bool newChan = channelSpellID != 0 && channelSpellID != g_chanSpell;
    if (g_chanSpell != 0 && (channelSpellID == 0 || newChan)) {
        // Channels only ever fire CHANNEL_STOP — never INTERRUPTED — whether
        // they end naturally or are cut short (retail behavior, verified).
        Fire(kChannelStop, g_chanSpell, g_chanGuid);
        g_chanSpell = 0;
    }
    if (newChan) {
        // The initiating cast's SENT guid is still pending — a channel's
        // SUCCEEDED is deferred (OnPlayerSucceeded), so it hasn't consumed the
        // guid yet. NextCastGuid reuses it, keeping SENT / CHANNEL_START /
        // SUCCEEDED / CHANNEL_STOP on one castGUID.
        g_chanGuid = NextCastGuid(channelSpellID);
        g_chanSpell = channelSpellID;
        Fire(kChannelStart, channelSpellID, g_chanGuid);
        // Modern fires SUCCEEDED right after CHANNEL_START. SPELL_GO deferred
        // it to here so it lands after START, sharing the channel's guid.
        if (g_pendingChanSuccSpell == channelSpellID) {
            Fire(kSucceeded, channelSpellID, g_chanGuid);
            g_pendingChanSuccSpell = 0;
        }
    }

    // Loss guard: a deferred channel SUCCEEDED that no CHANNEL_START claimed
    // within the window (the channel never stamped) fires standalone rather
    // than being dropped.
    if (g_pendingChanSuccSpell != 0 &&
        NowMs() - g_pendingChanSuccTMs >= kChanSuccDeferMs) {
        Fire(kSucceeded, g_pendingChanSuccSpell,
             NextCastGuid(g_pendingChanSuccSpell));
        g_pendingChanSuccSpell = 0;
    }
}

void OnPlayerSucceeded(int spellID) {
    if (spellID == 0)
        return;
    // Channels fire SUCCEEDED *after* CHANNEL_START (modern order). This
    // SPELL_GO lands a poll before the channel is detected, so defer: stash
    // the spell and let PollPlayer fire SUCCEEDED right after it fires
    // CHANNEL_START, sharing the channel's guid.
    if (IsChanneledSpell(spellID)) {
        g_pendingChanSuccSpell = spellID;
        g_pendingChanSuccTMs = NowMs();
        return;
    }
    // Non-channel: fire now. Pair with the tracked cast guid when it's the
    // same spell (cast-time — START already minted the guid); otherwise an
    // instant with no tracked cast, so mint a fresh one (pairs with its SENT).
    int guid;
    if (g_castSpell == spellID) {
        guid = g_castGuid;
        g_castSucceeded = true; // marks this cast a completion, not an interrupt
    } else {
        guid = NextCastGuid(spellID); // instant — pairs with its SENT
    }
    Fire(kSucceeded, spellID, guid);
}

void PollReticle() {
    const bool active =
        *reinterpret_cast<const int *>(Offsets::VAR_SPELL_TARGETING_FLAGS) != 0;
    if (active) {
        if (s_reticleSpell == 0) {
            // Reticle just came up — report the pending spell. Read the spellID
            // now (it's set by Spell_C_CastSpell before targeting begins); if
            // it's transiently 0 this frame, retry next tick.
            const int spellID =
                *reinterpret_cast<const int *>(Offsets::VAR_PENDING_CAST_SPELL);
            if (spellID != 0) {
                s_reticleSpell = spellID;
                s_reticlePlaced = false;
                FireReticle(kReticleTarget, spellID);
            }
        }
    } else if (s_reticleSpell != 0) {
        // Reticle cleared. Fire CLEAR only if the spell wasn't cast — i.e. the
        // player CANCELLED (Esc / right-click). A placement (CMSG_CAST_SPELL
        // went out, s_reticlePlaced set by OnSend) clears the reticle too but
        // is not a CLEAR event in retail.
        if (!s_reticlePlaced)
            FireReticle(kReticleClear, s_reticleSpell);
        s_reticleSpell = 0;
        s_reticlePlaced = false;
    }
}

void OnPlayerChannelUpdate(int spellID) {
    // Only the tracked, already-started channel gets an UPDATE, reusing its
    // guid so CHANNEL_START / UPDATE / STOP share one castGUID. A pushback
    // arrives seconds into a channel — long after the poll fired
    // CHANNEL_START — so g_chanSpell / g_chanGuid are set by now; if somehow
    // not, skip (an UPDATE never precedes its START).
    if (spellID == 0 || g_chanSpell != spellID)
        return;
    Fire(kChannelUpdate, spellID, g_chanGuid);
}

// ---- Remote unit entry points ------------------------------------------

void OnRemoteCastStart(uint64_t casterGuid, int spellID, int endMs,
                       bool isChannel) {
    if (casterGuid == 0 || spellID == 0)
        return;
    RemoteEvt *e = AllocRemoteEvt(casterGuid);
    *e = RemoteEvt{};
    e->guid = casterGuid;
    e->spellID = spellID;
    e->guidNum = MakeCastUID();
    e->endMs = endMs;
    e->isChannel = isChannel;
    e->pendingStart = true;
    e->active = true;
}

void OnRemoteSucceeded(uint64_t casterGuid, int spellID) {
    if (casterGuid == 0 || spellID == 0)
        return;
    RemoteEvt *e = FindRemoteEvt(casterGuid);
    if (e != nullptr && e->spellID == spellID) {
        e->succeeded = true; // poll fires SUCCEEDED (then STOP) in order
        return;
    }
    // No tracked cast — an instant (no SMSG_SPELL_START). Nothing precedes it,
    // so fire SUCCEEDED now with a fresh (server-time-based) remote UID.
    FireRemote(kSucceeded, casterGuid, spellID, MakeCastUID());
}

bool CurrentCastGuid(uint64_t casterGuid, int spellID, char *out,
                     size_t outSize) {
    if (casterGuid == 0 || spellID == 0 || out == nullptr)
        return false;
    int uid;
    if (casterGuid == Unit::Identity::PlayerGuid()) {
        // Player's own cast — the guid PollPlayer minted at START (threaded
        // from SENT). Channels are excluded (g_chanGuid) — UnitCastingInfo is
        // cast-only.
        if (g_castSpell != spellID)
            return false;
        uid = g_castGuid;
    } else {
        // Remote caster — its live event entry (a channel entry is skipped).
        const RemoteEvt *e = FindRemoteEvt(casterGuid);
        if (e == nullptr || e->spellID != spellID || e->isChannel)
            return false;
        uid = e->guidNum;
    }
    BuildCastGuid(out, outSize, /*type=*/3, spellID, uid);
    return true;
}

void OnRemoteAborted(uint64_t casterGuid, int spellID) {
    if (casterGuid == 0 || spellID == 0)
        return;
    RemoteEvt *e = FindRemoteEvt(casterGuid);
    if (e != nullptr && e->spellID == spellID)
        e->aborted = true; // poll fires INTERRUPTED + STOP next frame
}

void PollRemote() {
    const int now = NowMs();
    for (auto &e : g_remoteEvt) {
        if (!e.active)
            continue;

        // 1. START / CHANNEL_START.
        if (e.pendingStart) {
            FireRemote(e.isChannel ? kChannelStart : kStart, e.guid, e.spellID,
                       e.guidNum);
            e.pendingStart = false;
            e.startFired = true;
        }

        // 2. SUCCEEDED (SPELL_GO seen). A regular cast completes here (STOP
        //    follows immediately); a channel keeps running to its endMs.
        if (e.succeeded && !e.succeededFired) {
            FireRemote(kSucceeded, e.guid, e.spellID, e.guidNum);
            e.succeededFired = true;
            if (!e.isChannel) {
                FireRemote(kStop, e.guid, e.spellID, e.guidNum);
                e.active = false;
                continue;
            }
            // A channel's SPELL_GO (which just set `succeeded`) also drives the
            // engine's cast→channel `ClearCastingSpell` in the same packet
            // batch — our abort hook sees that. It's the channel STARTING, not
            // an interrupt, so drop the same-batch abort; otherwise the channel
            // is killed at its own start. A genuine interrupt lands on a later
            // poll, when `succeededFired` is already set, so it's still honored.
            e.aborted = false;
        }

        // 3. Abort (kick / LoS / death / movement). A cut-short CAST fires
        //    INTERRUPTED then STOP; a CHANNEL fires only CHANNEL_STOP (retail
        //    never emits INTERRUPTED for channels, ended early or not — so the
        //    caster and observer match).
        if (e.aborted) {
            if (e.startFired && !e.isChannel)
                FireRemote(kInterrupted, e.guid, e.spellID, e.guidNum);
            FireRemote(e.isChannel ? kChannelStop : kStop, e.guid, e.spellID,
                       e.guidNum);
            e.active = false;
            continue;
        }

        // 4. Natural end at the computed time — the STOP for a channel, and
        //    the backstop for a cast whose SPELL_GO we never saw.
        if (e.endMs != 0 && now - e.endMs >= 0) {
            FireRemote(e.isChannel ? kChannelStop : kStop, e.guid, e.spellID,
                       e.guidNum);
            e.active = false;
        }
    }
}

} // namespace Spell::CastEvents
