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

// Cast / channel info — `UnitCastingInfo`, `CastingInfo`,
// `UnitChannelInfo`, `ChannelInfo`.
//
// Vanilla 1.12 stores NO cast start/end times anywhere readable (unlike
// 3.3.5+, which keeps them per-unit on the CGUnit). The cast bar is
// Lua-driven off `SPELLCAST_START(duration)` + `GetTime()`. So we
// self-track the LOCAL PLAYER's cast/channel with a HYBRID of two sources:
//   - Normal casts (client path): co-hook the engine's cast-start writer
//     `FUN_CAST_START_SET` and stamp from its spellID argument the instant
//     the cast begins (startMs = now, endMs = now + effective cast time) —
//     zero latency. `g_castFromServer = false`; cleared on `WorldTick` when
//     `VAR_CURRENT_CAST_SPELL` returns to 0 (normal end / cancel / interrupt).
//   - Chained same-spell recasts (server path): the engine SHORT-CIRCUITS
//     `Spell_C_CastSpell` at `spellID == VAR_CURRENT_CAST_SPELL` — it never
//     runs the client cast path, never sets the global, never fires
//     SPELLCAST_START. So a chained recast is invisible client-side; the
//     ONLY witness is the server's `SMSG_SPELL_START` (= nampower's
//     SPELL_START_SELF). We stamp `g_cast` from that packet (server cast
//     time), dedup'd against a just-made client stamp so a normal cast's
//     confirming packet doesn't restart its bar. `g_castFromServer = true`;
//     since the global was never set for these, the WorldTick VAR==0 clear
//     must skip them — they expire on their computed endMs, or earlier via
//     the abort co-hooks (ClearCastingSpell / the failure packets) when the
//     engine learns the cast died.
//   - channel: stamped from `SMSG_SPELL_START` at packet receipt (instant
//     channels; engine-computed duration) and from `MSG_CHANNEL_START`
//     (server duration — and the ONLY start signal for cast-then-channel
//     spells like Mind Control, whose SpellStart covers just the cast
//     phase). The broadcast +0x228 field is NOT polled for the player —
//     it lags the channel start by ~1 tick.
// `now` is the engine ms clock (`FUN_OS_TICKCOUNT_MS`), the same source
// Lua's `GetTime()` scales by 0.001 — so startMs/endMs are directly
// comparable to `GetTime()*1000` in addon progress math.
//
// Other units have no engine-stored cast TIMES (the engine does keep the
// current-cast spellID at [unit+0xC8C], but nothing temporal), so we
// capture their casts from the `SMSG_SPELL_START` packet (the only place
// the client sees another unit's cast + a server-authoritative cast time)
// into a per-caster cache — see the SpellStart co-hook below. Scope:
//   - Player casts/channels: full timing (self-tracked, exact engine math).
//   - Other units' regular CASTS: full timing while within the cast window,
//     from the SpellStart cache. Aborts (interrupt, death, movement,
//     cancel) evict the entry the moment the engine stops the unit's cast
//     — the CGUnit_C::ClearCastingSpell co-hook below, backstopped by
//     co-hooks on both failure packets — so no lingering ghost bar.
//   - Other units' CHANNELS: the broadcast +0x228 field gates "channeling
//     now"; the SpellStart cache adds real start/end times when we observed
//     the channel begin (else spellID/name/texture with nil times).
//   - Other units' PUSHBACK is invisible: SMSG_SPELL_DELAYED goes only to
//     the affected caster (empirically confirmed — during a melee-pushback
//     test only the victim's client received it), so a remote bar can't
//     stretch. Inherent 1.12 protocol gap, not recoverable client-side.
//
// The other modern-signature fields: `castID` is the cast's castGUID
// (Spell::CastEvents), `isTradeskill` is SPELL_ATTR_TRADESPELL, `delayTimeMs`
// the accumulated pushback, `notInterruptible` the 3.3.5 client's own
// player-relative predicate on 1.12 data (Spell::Interruptible). `castBarID`
// has no 1.12 source and stays nil.

#include "Game.h"
#include "Offsets.h"
#include "aura/Source.h"
#include "dbc/Lookup.h"
#include "net/PacketDispatch.h"
#include "net/PacketReader.h"
#include "player/Info.h"
#include "spell/Interruptible.h"
#include "spell/Lookup.h"
#include "spell/CastEvents.h"
#include "tick/WorldTick.h"
#include "time/Clock.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Spell::Cast {

