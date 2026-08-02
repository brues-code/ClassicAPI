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
// Modern-signature fields vanilla can't fill are structurally-correct
// placeholders: castID / castBarID = nil, notInterruptible = false,
// isTradeskill = false (no readable flag in 1.12), delayTimeMs = 0.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "net/PacketDispatch.h"
#include "net/PacketReader.h"
#include "spell/Lookup.h"
#include "spell/CastEvents.h"
#include "tick/WorldTick.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Spell::Cast {

namespace {

using ResolveUnitToken_t = void *(__fastcall *)(const char *token);
using TickMs_t = uint32_t(__fastcall *)();
using GetCastTime_t = uint32_t(__fastcall *)(int spellID, int unit, int flag);
using GetDuration_t = int(__fastcall *)(const uint8_t *spellRecord, int unit, int skipMod);

// Spell.dbc record field offsets (mirrors Spell::Info's locals).
constexpr int OFF_NAME = 0x1E0;            // localized name[9]
constexpr int OFF_ICON_ID = 0x1D4;         // -> SpellIcon.dbc
constexpr int OFF_ATTRIBUTES = 0x18;       // u32 Attributes flags
constexpr int OFF_ATTRIBUTES_EX2 = 0x24;   // u32 AttributesEx2 flags
constexpr int OFF_SUBRECORD_VALUE = 0x04;  // SpellIcon path field

// SPELL_ATTR_TRADESPELL — set on profession recipe casts (enchant,
// cooking with a cast, etc.). The version-stable `isTradeskill` source:
// 3.3.5's Script_UnitCastingInfo reads bit 0x20 of Attributes (at +0x10
// there, +0x18 here — the bit value is the same across builds).
constexpr uint32_t SPELL_ATTR_TRADESPELL = 0x20;

// SPELL_ATTR_RANGED — every ranged ability carries it; the server adds a
// flat +500ms "aim time" to these (SpellEntry::GetCastTime). Auto-repeat
// shots (Auto Shot / Shoot, AttributesEx2 bit 0x20) are the one exclusion.
constexpr uint32_t SPELL_ATTR_RANGED = 0x2;
constexpr uint32_t SPELL_ATTR_EX2_AUTOREPEAT = 0x20;

struct TrackedSpell {
    int spellID; // 0 = not casting / channeling
    int startMs;
    int endMs;
    int delayMs; // accumulated pushback (SMSG_SPELL_DELAYED), 0 = none
};

TrackedSpell g_cast{0, 0, 0, 0};
TrackedSpell g_channel{0, 0, 0, 0};

// True when g_cast was stamped from SMSG_SPELL_START (a chained same-spell
// recast the client cast path bailed on) rather than the client-side
// FUN_CAST_START_SET hook. Such casts never set VAR_CURRENT_CAST_SPELL, so
// the WorldTick VAR==0 clear must not touch them.
bool g_castFromServer = false;

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

int NowMs() { return static_cast<int>(reinterpret_cast<TickMs_t>(Offsets::FUN_OS_TICKCOUNT_MS)()); }

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
    return reinterpret_cast<ResolveUnitToken_t>(Offsets::FUN_RESOLVE_UNIT_TOKEN)(token);
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
                *reinterpret_cast<const uint32_t *>(rec + OFF_ATTRIBUTES);
            const uint32_t attrEx2 =
                *reinterpret_cast<const uint32_t *>(rec + OFF_ATTRIBUTES_EX2);
            if ((attr & SPELL_ATTR_RANGED) && !(attrEx2 & SPELL_ATTR_EX2_AUTOREPEAT))
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
    const int locale = *reinterpret_cast<int *>(Offsets::VAR_LOCALE_INDEX);
    return *reinterpret_cast<const char *const *>(rec + OFF_NAME + locale * 4);
}

bool IsTradeskill(const uint8_t *rec) {
    return (*reinterpret_cast<const uint32_t *>(rec + OFF_ATTRIBUTES) & SPELL_ATTR_TRADESPELL) != 0;
}

// Spell icon texture path, or "" if there's no icon record. SpellIcon.dbc
// already stores the full "Interface\Icons\..." path (unlike item icons,
// which are bare filenames) — so it's used verbatim, no prefix.
const char *SpellIconPath(const uint8_t *rec) {
    const int iconID = *reinterpret_cast<const int *>(rec + OFF_ICON_ID);
    const uint8_t *iconRec = DBC::Record(Offsets::VAR_SPELL_ICON_RECORDS,
                                         Offsets::VAR_SPELL_ICON_COUNT,
                                         static_cast<uint32_t>(iconID));
    if (iconRec == nullptr)
        return "";
    const char *path = *reinterpret_cast<const char *const *>(iconRec + OFF_SUBRECORD_VALUE);
    return (path != nullptr) ? path : "";
}

