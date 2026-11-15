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

// Turtle WoW Carnage (druid) — refresh Rip/Rake off Ferocious Bite.
//
// Carnage gives Ferocious Bite a chance to refresh the caster's Rip and Rake
// and to grant one combo point back. The chance scales with the combo points
// the Bite spent (verified in tortoise-wow spell_druid_carnage: chance =
// base * 2 * comboPoints), so only max rank at five points is near-certain.
// The refresh emits no packet and the proc applies no aura, so no trigger rule
// (`C_UnitAuras.RegisterAuraDurationModifier*`) can express it. But the granted
// point is observable: Ferocious Bite spends every point, so the first
// combo-point rise after a Bite is the proc landing. Server-side the refresh is
// caster-scoped and matched by family flags + SpellIconID (108 Rip / 494 Rake),
// and applies RefreshHolder (restore the aura's own applied duration) -- which
// is exactly what Aura::Source::RefreshDurationByFamily mirrors.
//
// The idea and the confirm-on-the-returned-point approach come from fuffc's
// PR #15; this is the C++ form. The refresh itself is
// `Aura::Source::RefreshDurationByFamily`, which restores each aura's own
// applied duration (a Rip cast at 3 combo points keeps its 3-point length) and
// re-broadcasts UNIT_AURA so aura-bar addons re-read.
//
// Gated on `Turtle::Detected()`: Carnage is Turtle-only, and the gate also
// keeps the heuristic off servers without it. Residual: if Turtle grants combo
// points off-GCD by another path (e.g. a Primal-Fury-style melee-crit talent),
// such a point inside the window can false-fire. The GCD the Bite starts
// excludes ability-based generation; an off-GCD proc is the one uncovered case.

#include "aura/Source.h"
#include "turtle/Detect.h"

#include "Offsets.h"
#include "net/PacketReader.h"
#include "net/SendObserver.h"
#include "spell/Lookup.h"
#include "tick/WorldTick.h"
#include "time/Clock.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Turtle::Carnage {

namespace {

constexpr uint64_t kRipFlag = 0x800000;
constexpr uint32_t kRipIcon = 108;
constexpr uint64_t kRakeFlag = 0x1000;
constexpr uint32_t kRakeIcon = 494;

// Ferocious Bite, identified structurally the way the server's
// IsDruidFerociousBite (spell_druid.cpp) does: druid family + the Rip/Bite
// family flag + a direct-damage effect. The damage effect is what separates
// Bite from Rip, its DoT sibling that shares the flag (Rip's effect is
// APPLY_AURA, not SCHOOL_DAMAGE). Rank-proof -- covers every rank and any
// Turtle-added one with no ID list. Verified against the client Spell.dbc:
// all five FB ranks are {family 7, flag 0x800000, Effect[0] 2}; Rip shares the
// flag but has no damage effect, and Rake carries a different flag.
bool IsFerociousBite(uint32_t spellId) {
    const uint8_t *rec = Spell::Lookup::RecordForID(static_cast<int>(spellId));
    if (!Spell::Lookup::IsFitToFamily(rec, Offsets::SPELLFAMILY_DRUID, kRipFlag))
        return false;
    // The direct-damage effect separates Bite from Rip, its DoT sibling that
    // shares the flag (Rip's effect is APPLY_AURA, not SCHOOL_DAMAGE).
    const auto *effects = reinterpret_cast<const int32_t *>(
        rec + Offsets::OFF_SPELL_RECORD_EFFECT);
    for (int i = 0; i < Offsets::SPELL_RECORD_EFFECT_COUNT; ++i)
        if (effects[i] == Offsets::SPELL_EFFECT_SCHOOL_DAMAGE)
            return true;
    return false;
}

// Long enough to absorb latency, short enough that the GCD the Bite just
// started rules out an ability as the source of the combo point.
constexpr uint32_t kWindowMs = 500;

uint64_t g_target = 0;   // unit the armed Bite's combo points / DoTs belong to
uint32_t g_untilMs = 0;  // window expiry
uint8_t g_lastCp = 0;    // last combo-point value seen since arming

using Time::Clock::NowMs;

// The CGPlayer +0xE68 sub-struct that holds combo points + combo target
// (Script_GetComboPoints, 0x0051A190). Null pre-world.
const uint8_t *ComboInfo() {
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return nullptr;
    return *reinterpret_cast<const uint8_t *const *>(
        player + Offsets::OFF_CGPLAYER_INFO);
}

// Arm on a Ferocious Bite send. The combo-target GUID is where the points live
// and where Rip/Rake sit — the authoritative unit this finisher applies to, so
// it survives a target swap in the window.
void OnSend(uint32_t opcode, Net::CDataStore *packet) {
    if (opcode != Offsets::OP_CMSG_CAST_SPELL || !Turtle::Detected())
        return;
    if (!IsFerociousBite(Net::Read<uint32_t>(packet)))
        return;
    const uint8_t *info = ComboInfo();
    if (info == nullptr)
        return;
    g_target = *reinterpret_cast<const uint64_t *>(
        info + Offsets::OFF_CGPLAYER_COMBO_TARGET);
    g_lastCp = *(info + Offsets::OFF_CGPLAYER_COMBO_POINTS); // pre-spend
    g_untilMs = NowMs() + kWindowMs;
}

// Confirm on the first combo-point CHANGE after the Bite (the tick poll stands
// in for PLAYER_COMBO_POINTS). The Bite spent every point, so a change to >0 is
// the Carnage refund; a change to 0 is the spend — stay armed for the refund
// (covers a split 5->0->1; the coalesced 5->1 fires on the single change).
void OnTick() {
    if (g_target == 0)
        return;
    if (Time::Clock::Reached(g_untilMs)) {
        g_target = 0;
        return;
    }
    const uint8_t *info = ComboInfo();
    if (info == nullptr)
        return;
    const uint8_t cp = *(info + Offsets::OFF_CGPLAYER_COMBO_POINTS);
    if (cp == g_lastCp)
        return; // no change yet
    g_lastCp = cp;
    if (cp > 0) {
        const uint64_t self = Unit::Identity::PlayerGuid();
        Aura::Source::RefreshDurationByFamily(g_target, Offsets::SPELLFAMILY_DRUID, kRipFlag,
                                              kRipIcon, self);
        Aura::Source::RefreshDurationByFamily(g_target, Offsets::SPELLFAMILY_DRUID, kRakeFlag,
                                              kRakeIcon, self);
        g_target = 0;
    }
}

const Net::SendObserver::AutoSubscribe _sendSub{&OnSend};
const Tick::WorldTick::AutoSubscribe _tickSub{&OnTick};

} // namespace

} // namespace Turtle::Carnage
