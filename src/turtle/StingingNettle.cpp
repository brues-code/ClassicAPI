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

// Turtle WoW "Stinging Nettle" hunter talent (talent 403; rank passives
// 51579 / 51580): Mongoose Bite and triggered fire traps apply the hunter's
// highest known Serpent Sting rank at 20% (R1) / 40% (R2) of its duration.
//
// Server-side (tortoise-wow spell_hunter.cpp ApplyStingingNettle) the sting
// is a direct `target->AddAura(...)` — no cast, no SMSG_SPELL_GO for the
// sting itself — so the client sees only the descriptor application and
// OnAuraAdded would seat it caster-less at the full 15s base. The TRIGGERS
// are visible, though: Mongoose Bite is a real player cast, and the
// fire-trap effect spells are cast by the trap's OWNER (GameObject::Use
// TYPE_TRAP → pOwner->CastSpell) with non-zero SpellVisual, so they pass
// IsNeedSendToClient and their SMSG_SPELL_GO broadcasts with the hunter as
// caster and the victim in the hit list. Registered as Aura::Source
// triggered-application rules: the trigger arms its hit targets, and the
// caster-less Serpent Sting application that follows consumes the arm
// (player attribution + the server's max(1, base × pct / 100)).
//
// The rank gates are the talent-granted passives, read live from the
// spell-knowledge bitmap at trigger time (respec-safe); R2 registers first
// so its 40% wins while known. The percents come from the rank spells' own
// EffectBasePoints[0] + 1 — the same DBC value the server's
// CalculateSimpleValue reads (19 → 20%, 39 → 40%), with the server's own
// 20/40 fallback — so a Turtle rebalance that patches the client DBC tracks
// automatically.
//
// Server guards that mirror for free: no talent → no arm (gate fails, and
// the server applies nothing); an existing LONGER sting → the server skips
// AddAura entirely → no application event → the arm just expires. A nettle
// re-apply over a SHORTER existing sting reuses the descriptor slot (no
// OnAuraAdded, verified in-game with chained Mongoose Bites), so the arm
// mechanism additionally refreshes the existing cached entry at trigger
// time, mirroring the server's keep-the-longer rule — see
// RefreshTriggeredExisting in aura/Source.cpp. Other hunters' nettle stings
// stay at base duration (their talents aren't client-knowable).
//
// Gated on Turtle::Detected() via a WorldTick latch, like DurationMods —
// the trigger spells exist on stock 1.12, but the mechanic is Turtle's.

#include "Offsets.h"
#include "aura/Source.h"
#include "spell/Lookup.h"
#include "tick/WorldTick.h"
#include "turtle/Detect.h"

#include <cstdint>

namespace Turtle::StingingNettle {

namespace {

// Serpent Sting's SpellFamilyFlags bit — all player ranks carry it (1978,
// 13549..13555, 25295, and Turtle's added Rank 9, 33459; client DBC
// verified).
constexpr uint64_t kSerpentStingFlag = 0x4000;

constexpr uint32_t kNettleR1 = 51579; // 20%
constexpr uint32_t kNettleR2 = 51580; // 40%

// The rank's duration percent: its record's EffectBasePoints[0] + 1 (the
// server's CalculateSimpleValue), else the server's hardcoded fallback.
int32_t NettlePct(uint32_t rankSpellId, int32_t fallbackPct) {
    const uint8_t *rec =
        Spell::Lookup::RecordForID(static_cast<int>(rankSpellId));
    if (rec == nullptr)
        return fallbackPct;
    const int32_t pct =
        *reinterpret_cast<const int32_t *>(
            rec + Offsets::OFF_SPELL_RECORD_EFFECT_BASE_POINTS) +
        1;
    return pct > 0 ? pct : fallbackPct;
}

// Fire-trap effects' SpellFamilyFlags bit. Verified exhaustively against the
// client Spell.dbc: bit 0x4 in the hunter family selects EXACTLY the eight
// spells Turtle's script binding lists (Immolation Trap Effect r1-5,
// Explosive Trap Effect r1-3) and nothing else — so the mask form is
// rank-proof, covering any future trap rank for free. (Freezing Trap is the
// distinct bit 0x8.)
constexpr uint64_t kFireTrapEffectFlag = 0x4;

// Mongoose Bite ranks must stay EXACT IDs: their family bit (0x2) is shared
// with all eight Raptor Strike ranks — verified against the DBC — and Raptor
// Strike does NOT trigger nettle server-side. Mask-matching it would keep a
// melee-weaving hunter's target permanently armed.
constexpr uint32_t kMongooseBite[] = {1495, 14269, 14270, 14271};

bool g_registered = false;

void RegisterAll() {
    const int32_t pctR2 = NettlePct(kNettleR2, 40);
    const int32_t pctR1 = NettlePct(kNettleR1, 20);
    // Higher rank first — rules matching the same trigger arm in
    // registration order, first live gate wins.
    Aura::Source::AddTriggeredApplicationByFamily(
        Offsets::SPELLFAMILY_HUNTER, kFireTrapEffectFlag, kNettleR2, Offsets::SPELLFAMILY_HUNTER, kSerpentStingFlag,
        pctR2);
    Aura::Source::AddTriggeredApplicationByFamily(
        Offsets::SPELLFAMILY_HUNTER, kFireTrapEffectFlag, kNettleR1, Offsets::SPELLFAMILY_HUNTER, kSerpentStingFlag,
        pctR1);
    for (uint32_t id : kMongooseBite) {
        Aura::Source::AddTriggeredApplication(id, kNettleR2, Offsets::SPELLFAMILY_HUNTER,
                                              kSerpentStingFlag, pctR2);
        Aura::Source::AddTriggeredApplication(id, kNettleR1, Offsets::SPELLFAMILY_HUNTER,
                                              kSerpentStingFlag, pctR1);
    }
}

void OnTick() {
    if (g_registered || !Turtle::Detected())
        return;
    RegisterAll();
    g_registered = true;
}

const Tick::WorldTick::AutoSubscribe _tickSub{&OnTick};

} // namespace

} // namespace Turtle::StingingNettle