// Pushes UnitCastingInfo's 11-tuple from a tracked cast, or nothing (nil)
// if there's no active cast. `casterGuid` identifies whose cast this is (the
// player's or a remote unit's) so `castID` can be pulled from Spell::CastEvents
// — the same castGUID the cast's UNIT_SPELLCAST_* events carry.
int PushCastInfo(void *L, const TrackedSpell &c, uint64_t casterGuid) {
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
    Game::Lua::PushBool(L, false);                           // 8 notInterruptible
    Game::Lua::PushNumber(L, static_cast<double>(c.spellID)); // 9 castingSpellID
    Game::Lua::PushNil(L);                                   // 10 castBarID
    Game::Lua::PushNumber(L, static_cast<double>(c.delayMs)); // 11 delayTimeMs
    return 11;
}

// Pushes UnitChannelInfo's 8-tuple. `haveTimes` is false for remote units
// (we only track the local player's channel start), pushing nil times.
int PushChannelInfo(void *L, int spellID, int startMs, int endMs, bool haveTimes) {
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
    Game::Lua::PushBool(L, false);                           // 7 notInterruptible
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
            g_castFromServer = false; // client-tracked; VAR==0 clears it
            g_channel.spellID = 0;    // a cast supersedes any channel
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

constexpr int OFF_ATTRIBUTES_EX = 0x1C; // Spell.dbc AttributesEx
constexpr uint32_t SPELL_ATTR_EX_CHANNELED = 0x4 | 0x40; // IS_CHANNELED | SELF

bool IsChannelSpell(const uint8_t *rec) {
    return (*reinterpret_cast<const uint32_t *>(rec + OFF_ATTRIBUTES_EX) &
            SPELL_ATTR_EX_CHANNELED) != 0;
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
    int spellID;
    int startMs;
    int endMs;
    bool isChannel;
    bool used;
};

constexpr int kRemoteCastSlots = 64;
RemoteCast g_remoteCasts[kRemoteCastSlots];
int g_remoteCursor = 0;

void StoreRemoteCast(uint64_t caster, int spellID, int startMs, int endMs,
                     bool isChannel) {
    const RemoteCast entry{caster, spellID, startMs, endMs, isChannel, true};
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

void HandleSpellStart(uint64_t caster, int spellID, uint32_t castTime) {
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
            if (dur > 0)
                StampChannel(spellID, now, now + dur);
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
            // came from FUN_006e3340, which folds in the caster's cast-speed /
            // ranged-haste multiplier (descriptor +0x22c) — right for spells the
            // server also hastes (Aimed/Multi-Shot), WRONG for ones it doesn't.
            // Volley is the case that surfaced this: not channeled, not sped by
            // Quick Shots server-side, so the hasted prediction ended the bar
            // early and the player was "still casting after the bar finished."
            // The server's castTime is reality; reconcile to it (preserving any
            // pushback already accumulated). A no-op when client and server agree.
            g_cast.endMs =
                g_cast.startMs + static_cast<int>(castTime) + g_cast.delayMs;
            return;
        }
        g_cast = TrackedSpell{spellID, now, now + static_cast<int>(castTime), 0};
        g_castFromServer = true;
        g_channel.spellID = 0; // a cast supersedes any channel
        return;
    }
    // A channeled spell with a cast time (Mind Control) is in its CAST phase
    // here — store it as a regular cast so observers see the 3 s bar. The
    // remote channel phase has no packet (MSG_CHANNEL_START is caster-only);
    // it surfaces through the unit's broadcast +0x228 field as usual.
    const bool instantChannel = channel && castTime == 0;
    const int endMs = instantChannel ? now + RemoteChannelDurationMs(rec)
                                     : now + static_cast<int>(castTime);
    StoreRemoteCast(caster, spellID, now, endMs, instantChannel);
    // Phase 2 events: START (cast) or CHANNEL_START (channel). Skip pure
    // instants (non-channel, castTime 0 → no bar); their SUCCEEDED still fires
    // from the SPELL_GO hook.
    if (castTime > 0 || instantChannel)
        Spell::CastEvents::OnRemoteCastStart(caster, spellID, endMs,
                                             instantChannel);
}