namespace {

using GetCastTime_t = uint32_t(__fastcall *)(int spellID, int unit, int flag);
using GetDuration_t = int(__fastcall *)(const uint8_t *spellRecord, int unit, int skipMod);

// SPELL_ATTR_TRADESPELL — set on profession recipe casts (enchant,
// cooking with a cast, etc.). The version-stable `isTradeskill` source:
// 3.3.5's Script_UnitCastingInfo reads bit 0x20 of Attributes (at +0x10
// there, +0x18 here — the bit value is the same across builds).
constexpr uint32_t SPELL_ATTR_TRADESPELL = 0x20;

// SPELL_ATTR_RANGED — every ranged ability carries it; the server adds a
// flat +500ms "aim time" to these (SpellEntry::GetCastTime). Auto-repeat
// shots (Auto Shot / Shoot, AttributesEx2 bit 0x20) are the one exclusion.
constexpr uint32_t SPELL_ATTR_RANGED = 0x2;

// SMSG_SPELL_START target block: SpellCastTargets writes a u16 target mask,
// then a packed GUID for the unit target when this bit is set (1.12 server
// SpellCastTargets::write). Self / ground / item targets carry no unit GUID —
// a cast with none reports no target. Per project convention TARGET_* flags
// stay local (single-use, no drift).
constexpr uint16_t TARGET_FLAG_UNIT = 0x2;

struct TrackedSpell {
    int spellID; // 0 = not casting / channeling
    int startMs;
    int endMs;
    int delayMs; // accumulated pushback (SMSG_SPELL_DELAYED), 0 = none
};

TrackedSpell g_cast{0, 0, 0, 0};
TrackedSpell g_channel{0, 0, 0, 0};

// The unit GUID the player is currently casting / channeling AT (from the
// SMSG_SPELL_START target block), or 0 for a self / ground / untargeted cast.
// Reset on each fresh client-side stamp; filled when the confirming packet's
// target lands. Read by `UnitSpellTargetName` and gated there on g_cast /
// g_channel being active, so a stale value from a finished cast is never read.
uint64_t g_castTargetGuid = 0;

// True when g_cast was stamped from SMSG_SPELL_START (a chained same-spell
// recast the client cast path bailed on) rather than the client-side
// FUN_CAST_START_SET hook. Such casts never set VAR_CURRENT_CAST_SPELL, so
// the WorldTick VAR==0 clear must not touch them.
bool g_castFromServer = false;

// Set when a movement-immune cast's engine cast-state was dropped by the player
// MOVING (issue #23 — grenades). Such a cast isn't really interrupted; the
// server completes it. So we keep tracking it instead of ending it, and clear
// it on its SPELL_GO completion (→ STOP, no INTERRUPTED) or a grace past its end.
// A real cancel / interrupt happens with the player stationary, so it clears
// normally. Reset on every fresh cast stamp.
bool g_castMoveDropped = false;

// Armed once the broadcast UNIT_FIELD_CHANNEL_SPELL (+0x228) has been seen to
// reflect g_channel's spell — the enable for OnWorldTick's channel-stop poll.
// StampChannel re-arms it (false) on every channel stamp so that field's
// ~1-tick start lag can't false-clear a channel we just stamped.
bool g_channelConfirmed = false;

// Stamp the player's channel and re-arm the stop latch. A channel supersedes
// any in-flight cast. Both channel-stamp sites (SMSG_SPELL_START's instant-
// channel path and the MSG_CHANNEL_START handler) route through here so
// neither can forget to reset the latch.
void StampChannel(int spellID, int startMs, int endMs) {
    g_channel = TrackedSpell{spellID, startMs, endMs, 0};
    g_channelConfirmed = false;
    g_cast.spellID = 0;
}

// Cast tracking keeps ticks in a signed int internally by design — see PushMs
// for why that's safe (small deltas cancel; the Lua boundary re-casts to
// uint32 so it matches GetTime()*1000 across the wrap). The reader itself
// still comes from the one canonical source.
int NowMs() { return static_cast<int>(Time::Clock::NowMs()); }

// A cast is interrupted by the caster moving only if its Spell.dbc InterruptFlags
// carry SPELL_INTERRUPT_FLAG_MOVEMENT (the server's HandleMovementOpcodes and the
// client's own CheckCast both gate the movement interrupt on this bit). Thrown
// items — grenades — lack it. Unknown spell → assume interruptible (the safe
// default: never keeps a phantom bar alive).
bool MovementInterruptible(int spellID) {
    const uint8_t *rec = Spell::Lookup::RecordForID(spellID);
    if (rec == nullptr)
        return true;
    return (Game::Read<uint32_t>(
                rec, Offsets::OFF_SPELL_RECORD_INTERRUPT_FLAGS) &
            Offsets::SPELL_INTERRUPT_FLAG_MOVEMENT) != 0;
}

// Grace past a move-dropped cast's computed end before we give up on its
// SPELL_GO and end it anyway (covers the SPELL_GO landing ~1 RTT after the
// bar's computed end).
constexpr int kMoveDroppedGraceMs = 750;

// Is the local player moving in a way that drops an in-progress cast? Mirrors
// the engine's own cast-drop movement test (CheckCast masks the player movement
// flags with MOVEFLAG_MASK_CAST_DROP). Used to tell a spurious movement drop of
// a movement-immune cast (grenades) from a real cancel/interrupt (stationary).
// False (the safe answer — clear normally) when the player object or its
// movement block is missing.
bool PlayerMoving() {
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return false;
    const uint8_t *move = Game::Read<const uint8_t *>(
        player, Offsets::OFF_UNIT_MOVEMENT_INFO_PTR);
    if (move == nullptr)
        return false;
    return (Game::Read<uint32_t>(move, Offsets::OFF_MOVEMENT_FLAGS) &
            Offsets::MOVEFLAG_MASK_CAST_DROP) != 0;
}

// Pushes an engine-ms timestamp to Lua as an UNSIGNED 32-bit value.
//
// `FUN_OS_TICKCOUNT_MS` returns a 64-bit ms tick from one of two backends
// (GetTickCount, or a scaled rdtsc — the latter survives warm reboots, so
// the value can be well past 2^31 even right after a "restart"). We keep
// ticks in a signed int internally, which is fine for the small deltas and
// window compares (they cancel), but a direct int->double push surfaces a
// NEGATIVE startTimeMs/endTimeMs once the tick's high bit is set.
//
// The contract is that these match `GetTime()*1000`. Script_GetTime
// (0x00515EA0) does `(double)(tick & 0xFFFFFFFF) * 0.001` — i.e. it masks
// the tick to the low 32 bits UNSIGNED, then scales. So reinterpreting our
// stored tick as uint32 reproduces GetTime()'s exact value for the whole
// range, including across the ~49.7-day 2^32 wrap (both wrap together).
// Carrying 64-bit precision here would be WRONG: it would diverge from
// GetTime() past 2^32 and break addons' (now-start)/(end-start) math.
void PushMs(void *L, int ms) {
    Game::Lua::PushNumber(L, static_cast<double>(static_cast<uint32_t>(ms)));
}

void *Resolve(const char *token) {
    return Game::ResolveUnitToken(token);
}

// Effective cast time (ms) for the local player, 0 if instant. Uses the
// engine's own cast-time helper (base + level scaling + cast-time SpellMod
// op 10 + the caster's haste/cast-speed multiplier), so our end time
// matches the engine's cast bar exactly — including talents like a Mage's
// faster casts. Unit arg 0 = local player; flag 0 clamps negatives to 0.
int CastTimeMs(int spellID) {
    int ms = static_cast<int>(
        reinterpret_cast<GetCastTime_t>(Offsets::FUN_GET_CAST_TIME)(spellID, 0, 0));
    // FUN_006e3340 (also the spell tooltip's cast-time source) does NOT add the
    // flat +500ms "aim time" the server applies to ranged, non-autorepeat
    // spells — SpellEntry::GetCastTime does `castTime += 500` for
    // SPELL_ATTR_RANGED. So our zero-latency prediction runs ~500ms short of
    // the server for Volley / Aimed Shot / Multi-Shot; the confirming
    // SMSG_SPELL_START then snaps the bar out ~1 RTT later. Mirror the server
    // here so the prediction is right from frame 1 (only for spells that
    // already have a cast time — instant ranged abilities that the server
    // gives a 500ms aim time still surface through the packet path). The
    // cast-time reconcile in HandleSpellStart remains the backstop.
    if (ms > 0) {
        const uint8_t *rec = Spell::Lookup::RecordForID(spellID);
        if (rec != nullptr) {
            const uint32_t attr =
                Game::Read<uint32_t>(rec, Offsets::OFF_SPELL_RECORD_ATTRIBUTES);
            const uint32_t attrEx2 =
                Game::Read<uint32_t>(rec, Offsets::OFF_SPELL_RECORD_ATTRIBUTES_EX2);
            if ((attr & SPELL_ATTR_RANGED) && !(attrEx2 & Offsets::SPELL_ATTR_EX2_AUTOREPEAT_FLAG))
                ms += 500;
        }
    }
    return ms;
}

// Effective channel duration (ms) for the local player, 0 if none. Uses
// the engine's own duration helper (SpellDuration base + level scaling +
// duration SpellMod op 1), mirroring how the cast time is sourced, so the
// channel end time matches the engine. Unit arg 0 = local player.
int ChannelDurationMs(int spellID) {
    const uint8_t *rec = Spell::Lookup::RecordForID(spellID);
    if (rec == nullptr)
        return 0;
    const int ms = reinterpret_cast<GetDuration_t>(Offsets::FUN_GET_SPELL_DURATION)(rec, 0, 0);
    return ms > 0 ? ms : 0; // negative = "infinite"; channels are finite
}

const char *SpellName(const uint8_t *rec) {
    const int locale = Game::Read<int>(Offsets::VAR_LOCALE_INDEX);
    return Game::Read<const char *>(rec, Offsets::OFF_SPELL_NAMES + locale * 4);
}

bool IsTradeskill(const uint8_t *rec) {
    return (Game::Read<uint32_t>(rec, Offsets::OFF_SPELL_RECORD_ATTRIBUTES) &
            SPELL_ATTR_TRADESPELL) != 0;
}

// Spell icon texture path, or "" if there's no icon record. SpellIcon.dbc
// already stores the full "Interface\Icons\..." path (unlike item icons,
// which are bare filenames) — so it's used verbatim, no prefix.
const char *SpellIconPath(const uint8_t *rec) {
    const int iconID = Game::Read<int>(rec, Offsets::OFF_SPELL_RECORD_ICON_ID);
    const uint8_t *iconRec = DBC::Record(Offsets::VAR_SPELL_ICON_RECORDS,
                                         Offsets::VAR_SPELL_ICON_COUNT,
                                         static_cast<uint32_t>(iconID));
    if (iconRec == nullptr)
        return "";
    const char *path = Game::Read<const char *>(iconRec, Offsets::OFF_SPELLICON_PATH);
    return (path != nullptr) ? path : "";
}

// Pushes UnitCastingInfo's 11-tuple from a tracked cast, or nothing (nil)
// if there's no active cast. `casterGuid` identifies whose cast this is (the
// player's or a remote unit's) so `castID` can be pulled from Spell::CastEvents
// — the same castGUID the cast's UNIT_SPELLCAST_* events carry. `casterUnit`
// is the caster's CGUnit (null when unresolvable) for the immunity-aura half
// of `notInterruptible`.
int PushCastInfo(void *L, const TrackedSpell &c, uint64_t casterGuid,
                 const uint8_t *casterUnit) {
    if (c.spellID == 0)
        return 0;
    // Self-expire: once the cast window has elapsed, report nothing even if
    // the slot wasn't explicitly cleared. Keeps a stale cast (e.g. one whose
    // clear we missed) from lingering on an addon's cast bar, and lets a
    // fresh stamp for the next cast take over cleanly.
    if (NowMs() >= c.endMs)
        return 0;
    const uint8_t *rec = Spell::Lookup::RecordForID(c.spellID);
    if (rec == nullptr)
        return 0;
    const char *name = SpellName(rec);
    Game::Lua::PushString(L, name);                          // 1 name
    Game::Lua::PushString(L, name);                          // 2 displayName
    Game::Lua::PushString(L, SpellIconPath(rec));            // 3 textureID (path)
    PushMs(L, c.startMs);                                    // 4 startTimeMs
    PushMs(L, c.endMs);                                      // 5 endTimeMs
    Game::Lua::PushBool(L, IsTradeskill(rec));               // 6 isTradeskill
    char castGuid[48];                                       // 7 castID
    if (Spell::CastEvents::CurrentCastGuid(casterGuid, c.spellID, castGuid,
                                           sizeof castGuid))
        Game::Lua::PushString(L, castGuid);
    else
        Game::Lua::PushNil(L);
    Game::Lua::PushBool(L, Spell::Interruptible::NotInterruptible(
                               casterUnit, rec, /*isChannel*/ false)); // 8 notInterruptible
    Game::Lua::PushNumber(L, static_cast<double>(c.spellID)); // 9 castingSpellID
    Game::Lua::PushNil(L);                                   // 10 castBarID
    Game::Lua::PushNumber(L, static_cast<double>(c.delayMs)); // 11 delayTimeMs
    return 11;
}

// Pushes UnitChannelInfo's 8-tuple. `haveTimes` is false for remote units
// (we only track the local player's channel start), pushing nil times.
// `casterUnit` is the channeling CGUnit (null when unresolvable), for the
// immunity-aura half of `notInterruptible`.
int PushChannelInfo(void *L, int spellID, int startMs, int endMs, bool haveTimes,
                    const uint8_t *casterUnit) {
    if (spellID == 0)
        return 0;
    // Self-expire a timed channel once its window elapses (mirrors
    // PushCastInfo) — this is how the player's channel clears, since we no
    // longer poll the +0x228 field. Remote callers gate on endMs before
    // passing haveTimes, so this is a no-op for them.
    if (haveTimes && endMs != 0 && NowMs() >= endMs)
        return 0;
    const uint8_t *rec = Spell::Lookup::RecordForID(spellID);
    if (rec == nullptr)
        return 0;
    const char *name = SpellName(rec);
    Game::Lua::PushString(L, name);                          // 1 name
    Game::Lua::PushString(L, name);                          // 2 text / displayName
    Game::Lua::PushString(L, SpellIconPath(rec));            // 3 textureID (path)
    if (haveTimes) {
        PushMs(L, startMs);                                  // 4 startTimeMs
        PushMs(L, endMs);                                    // 5 endTimeMs
    } else {
        Game::Lua::PushNil(L);
        Game::Lua::PushNil(L);
    }
    Game::Lua::PushBool(L, IsTradeskill(rec));               // 6 isTradeskill
    Game::Lua::PushBool(L, Spell::Interruptible::NotInterruptible(
                               casterUnit, rec, /*isChannel*/ true)); // 7 notInterruptible
    Game::Lua::PushNumber(L, static_cast<double>(spellID));   // 8 spellID
    return 8;
}

// ---- Player cast detection (FUN_CAST_START_SET co-hook) ------------------

// The engine's cast-start writer — the client path for a NORMAL cast. We
// stamp from its spellID argument the instant the cast begins (zero
// latency), *before* the original so any SPELLCAST_START it triggers sees
// fresh times. This does NOT fire for a chained same-spell recast — the
// caller (Spell_C_CastSpell) bails at `spellID == VAR_CURRENT_CAST_SPELL`
// before ever reaching this writer; those are caught by the SMSG_SPELL_START
// path instead. The `spellID == 0` (queued-restore / clear) path is left to
// the OnWorldTick VAR==0 poll so a restore doesn't prematurely wipe a bar.
using CastStartSet_t = void(__fastcall *)(int spellID, int targetState);
CastStartSet_t g_origCastStartSet = nullptr;

void __fastcall CastStartSet_h(int spellID, int targetState) {
    if (spellID != 0) {
        const int dur = CastTimeMs(spellID);
        if (dur > 0) {
            const int now = NowMs();
            g_cast = TrackedSpell{spellID, now, now + dur, 0};
            g_castFromServer = false;  // client-tracked; VAR==0 clears it
            g_castMoveDropped = false; // fresh cast — clear the move-drop latch
            g_channel.spellID = 0;     // a cast supersedes any channel
            g_castTargetGuid = 0;      // target arrives with the confirming packet
        }
        // dur == 0: instant (no bar) or channel (handled via +0x228 poll);
        // leave g_cast — don't clobber an unrelated active cast.
    }
    g_origCastStartSet(spellID, targetState);
}

const Game::HookAutoRegister _castStartHook{
    Offsets::FUN_CAST_START_SET, reinterpret_cast<void *>(&CastStartSet_h),
    reinterpret_cast<void **>(&g_origCastStartSet)};

// ---- Remote-unit cast tracking (SMSG_SPELL_START) ------------------------

// Vanilla stores no cast record for other units, so we capture their casts
// from SMSG_SPELL_START — the only packet carrying another unit's cast with
// a server-authoritative cast time. Keyed by caster GUID (a unit casts one
// thing at a time). Regular casts back `UnitCastingInfo`; channels add real
// times to `UnitChannelInfo` (validated against the live +0x228 field).

bool IsChannelSpell(const uint8_t *rec) {
    return (Game::Read<uint32_t>(rec, Offsets::OFF_SPELL_RECORD_ATTRIBUTES_EX) &
            Offsets::SPELL_ATTR_EX_CHANNELED) != 0;
}

// Channel duration for a non-player caster — base (skipMod=1), since we
// only have the local player's spell-mod tables.
int RemoteChannelDurationMs(const uint8_t *rec) {
    const int ms = reinterpret_cast<GetDuration_t>(
        Offsets::FUN_GET_SPELL_DURATION)(rec, 0, /*skipMod*/ 1);
    return ms > 0 ? ms : 0;
}

struct RemoteCast {
    uint64_t casterGuid;
    uint64_t targetGuid; // unit the caster is casting at, 0 if none
    int spellID;
    int startMs;
    int endMs;
    bool isChannel;
    bool used;
};

constexpr int kRemoteCastSlots = 64;
RemoteCast g_remoteCasts[kRemoteCastSlots];
int g_remoteCursor = 0;

void StoreRemoteCast(uint64_t caster, uint64_t targetGuid, int spellID,
                     int startMs, int endMs, bool isChannel) {
    const RemoteCast entry{caster, targetGuid, spellID,
                           startMs, endMs, isChannel, true};
    // One active cast per unit — replace any existing entry for this caster.
    for (auto &e : g_remoteCasts) {
        if (e.used && e.casterGuid == caster) {
            e = entry;
            return;
        }
    }
    const int now = NowMs();
    for (auto &e : g_remoteCasts) {
        if (!e.used || now >= e.endMs) {
            e = entry;
            return;
        }
    }
    g_remoteCasts[g_remoteCursor] = entry;
    g_remoteCursor = (g_remoteCursor + 1) % kRemoteCastSlots;
}

const RemoteCast *FindRemoteCast(uint64_t caster) {
    if (caster == 0)
        return nullptr;
    for (const auto &e : g_remoteCasts) {
        if (e.used && e.casterGuid == caster)
            return &e;
    }
    return nullptr;
}

// How recently the client-side FUN_CAST_START_SET hook must have stamped the
// same spell for an SMSG_SPELL_START to count as that cast's confirming
// packet (skip — don't restart the bar) rather than a new chained recast
// (stamp). Must exceed worst-case latency but stay under a GCD / min cast
// time so genuine chained casts aren't wrongly deduped.
constexpr int kCastStartDedupMs = 500;

void HandleSpellStart(uint64_t caster, int spellID, uint32_t castTime,
                      uint64_t targetGuid) {
    if (caster == 0 || spellID == 0)
        return;
    const uint8_t *rec = Spell::Lookup::RecordForID(spellID);
    if (rec == nullptr)
        return;
    const int now = NowMs();
    const bool channel = IsChannelSpell(rec);

    if (caster == Unit::Identity::PlayerGuid()) {
        if (channel && castTime == 0) {
            // Player channel start. The broadcast +0x228 field that signals
            // "channeling" is server-pushed and lands ~1 tick (~1s) after the
            // channel actually begins, so a +0x228 poll shows the bar far too
            // late — the addon already polled nil at SPELLCAST_CHANNEL_START
            // and cleared. This packet IS the channel start: stamp g_channel
            // now with the engine-computed duration. We run before the
            // original handler that fires SPELLCAST_CHANNEL_START, so the data
            // is fresh when addons poll. Cleared by self-expiry (PushChannel-
            // Info) at endMs. (MSG_CHANNEL_START re-stamps moments later with
            // the server's duration — see ChannelStart_h.)
            const int dur = ChannelDurationMs(spellID);
            if (dur > 0) {
                StampChannel(spellID, now, now + dur);
                g_castTargetGuid = targetGuid;
            }
            return;
        }
        // channel && castTime > 0 → a CAST-THEN-CHANNEL spell (Mind Control:
        // 3 s cast, then a 60 s channel). This packet is the cast phase —
        // the channel begins only when the cast completes, signaled by
        // MSG_CHANNEL_START (ChannelStart_h). Fall through to regular cast
        // handling so the bar shows the 3 s cast, not a premature 60 s
        // channel (pfUI #9).
        if (castTime == 0)
            return; // instant — no cast bar
        // Backstop for chained same-spell recasts: the engine short-circuits
        // Spell_C_CastSpell at `spellID == VAR_CURRENT_CAST_SPELL`, so the
        // client path (FUN_CAST_START_SET hook) never fires for them — this
        // server packet is the only signal. Dedup against a fresh client stamp
        // (same spell, started within ~latency): a normal cast's confirming
        // packet mustn't RESTART its bar; otherwise it's a chained recast the
        // client bailed on — take it, and flag it so the VAR==0 poll (which
        // never saw it) won't clear it.
        if (g_cast.spellID == spellID && now < g_cast.endMs &&
            now - g_cast.startMs < kCastStartDedupMs) {
            // Confirming packet. Keep startMs (no visual restart), but SNAP the
            // end time to the server's authoritative castTime. The client stamp
            // came from FUN_006e3340, whose only caster factor is the cast-speed
            // multiplier at descriptor +0x22c (UNIT_MOD_CAST_SPEED — spell
            // haste); the server additionally scales ranged ABILITIES by ranged
            // attack speed (SpellEntry::GetCastTime's IsRangedSpell branch,
            // m_modAttackSpeedPct[RANGED_ATTACK] — quiver/Quick Shots), which
            // the prediction can't see. Two real divergences so far: Volley
            // (client +0x22c hastes it, server doesn't → bar ended early) and
            // Steady/Aimed Shot (server ranged-hastes it, client doesn't → bar
            // ran long, pfUI#43). The server's castTime is reality; reconcile
            // to it (preserving accumulated pushback). A no-op when they agree;
            // when they differ, PollPlayer sees the end move and fires
            // UNIT_SPELLCAST_DELAYED so cast bars re-read the corrected times.
            g_cast.endMs =
                g_cast.startMs + static_cast<int>(castTime) + g_cast.delayMs;
            g_castTargetGuid = targetGuid; // confirming packet carries the target
            return;
        }
        g_cast = TrackedSpell{spellID, now, now + static_cast<int>(castTime), 0};
        g_castFromServer = true;
        g_castMoveDropped = false; // fresh cast — clear the move-drop latch
        g_channel.spellID = 0;     // a cast supersedes any channel
        g_castTargetGuid = targetGuid;
        return;
    }
    // A channeled spell with a cast time (Mind Control) is in its CAST phase
    // here — store it as a regular cast so observers see the 3 s bar. The
    // remote channel phase has no packet (MSG_CHANNEL_START is caster-only);
    // it surfaces through the unit's broadcast +0x228 field as usual.
    const bool instantChannel = channel && castTime == 0;
    const int endMs = instantChannel ? now + RemoteChannelDurationMs(rec)
                                     : now + static_cast<int>(castTime);
    StoreRemoteCast(caster, targetGuid, spellID, now, endMs, instantChannel);
    // Phase 2 events: START (cast) or CHANNEL_START (channel). Skip pure
    // instants (non-channel, castTime 0 → no bar); their SUCCEEDED still fires
    // from the SPELL_GO hook.
    if (castTime > 0 || instantChannel)
        Spell::CastEvents::OnRemoteCastStart(caster, spellID, endMs,
                                             instantChannel);
}

// SMSG_SPELL_START parse. Body (mirrored from nampower's SpellStartHandler):
// itemGuid(packed), casterGuid(packed), spellId(u32), castFlags(u16),
// castTime(u32), then the SpellCastTargets block: targetMask(u16) and — when
// TARGET_FLAG_UNIT is set — a packed target GUID (the unit the cast is aimed
// at; the field that backs UnitSpellTargetName). Runs from the
// Net::PacketDispatch funnel with the cursor already positioned at the body.
void ParseSpellStart(Net::CDataStore *packet) {
    Net::ReadPackedGuid(packet); // itemGuid (unused)
    const uint64_t caster = Net::ReadPackedGuid(packet);
    const int spellID = static_cast<int>(Net::Read<uint32_t>(packet));
    Net::Read<uint16_t>(packet); // castFlags (unused)
    const uint32_t castTime = Net::Read<uint32_t>(packet);
    // TARGET_FLAG_UNIT is the first field the server writes after the mask, so
    // the packed GUID sits immediately after it (SpellCastTargets::write). Other
    // target kinds (ground coords, item) carry no unit GUID → target is 0.
    const uint16_t targetMask = Net::Read<uint16_t>(packet);
    const uint64_t targetGuid =
        (targetMask & TARGET_FLAG_UNIT) ? Net::ReadPackedGuid(packet) : 0;
    HandleSpellStart(caster, spellID, castTime, targetGuid);
}

// SMSG_SPELL_DELAYED — cast pushback. The server only sends it to the
// affected caster, so it's effectively always the local player; extend the
// tracked cast's end (and report the accumulated delay as delayTimeMs) so an
// addon's bar stretches on damage. Body: guid(u64), delay(u32).
void ParseSpellDelayed(Net::CDataStore *packet) {
    const uint64_t guid = Net::Read<uint64_t>(packet);
    const uint32_t delay = Net::Read<uint32_t>(packet);
    if (delay != 0 && g_cast.spellID != 0 &&
        guid == Unit::Identity::PlayerGuid()) {
        g_cast.endMs += static_cast<int>(delay);
        g_cast.delayMs += static_cast<int>(delay);
    }
}

// MSG_CHANNEL_START — the server tells the caster a channel has begun, with
// the server-authoritative (modifier-applied) duration. Sent only to the
// caster, so always the local player. This is what makes CAST-THEN-CHANNEL
// spells (Mind Control: 3 s cast, then a 60 s channel) show both phases:
// their SMSG_SPELL_START carries the cast (castTime > 0, handled as a regular
// cast in HandleSpellStart) and only this packet marks the channel starting.
// For instant channels it re-stamps what the SpellStart path just wrote (same
// moment, server duration) — harmless. Running from the dispatch funnel keeps
// the stamp ahead of the engine's leaf handler (which fires
// SPELLCAST_CHANNEL_START), so the data is fresh when addons poll. Body:
// spellId(u32), durationMs(u32).
void ParseChannelStart(Net::CDataStore *packet) {
    const int spellID = static_cast<int>(Net::Read<uint32_t>(packet));
    const uint32_t duration = Net::Read<uint32_t>(packet);
    if (spellID != 0 && duration > 0) {
        const int now = NowMs();
        StampChannel(spellID, now, now + static_cast<int>(duration));
    }
}

// MSG_CHANNEL_UPDATE — sent to the channeling PLAYER on damage pushback
// (vanilla channels SHORTEN when hit) and once more at the channel's end
// (remaining == 0). Body is a single u32 = the new remaining time (ms); the
// spell is the in-progress channel (the packet carries no spellID). The engine
// stores the new end nowhere, so re-anchor g_channel.endMs to the server's
// remaining time so UnitChannelInfo (and the OnWorldTick endMs self-expiry)
// track pushback; on remaining == 0 clear the channel so CHANNEL_STOP fires
// promptly (ahead of the ~1 s-lagged +0x228 field). The pushback also
// shortened the channel's aura on each hit target server-side
// (Spell::DelayedChannel → DelaySpellAuraHolder), so re-anchor the cached
// target-side expirations too — else C_UnitAuras reads a pushed-back channel
// debuff (Dark Harvest, the drains) late by the accumulated pushback.
void ParseChannelUpdate(Net::CDataStore *packet) {
    if (g_channel.spellID == 0)
        return;
    const uint32_t remaining = Net::Read<uint32_t>(packet);
    const int spellID = g_channel.spellID;
    if (remaining == 0) {
        g_channel.spellID = 0; // authoritative channel end
    } else {
        g_channel.endMs = NowMs() + static_cast<int>(remaining);
        Aura::Source::RestampPlayerChannel(static_cast<uint32_t>(spellID),
                                           remaining);
        Spell::CastEvents::OnPlayerChannelUpdate(spellID);
    }
}

// Shared abort handling for the two sibling failure packets. Servers
// derived from (v)mangos broadcast BOTH `SMSG_SPELL_FAILURE` and
// `SMSG_SPELL_FAILED_OTHER` from Spell::SendInterrupted, but which one a
// given core emits per abort path varies — so both handlers are co-hooked
// and both feed this. Idempotent, so the both-arrive case is harmless.
void HandleCastAborted(uint64_t guid, int spellID) {
    if (guid == 0 || spellID == 0)
        return;
    // Remote units: invalidate the cached cast/channel so
    // UnitCastingInfo/UnitChannelInfo stop reporting it this tick.
    for (auto &e : g_remoteCasts) {
        if (e.used && e.casterGuid == guid && e.spellID == spellID) {
            e.used = false;
            break;
        }
    }
    // Local player: a server-stamped chained recast (g_castFromServer)
    // never set VAR_CURRENT_CAST_SPELL, so the WorldTick VAR==0 poll can't
    // clear it on interrupt — this packet is the only signal. Whether the
    // caster receives their own broadcast is server-dependent; if it never
    // fires, the endMs self-expiry backstop still applies.
    if (guid == Unit::Identity::PlayerGuid()) {
        // Cast interrupts are surfaced by PollPlayer / SpellFailed_h. Player
        // CHANNELS never fire INTERRUPTED (retail only fires CHANNEL_STOP for
        // them, interrupted or not), so there's nothing to do for a channel.
        if (g_cast.spellID == spellID) {
            // A movement-immune cast (grenade) whose cast-state the movement
            // path just cleared isn't really interrupted — the server completes
            // it (issue #23). Keep tracking it (mark it move-dropped); OnWorldTick
            // ends it on its SPELL_GO or a grace past its end. A real cancel /
            // interrupt happens with the player NOT moving, so it clears here.
            if (!MovementInterruptible(spellID) && PlayerMoving())
                g_castMoveDropped = true;
            else
                g_cast.spellID = 0;
        }
        return;
    }
    // Phase 2: remote unit — the poll fires INTERRUPTED + STOP for it.
    Spell::CastEvents::OnRemoteAborted(guid, spellID);
}

// SMSG_SPELL_FAILURE / SMSG_SPELL_FAILED_OTHER — sibling abort packets. The
// server broadcasts one (which varies by core / abort path) to observers when
// a started cast aborts (interrupt, death, movement, fizzle); handling both
// makes eviction independent of which the server chose. Both share the head
// casterGuid(u64), spellId(u32) — plain, not packed (SpellFailure has a
// trailing result(u8) we don't read). Drops a remote cast the moment it dies
// instead of letting the ghost bar run to its computed end.
void ParseCastAborted(Net::CDataStore *packet) {
    const uint64_t guid = Net::Read<uint64_t>(packet);
    const int spellID = static_cast<int>(Net::Read<uint32_t>(packet));
    HandleCastAborted(guid, spellID);
}

// Co-hook on CGUnit_C::ClearCastingSpell — the engine choke point every
// "unit stopped casting" path funnels through (failure packets, SPELL_GO,
// movement, death, animation events, local cancel — see Offsets.h). The
// original is gated on `spellID == [unit+0xC8C]` (the unit's live
// current-cast field), so a real stop only happens when the gate passes;
// we mirror the gate and evict then. This is the PRIMARY interrupt signal
// on Turtle: its core doesn't broadcast either failure packet — interrupt
// propagation to observers happens via SuperWoW, whose code calls this
// method directly (verified by return-address tracing: interrupted remote
// casts clear with a caller inside a sibling DLL, natural completions
// clear from the SPELL_GO handler). Verified in-game: Kick / Earth Shock /
// Counterspell each evict the victim's cast, while non-interrupting damage
// (pushback flinch) does NOT route through here — no false eviction.
using ClearCastingSpell_t = void(__fastcall *)(void *unit, void *edx,
                                               int spellID, char notify,
                                               char cleanup);
ClearCastingSpell_t g_origClearCastingSpell = nullptr;

void __fastcall ClearCastingSpell_h(void *unit, void *edx, int spellID,
                                    char notify, char cleanup) {
    if (unit != nullptr && spellID != 0) {
        const int current = Game::Read<int>(unit, Offsets::OFF_UNIT_CAST_SPELL);
        if (current != 0 && current == spellID)
            HandleCastAborted(Unit::Identity::GuidForObject(unit), spellID);
    }
    g_origClearCastingSpell(unit, edx, spellID, notify, cleanup);
}

const Game::HookAutoRegister _clearCastingHook{
    Offsets::FUN_UNIT_CLEAR_CASTING_SPELL,
    reinterpret_cast<void *>(&ClearCastingSpell_h),
    reinterpret_cast<void **>(&g_origClearCastingSpell)};

// One subscriber on the SMSG dispatch funnel, replacing the six per-opcode
// co-hooks these spell packets used to install (SpellStart / Delayed /
// ChannelStart / ChannelUpdate / Failure / FailedOther) — every one of which
// nampower also detours. The dispatcher hands us the cursor at the body (the
// opcode already consumed). CastStartSet and ClearCastingSpell stay direct
// hooks: they're engine methods, not SMSG handlers, so the funnel can't reach
// them (and ClearCastingSpell is the SuperWoW-shared interrupt choke point).
void SpellPacketSub(uint32_t opcode, Net::CDataStore *packet) {
    if (packet == nullptr)
        return;
    switch (opcode) {
    case Offsets::SMSG_SPELL_START:          ParseSpellStart(packet); break;
    case Offsets::SMSG_SPELL_DELAYED:        ParseSpellDelayed(packet); break;
    case Offsets::SMSG_SPELL_CHANNEL_START:  ParseChannelStart(packet); break;
    case Offsets::SMSG_SPELL_CHANNEL_UPDATE: ParseChannelUpdate(packet); break;
    case Offsets::SMSG_SPELL_FAILURE:
    case Offsets::SMSG_SPELL_FAILED_OTHER:   ParseCastAborted(packet); break;
    default:                                 break;
    }
}

const Net::PacketDispatch::AutoSubscribe _spellPacketSub{&SpellPacketSub};

} // namespace

