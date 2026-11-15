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

// Turtle WoW "Dark Harvest" (warlock channel; ranks 52550/52551/52552, family
// bit 0x4000000000): "While channeling, the time between periodic ticks of
// your Affliction spells on the target is reduced by 30%." The live server
// preserves each dot's TICK COUNT, so the faster ticks consume the dot's
// remaining time 30% faster while the channel aura is up — it falls off early
// by 0.3 × the overlap with NO client-visible aura change (live report: dots
// gone with ~3 s still showing after a full 8 s channel = 2.4 s + tick
// rounding; Cursive's curses.lua ships the same `remaining -= 0.30 × overlap`
// community calibration). tortoise-wow's recreation (spell_warlock.cpp
// `spell_warlock_dark_harvest`, added 2026-08-25) only re-times the tick
// timers via SetPeriodicTimer and leaves holder duration alone — that predicts
// NO early fall-off, contradicting live, so the compression math here is
// deliberately NOT mirrored from that source.
//
// What IS taken from tortoise-wow (`IsDarkHarvestAfflictionPeriodicAura`): the
// affected set — the caster's own SPELLFAMILY_WARLOCK periodic auras matching
// Corruption (0x2), Drain Life (0x8), Curse of Agony (0x400), Drain Soul
// (0x4000), Siphon Life (0x1'00000000), Curse of Doom (0x2'00000000). The
// drains can't overlap the channel in practice (one channel per caster), so
// the live set is the curses + Corruption + Siphon Life. Client DBC verified:
// all three DH ranks carry famName 5 / famFlags 0x4000000000, so the
// family-mask trigger is rank-proof.
//
// The percent comes from the rank record's EffectBasePoints[1] + 1 (the dummy
// effect the tooltip's $s2 prints: -31 → 30%), with the community-calibrated
// 30 as fallback — a Turtle rebalance that patches the client DBC tracks
// automatically. All ranks carry the same -31 today, so rank 1 is read.
//
// Gated on Turtle::Detected() via a WorldTick latch, like StingingNettle —
// the spells only exist in Turtle's DBC, so the rule is inert elsewhere
// anyway.

#include "Offsets.h"
#include "aura/Source.h"
#include "spell/Lookup.h"
#include "tick/WorldTick.h"
#include "turtle/Detect.h"

#include <cstdint>

namespace Turtle::DarkHarvest {

namespace {

constexpr uint64_t kDarkHarvestFlag = 0x4000000000ULL;
constexpr uint64_t kAfflictionMask = 0x2ULL | 0x8ULL | 0x400ULL | 0x4000ULL |
                                     0x100000000ULL | 0x200000000ULL;
constexpr uint32_t kDarkHarvestR1 = 52550;
constexpr int32_t kFallbackPct = 30;

int32_t HarvestPct() {
    const uint8_t *rec =
        Spell::Lookup::RecordForID(static_cast<int>(kDarkHarvestR1));
    if (rec == nullptr)
        return kFallbackPct;
    // EffectBasePoints[1] (the -30% dummy effect; index 0 is the tick damage).
    const int32_t pct =
        *reinterpret_cast<const int32_t *>(
            rec + Offsets::OFF_SPELL_RECORD_EFFECT_BASE_POINTS + 4) +
        1;
    return pct < 0 ? -pct : kFallbackPct;
}

bool g_registered = false;

void OnTick() {
    if (g_registered || !Turtle::Detected())
        return;
    Aura::Source::AddTickCompression(Offsets::SPELLFAMILY_WARLOCK, kDarkHarvestFlag, Offsets::SPELLFAMILY_WARLOCK,
                                     kAfflictionMask, HarvestPct());
    g_registered = true;
}

const Tick::WorldTick::AutoSubscribe _tickSub{&OnTick};

} // namespace

} // namespace Turtle::DarkHarvest