// SMSG_SPELL_START parse. Body (mirrored from nampower's SpellStartHandler):
// itemGuid(packed), casterGuid(packed), spellId(u32), castFlags(u16),
// castTime(u32). Runs from the Net::PacketDispatch funnel with the cursor
// already positioned at the body.
void ParseSpellStart(Net::CDataStore *packet) {
    Net::ReadPackedGuid(packet); // itemGuid (unused)
    const uint64_t caster = Net::ReadPackedGuid(packet);
    const int spellID = static_cast<int>(Net::Read<uint32_t>(packet));
    Net::Read<uint16_t>(packet); // castFlags (unused)
    const uint32_t castTime = Net::Read<uint32_t>(packet);
    HandleSpellStart(caster, spellID, castTime);
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
// promptly (ahead of the ~1 s-lagged +0x228 field).
void ParseChannelUpdate(Net::CDataStore *packet) {
    if (g_channel.spellID == 0)
        return;
    const uint32_t remaining = Net::Read<uint32_t>(packet);
    const int spellID = g_channel.spellID;
    if (remaining == 0) {
        g_channel.spellID = 0; // authoritative channel end
    } else {
        g_channel.endMs = NowMs() + static_cast<int>(remaining);
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
        // Cast interrupts are surfaced by PollPlayer (g_castSucceeded). Player
        // CHANNELS never fire INTERRUPTED (retail only fires CHANNEL_STOP for
        // them, interrupted or not), so there's nothing to do for a channel.
        if (g_cast.spellID == spellID)
            g_cast.spellID = 0;
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
        const int current = *reinterpret_cast<const int *>(
            static_cast<const uint8_t *>(unit) + Offsets::OFF_UNIT_CAST_SPELL);
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
        *reinterpret_cast<const int *>(Offsets::VAR_CURRENT_CAST_SPELL) == 0)
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
            const int chan = *reinterpret_cast<const int *>(
                desc + Offsets::OFF_UNIT_FIELD_CHANNEL_SPELL);
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
    Spell::CastEvents::PollPlayer(g_cast.spellID, g_cast.startMs, g_cast.delayMs,
                                  g_channel.spellID, g_channel.startMs);
    Spell::CastEvents::PollRemote();
    Spell::CastEvents::PollReticle();
}

static const Tick::WorldTick::AutoSubscribe _tickSub{&OnWorldTick};

// `CastingInfo()` — local player's cast, no token lookup.
static int __fastcall Script_CastingInfo(void *L) {
    return PushCastInfo(L, g_cast, Unit::Identity::PlayerGuid());
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
        return PushCastInfo(L, g_cast, Unit::Identity::PlayerGuid());

    const uint64_t guid = Unit::Identity::GuidForObject(u);
    const RemoteCast *rc = FindRemoteCast(guid);
    if (rc != nullptr && !rc->isChannel && NowMs() < rc->endMs)
        return PushCastInfo(L, TrackedSpell{rc->spellID, rc->startMs, rc->endMs},
                            guid);
    return 0;
}

// `ChannelInfo()` — local player's channel, no token lookup.
static int __fastcall Script_ChannelInfo(void *L) {
    return PushChannelInfo(L, g_channel.spellID, g_channel.startMs, g_channel.endMs, true);
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
        return PushChannelInfo(L, g_channel.spellID, g_channel.startMs, g_channel.endMs, true);

    auto *desc = *reinterpret_cast<const uint8_t *const *>(
        static_cast<const uint8_t *>(u) + Offsets::OFF_UNIT_DESCRIPTOR);
    if (desc == nullptr)
        return 0;
    // The live +0x228 field is authoritative for "is this unit channeling
    // right now"; the SMSG_SPELL_START cache adds real start/end times when
    // we observed the channel begin (and still matches the current spell).
    const int spellID = *reinterpret_cast<const int *>(desc + Offsets::OFF_UNIT_FIELD_CHANNEL_SPELL);
    if (spellID == 0)
        return 0;
    const RemoteCast *rc = FindRemoteCast(Unit::Identity::GuidForObject(u));
    if (rc != nullptr && rc->isChannel && rc->spellID == spellID && NowMs() < rc->endMs)
        return PushChannelInfo(L, spellID, rc->startMs, rc->endMs, /*haveTimes*/ true);
    return PushChannelInfo(L, spellID, 0, 0, /*haveTimes*/ false);
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
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::Cast