// Per-frame upkeep for the player. The regular cast is stamped by the
// FUN_CAST_START_SET hook; here we clear it when the engine's cast global
// drops to 0, and poll the broadcast +0x228 field for the channel (which
// has no comparable client-side start hook).
void OnWorldTick() {
    // Clear a CLIENT-tracked cast when the cast global returns to 0 — covering
    // normal end, cancel, and interrupt. No grace window is needed: the
    // FUN_CAST_START_SET hook stamps g_cast in the same call that writes the
    // global non-zero, so it's never transiently 0 right after a stamp.
    // Server-stamped chained casts (g_castFromServer) never set the global,
    // so they're exempt — they expire on their computed endMs (self-expiry in
    // PushCastInfo).
    if (g_cast.spellID != 0 && !g_castFromServer &&
        Game::Read<int>(Offsets::VAR_CURRENT_CAST_SPELL) == 0)
        g_cast.spellID = 0;

    // End a move-dropped cast (issue #23): the engine cleared its cast-state on
    // movement, but the server completes it. HandleCastAborted kept it tracked;
    // end it once its SPELL_GO has landed (→ PollPlayer STOP, no INTERRUPTED) or,
    // if none ever comes, a grace past its computed end (→ honest INTERRUPTED +
    // STOP). Gated on the latch, so normal casts are untouched.
    if (g_cast.spellID != 0 && g_castMoveDropped &&
        (Spell::CastEvents::PlayerCastSucceeded() ||
         Time::Clock::Reached(static_cast<uint32_t>(g_cast.endMs + kMoveDroppedGraceMs)))) {
        g_cast.spellID = 0;
        g_castMoveDropped = false;
    }

    // A target-selection spell (Disenchant, ground-target AoE, …) stamps
    // g_cast at the green-cursor step, NOT when the cast actually begins:
    // Spell_C_CastSpell (0x006E4B60) calls the cast-start writer
    // (FUN_CAST_START_SET) BEFORE Spell_C_TargetSpell raises the reticle, and
    // the later item/ground click re-enters Spell_C_CastSpell only to hit its
    // "already this spell" short-circuit — so the writer never re-fires for the
    // real start. Left alone, g_cast would surface a premature cast (UnitCasting-
    // Info + UNIT_SPELLCAST_START) during targeting, seconds early, and a cancel
    // would even fire a phantom START/INTERRUPTED/STOP. So drop the client stamp
    // while the reticle is up for that spell: on selection the server's
    // SMSG_SPELL_START re-stamps it cleanly (g_cast is 0, so HandleSpellStart
    // takes the fresh-stamp path and START fires with the correct time + the
    // SENT castGUID); on cancel it simply stays cleared. Gated on
    // !g_castFromServer so it can never touch that server re-stamp.
    if (g_cast.spellID != 0 && !g_castFromServer &&
        Game::Read<int>(Offsets::VAR_SPELL_TARGETING_FLAGS) != 0 &&
        g_cast.spellID ==
            Game::Read<int>(Offsets::VAR_PENDING_CAST_SPELL))
        g_cast.spellID = 0;

    // Detect a player CHANNEL that ended before its computed endMs — the
    // summon-ritual case (UNIT_FIELD_CHANNEL_SPELL = 698, cleared when the
    // summon fills), and equally an interrupted / cancelled / LoS-broken
    // channel. Without this, g_channel only clears at endMs (self-expiry in
    // PushChannelInfo) or when a later TIMED cast supersedes it — so an early
    // end followed by an INSTANT cast (which supersedes nothing) leaves a
    // ghost bar counting down to a duration that no longer reflects reality.
    //
    // We deliberately don't use +0x228 to START the bar — it's a broadcast
    // UpdateField that lags the channel start by ~1 tick (HandleSpellStart
    // stamps from the packet instead). For the STOP that lag is harmless, but a
    // naive "clear when field == 0" would wipe a channel we just stamped during
    // the lag window. Guard with a latch: only honor the 0 once the field has
    // been seen to reflect our channel spell at least once. A channel too short
    // to ever appear in the field just self-expires at endMs.
    if (g_channel.spellID != 0) {
        const uint8_t *desc = Unit::Identity::PlayerDescriptor();
        if (desc != nullptr) {
            const int chan = Game::Read<int>(
                desc, Offsets::OFF_UNIT_FIELD_CHANNEL_SPELL);
            if (chan == g_channel.spellID)
                g_channelConfirmed = true;
            else if (chan == 0 && g_channelConfirmed)
                g_channel.spellID = 0;
        }
        // Self-expiry backstop for the poll: the +0x228 broadcast clears ~1
        // tick (~1s) late, so a channel that runs its full duration would
        // otherwise linger until then — the poll's CHANNEL_STOP (and any
        // g_channel reader) would trail the real end by ~1s. Clear at the
        // server-computed endMs too, so the effective stop is the EARLIER of
        // (computed end, early +0x228 clear — the summon-ritual / interrupt
        // case). Mirrors PushChannelInfo's endMs guard, which already hides
        // the channel from UnitChannelInfo at this same instant.
        //
        // Wrap-safe compare: the ms tick (rdtsc-backed, survives reboots) can
        // sit past 2^31, where a stored endMs reads NEGATIVE as a signed int.
        // A direct `now >= endMs` would then misfire for a channel straddling
        // the boundary. The signed DELTA cancels the wrap (correct for any
        // interval < ~24.8 days — a channel is always far shorter), so test
        // `now - endMs >= 0` instead.
        if (g_channel.spellID != 0 && g_channel.endMs != 0 &&
            NowMs() - g_channel.endMs >= 0)
            g_channel.spellID = 0;
    }

    // Derive the player's UNIT_SPELLCAST_* events from the cast/channel
    // state above, now that this tick's clears have been applied. Poll-
    // based so it needs no changes to the many stamp/clear sites; ~1 frame
    // latency is imperceptible for a cast bar, and every fire is
    // listener-gated (near-free when no addon uses these).
    Spell::CastEvents::PollPlayer(g_cast.spellID, g_cast.startMs, g_cast.endMs,
                                  g_channel.spellID, g_channel.startMs);
    Spell::CastEvents::PollRemote();
    Spell::CastEvents::PollReticle();
}

