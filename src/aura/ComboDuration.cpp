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

// See ComboDuration.h for the contract and the protocol findings that
// motivate the design (the server never transmits target-debuff
// durations in 1.12).

#include "ComboDuration.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "net/PacketReader.h"
#include "net/SendObserver.h"
#include "spell/Mod.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Aura::ComboDuration {

namespace {

using Net::CDataStore;

uint32_t NowMs() {
    using TickCount_t = uint32_t(__fastcall *)();
    return reinterpret_cast<TickCount_t>(
        static_cast<uintptr_t>(Offsets::FUN_OS_TICKCOUNT_MS))();
}

// ---- Combo-point snapshot (NetClient send co-hook) ------------------------

// The player's live combo points, read the same way Script_GetComboPoints
// (0x0051A190) does: the CP byte on the CGPlayer +0xE68 sub-struct. The player
// comes from the shared Unit::Identity::PlayerObject() (the object-manager GUID
// resolve, our fastest player-object path — returns null pre-world rather than
// throwing), so this stays safe from any context the send hook can fire in.
uint8_t CurrentComboPoints() {
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return 0;
    const uint8_t *info = *reinterpret_cast<const uint8_t *const *>(
        player + Offsets::OFF_CGPLAYER_INFO);
    if (info == nullptr)
        return 0;
    return *(info + Offsets::OFF_CGPLAYER_COMBO_POINTS);
}

// Keyed by spellID, because a finisher's own SMSG_SPELL_GO is not reliably
// the next one to arrive after its cast. A talent that procs an aura on the
// finisher (Turtle's rogue Taste for Blood casts spell 52529 on the player)
// lands its SPELL_GO first, and every player-cast aura spell reaches
// TryComboScaledMs. A single slot -- or a lookup that cleared the slot on a
// spellID mismatch -- therefore lost the snapshot to the proc, and the
// finisher fell back to its 0-CP base duration. Only the matching entry is
// ever consumed; a miss leaves every other capture intact.
struct Capture {
    uint32_t spellId;
    uint8_t comboPoints;
    uint32_t capturedMs;
};
constexpr int kCaptureCount = 8;
Capture g_captures[kCaptureCount];
int g_captureCursor = 0;

// Worst-case send -> SMSG_SPELL_GO roundtrip. Past this the snapshot is
// stale (the cast failed / was rejected) and must not scale a later cast.
constexpr uint32_t kCaptureTtlMs = 2000;

void RememberCapture(uint32_t spellId, uint8_t comboPoints, uint32_t nowMs) {
    for (auto &c : g_captures) {
        if (c.spellId == spellId) { // re-cast before the first resolved
            c.comboPoints = comboPoints;
            c.capturedMs = nowMs;
            return;
        }
    }
    g_captures[g_captureCursor] = {spellId, comboPoints, nowMs};
    g_captureCursor = (g_captureCursor + 1) % kCaptureCount;
}

int ConsumeCapturedCP(uint32_t spellId) {
    for (auto &c : g_captures) {
        if (c.spellId != spellId)
            continue;
        const Capture got = c;
        c = {0, 0, 0};
        if (got.comboPoints == 0)
            return 0;
        if (NowMs() - got.capturedMs >= kCaptureTtlMs)
            return 0;
        return got.comboPoints;
    }
    return 0;
}

// Co-hook on the NetClient send — the one funnel every outgoing CMSG
// passes through, including casts released from nampower's spell queue
// (which bypass any co-hook on Spell_C_CastSpell itself via trampoline
// re-entry, but still build and send CMSG_CAST_SPELL through the engine).
// Snapshot the combo points at the moment the cast request leaves: the
// server consumes them before SMSG_SPELL_GO arrives, so reading at
// SpellGo time is too late. We watch sends through the shared
// `Net::SendObserver` rather than owning the funnel hook ourselves.
void OnSend(uint32_t opcode, Net::CDataStore *packet) {
    if (opcode != Offsets::OP_CMSG_CAST_SPELL)
        return;
    const uint32_t spellId = Net::Read<uint32_t>(packet);
    if (spellId != 0)
        RememberCapture(spellId, CurrentComboPoints(), NowMs());
}

const Net::SendObserver::AutoSubscribe _sendSub{&OnSend};

// ---- Duration data (SpellDuration.dbc + Lua overrides) --------------------

// Lua-registered overrides for spells whose client DBC row is missing or
// wrong. Turtle's reworked Rip is the motivating case: its DurationIndex
// dangles (records[87] == NULL in Turtle's SpellDuration.dbc) because the
// rework lives entirely server-side, so the values must come from config.
struct Override {
    uint32_t spellId;
    int32_t baseMs;
    int32_t maxMs;
};
constexpr int kMaxOverrides = 64;
Override g_overrides[kMaxOverrides];
int g_overrideCount = 0;

const Override *FindOverride(uint32_t spellId) {
    for (int i = 0; i < g_overrideCount; ++i) {
        if (g_overrides[i].spellId == spellId)
            return &g_overrides[i];
    }
    return nullptr;
}

// Resolver chain for dangling-duration-row spells (client DBC missing the
// row). The KNOWN VALUES themselves are server-custom data and live in
// their own module (e.g. `src/turtle/ComboDuration.cpp` for Turtle's
// reworked Rip); this stock module only owns the mechanism and the
// dangling-condition gate. Chained at static-init by `AutoDanglingResolver`.
AutoDanglingResolver *g_danglingResolvers = nullptr;

bool ResolveDangling(uint32_t spellId, int32_t *baseMs, int32_t *maxMs) {
    for (AutoDanglingResolver *r = g_danglingResolvers; r != nullptr;
         r = r->next)
        if (r->fn(spellId, baseMs, maxMs))
            return true;
    return false;
}

// `C_UnitAuras.RegisterComboDuration(spellID, baseSeconds, maxSeconds)`
// -> true on success. Re-registering a spellID replaces its entry.
// Values are in seconds to match the C_UnitAuras duration convention;
// the formula applied is the server's own:
// `base + (max - base) * comboPoints / 5`.
int __fastcall Script_RegisterComboDuration(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2) ||
        !Game::Lua::IsNumber(L, 3)) {
        Game::Lua::Error(
            L, "Usage: C_UnitAuras.RegisterComboDuration(spellID, "
               "baseSeconds, maxSeconds)");
        return 0;
    }
    const auto spellId = static_cast<uint32_t>(Game::Lua::ToNumber(L, 1));
    const auto baseMs = static_cast<int32_t>(Game::Lua::ToNumber(L, 2) * 1000.0);
    const auto maxMs = static_cast<int32_t>(Game::Lua::ToNumber(L, 3) * 1000.0);
    if (spellId == 0 || baseMs < 0 || maxMs <= 0) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    for (int i = 0; i < g_overrideCount; ++i) {
        if (g_overrides[i].spellId == spellId) {
            g_overrides[i] = {spellId, baseMs, maxMs};
            Game::Lua::PushBool(L, true);
            return 1;
        }
    }
    if (g_overrideCount >= kMaxOverrides) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    g_overrides[g_overrideCount++] = {spellId, baseMs, maxMs};
    Game::Lua::PushBool(L, true);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_UnitAuras", "RegisterComboDuration",
                                     &Script_RegisterComboDuration);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