static const Tick::WorldTick::AutoSubscribe _tickSub{&OnWorldTick};

// `CastingInfo()` — local player's cast, no token lookup.
static int __fastcall Script_CastingInfo(void *L) {
    return PushCastInfo(L, g_cast, Unit::Identity::PlayerGuid(),
                        Unit::Identity::PlayerObject());
}

// `UnitCastingInfo(unit)` — local player from self-tracking; other units
// from the SMSG_SPELL_START cache (regular casts still within their cast
// window; aborted casts are evicted by the ClearCastingSpell / failure-
// packet co-hooks).
static int __fastcall Script_UnitCastingInfo(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Spell.UnitCastingInfo(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);
    void *u = (token != nullptr) ? Resolve(token) : nullptr;
    if (u == nullptr)
        return 0;
    if (u == Resolve("player"))
        return PushCastInfo(L, g_cast, Unit::Identity::PlayerGuid(),
                            Unit::Identity::PlayerObject());

    const uint64_t guid = Unit::Identity::GuidForObject(u);
    const RemoteCast *rc = FindRemoteCast(guid);
    if (rc != nullptr && !rc->isChannel && NowMs() < rc->endMs)
        return PushCastInfo(L, TrackedSpell{rc->spellID, rc->startMs, rc->endMs},
                            guid, static_cast<const uint8_t *>(u));
    return 0;
}