AutoDanglingResolver::AutoDanglingResolver(DanglingResolver f)
    : fn(f), next(g_danglingResolvers) {
    g_danglingResolvers = this;
}

uint32_t TryComboScaledMs(const uint8_t *spellRecord, uint32_t spellId) {
    if (spellRecord == nullptr || spellId == 0)
        return 0;
    const int cp = ConsumeCapturedCP(spellId);
    if (cp <= 0 || cp > 5)
        return 0;

    int32_t baseMs = 0;
    int32_t maxMs = 0;
    if (const Override *o = FindOverride(spellId)) {
        baseMs = o->baseMs;
        maxMs = o->maxMs;
    } else {
        const int idx = *reinterpret_cast<const int *>(
            spellRecord + Offsets::OFF_SPELL_RECORD_DURATION_INDEX);
        const uint8_t *row =
            DBC::Record(Offsets::VAR_SPELL_DURATION_RECORDS,
                        Offsets::VAR_SPELL_DURATION_COUNT,
                        static_cast<uint32_t>(idx));
        if (row != nullptr) {
            baseMs = *reinterpret_cast<const int32_t *>(row + 0x4);
            maxMs = *reinterpret_cast<const int32_t *>(row + 0xC);
        } else if (idx != 0) {
            // Dangling duration index — the client data is broken for this
            // spell. Defer to a registered resolver (the known values are
            // server-custom data owned by a platform module, e.g.
            // src/turtle for Turtle's reworked Rip).
            if (!ResolveDangling(spellId, &baseMs, &maxMs))
                return 0;
        } else {
            return 0; // no duration row at all
        }
    }

    // The server scales only when the row has a combo range: base != max
    // ((v)mangos CalculateSpellDuration). base < 0 is the "infinite"
    // sentinel; fixed-duration rows (base == max) take the regular path.
    if (baseMs < 0 || maxMs <= 0 || baseMs == maxMs)
        return 0;

    int32_t durationMs = baseMs + (maxMs - baseMs) * cp / 5;
    if (durationMs <= 0)
        return 0;

    // Player duration SpellMods apply on top of the combo-scaled value,
    // matching the server's order (scale, then ApplySpellMod).
    durationMs = static_cast<int32_t>(Spell::Mod::Apply(
        spellRecord, Offsets::SPELLMOD_OP_DURATION,
        static_cast<float>(durationMs)));
    return durationMs > 0 ? static_cast<uint32_t>(durationMs) : 0;
}

} // namespace Aura::ComboDuration