// `ChannelInfo()` — local player's channel, no token lookup.
static int __fastcall Script_ChannelInfo(void *L) {
    return PushChannelInfo(L, g_channel.spellID, g_channel.startMs, g_channel.endMs, true,
                           Unit::Identity::PlayerObject());
}

// `UnitChannelInfo(unit)` — full timing for the player; spellID/name/
// texture (no times) for other units via the broadcast +0x228 field.
static int __fastcall Script_UnitChannelInfo(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Spell.UnitChannelInfo(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);
    void *u = (token != nullptr) ? Resolve(token) : nullptr;
    if (u == nullptr)
        return 0;
    if (u == Resolve("player"))
        return PushChannelInfo(L, g_channel.spellID, g_channel.startMs, g_channel.endMs, true,
                               Unit::Identity::PlayerObject());

    auto *unit = static_cast<const uint8_t *>(u);
    auto *desc = Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_DESCRIPTOR);
    if (desc == nullptr)
        return 0;
    // The live +0x228 field is authoritative for "is this unit channeling
    // right now"; the SMSG_SPELL_START cache adds real start/end times when
    // we observed the channel begin (and still matches the current spell).
    const int spellID = Game::Read<int>(desc, Offsets::OFF_UNIT_FIELD_CHANNEL_SPELL);
    if (spellID == 0)
        return 0;
    const RemoteCast *rc = FindRemoteCast(Unit::Identity::GuidForObject(unit));
    if (rc != nullptr && rc->isChannel && rc->spellID == spellID && NowMs() < rc->endMs)
        return PushChannelInfo(L, spellID, rc->startMs, rc->endMs, /*haveTimes*/ true, unit);
    return PushChannelInfo(L, spellID, 0, 0, /*haveTimes*/ false, unit);
}

// The unit GUID `caster` is currently casting / channeling AT, or 0 when it
// isn't casting or the spell has no unit target (self / ground / item). The
// player comes from g_cast / g_channel + g_castTargetGuid, gated on an active
// cast so a stale target from a finished cast is never returned; other units
// come from the SMSG_SPELL_START cache while inside their cast window.
static uint64_t TargetGuidForCaster(uint64_t caster) {
    if (caster == 0)
        return 0;
    if (caster == Unit::Identity::PlayerGuid()) {
        const int now = NowMs();
        const bool casting = g_cast.spellID != 0 && now < g_cast.endMs;
        const bool channeling =
            g_channel.spellID != 0 && (g_channel.endMs == 0 || now < g_channel.endMs);
        return (casting || channeling) ? g_castTargetGuid : 0;
    }
    const RemoteCast *rc = FindRemoteCast(caster);
    return (rc != nullptr && NowMs() < rc->endMs) ? rc->targetGuid : 0;
}

// `UnitSpellTargetName(unit)` — name of the unit that `unit` is currently
// casting / channeling a spell at, or nil when `unit` isn't casting, the spell
// has no unit target (self / ground / item), or the target's name can't be
// resolved (an off-screen stranger). ClassicAPI extension; the cast's target
// comes from the SMSG_SPELL_START target block, captured for the player and
// any remote unit whose cast we've observed.
static int __fastcall Script_UnitSpellTargetName(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitSpellTargetName(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);
    const uint64_t caster =
        (token != nullptr) ? Unit::Identity::GuidForToken(token) : 0;
    const uint64_t targetGuid = TargetGuidForCaster(caster);
    char name[64];
    if (targetGuid == 0 || !Player::Info::NameFromGuid(targetGuid, name, sizeof name)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushString(L, name);
    return 1;
}

static void RegisterLuaFunctions() {
    // Registered under C_Spell rather than as globals to avoid clobbering
    // the global `UnitCastingInfo` / `UnitChannelInfo` names. Addons that
    // ship their own vanilla cast-tracking libraries use the
    // `_G.UnitCastingInfo or <fallback>` idiom (e.g. ShaguTweaks'
    // libcast.lua scrapes the combat log for remote/enemy casts the 1.12
    // engine never exposes). Occupying the global makes them adopt our
    // player-only version and drop their superior fallback — so we cede
    // the global names and expose the functions here instead.
    Game::Lua::RegisterTableFunction("C_Spell", "UnitCastingInfo", &Script_UnitCastingInfo);
    Game::Lua::RegisterTableFunction("C_Spell", "CastingInfo", &Script_CastingInfo);
    Game::Lua::RegisterTableFunction("C_Spell", "UnitChannelInfo", &Script_UnitChannelInfo);
    Game::Lua::RegisterTableFunction("C_Spell", "ChannelInfo", &Script_ChannelInfo);
    // A novel ClassicAPI name (no addon ships its own), so it's safe as a
    // global — and it matches the `UnitSpellTargetName(unit)` call shape.
    Game::Lua::RegisterGlobalFunction("UnitSpellTargetName", &Script_UnitSpellTargetName);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::Cast
