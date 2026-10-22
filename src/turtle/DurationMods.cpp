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

// Turtle WoW server-side aura duration mods that the client can't hear.
// These mirror server mechanics that change a DoT's remaining time on another
// unit with no client-visible packet (verified in tortoise-wow: the edit goes
// through SetAuraDuration / RefreshHolder, which notifies only the aura's
// target-if-a-player, never the observing caster). We can't hear the change,
// but we see the triggering cast's SMSG_SPELL_GO, so Aura::Source mirrors the
// edit on the cached entry. A rule is a { exact trigger spellID -> affected
// aura by SpellFamilyName plus a family-flag overlap and/or a SpellIconID }
// pair, applied by Aura::Source::ApplyDurationModifiers.
//
//   Conflagrate   -> Immolate:       shave 3s   (Turtle keeps Immolate ticking, -3s;
//                                                stock's full-consume removes it, which
//                                                OnAuraRemoved evicts, so the rule is
//                                                harmless there)
//   Molten Blast  -> Flame Shock:    refresh    (RefreshHolder -> reset to full)
//   Bestial Wrath -> Scent of Blood: set to Bestial Wrath's own duration
//
// Gated on Turtle::Detected() and registered once (a WorldTick latch, since the
// gate reads a FrameXML global only present in-world). Stock's Conflagrate
// still exists, so keeping the gate preserves the previous Lua behaviour
// exactly; Molten Blast's spell IDs don't exist off Turtle anyway.

#include "Offsets.h"
#include "aura/Source.h"
#include "spell/Lookup.h"
#include "turtle/Detect.h"

#include "tick/WorldTick.h"

#include <cstdint>

namespace Turtle::DurationMods {

namespace {

// SpellFamilyName + family-flag bits of the affected auras (SpellClassMask):
// Immolate is warlock CF_WARLOCK_IMMOLATE (bit 2); Flame Shock is shaman
// CF_SHAMAN_FLAME_SHOCK (bit 28).
constexpr uint32_t kWarlock = 5;
constexpr uint32_t kShaman = 11;
constexpr uint64_t kImmolateFlag = 0x4;
constexpr uint64_t kFlameShockFlag = 0x10000000;

// Bestial Wrath -> Scent of Blood (hunter). The talent's proc puts Scent of
// Blood on the pet for its own 8s; casting Bestial Wrath then stretches that
// SAME holder to Bestial Wrath's length. Server-side (tortoise-wow
// spell_hunter_bestial_wrath) it is a bare
// `holder->SetAuraMaxDuration/SetAuraDuration` on the pet's existing holder —
// both are plain field writes that send nothing, and the one duration packet
// 1.12 has is scoped to an aura-bearer that is a PLAYER, which a pet is not.
// So the client keeps showing the 8s it computed at application and the timer
// simply runs out under a buff that is still up.
//
// The trigger is visible, though, and Bestial Wrath's implicit target is
// TARGET_UNIT_CASTER_PET, so the pet is in its SMSG_SPELL_GO hit list and the
// standard rule machinery reaches the right unit. If the pet has no Scent of
// Blood the server returns without doing anything; we mirror that for free,
// since no cache entry means no match.
//
// Selected by ICON, not by family flags: every Turtle custom spell carries
// SpellFamilyFlags of 0, so no mask can name this aura, while icon 2245 picks
// out exactly the four Scent of Blood records in the hunter family and nothing
// else (client DBC verified) — and it keeps picking them if Turtle adds a rank.
constexpr uint32_t kHunter = 9;
constexpr uint32_t kBestialWrath = 19574;
constexpr uint32_t kScentOfBloodIcon = 2245;
// The server hardcodes 18s; Bestial Wrath's own DBC duration agrees, so we
// read the DBC and keep this only for a record we can't resolve.
constexpr int32_t kBestialWrathFallbackMs = 18000;

// Bestial Wrath's own base duration, straight from the engine's duration
// helper (`skipMod` = base: the server applies no caster mods to the value it
// writes). Reading it instead of hardcoding means a Turtle rebalance that
// patches the client's spell data carries over on its own.
int32_t BestialWrathDurationMs() {
    const uint8_t *rec =
        Spell::Lookup::RecordForID(static_cast<int>(kBestialWrath));
    if (rec == nullptr)
        return kBestialWrathFallbackMs;
    using GetDuration_t = int(__fastcall *)(const uint8_t *spellRecord,
                                            int pet, char skipMod);
    const int ms = reinterpret_cast<GetDuration_t>(
        static_cast<uintptr_t>(Offsets::FUN_GET_SPELL_DURATION))(
        rec, 0, /*skipMod*/ 1);
    return ms > 0 ? ms : kBestialWrathFallbackMs;
}

bool g_registered = false;

void RegisterAll() {
    constexpr uint32_t kConflagrate[] = {17962, 18930, 18931, 18932};
    for (uint32_t id : kConflagrate)
        Aura::Source::AddDurationMod(id, kWarlock, kImmolateFlag, /*icon*/ 0,
                                     Aura::Source::DURATION_MOD_REDUCE,
                                     /*valueMs*/ 3000);

    constexpr uint32_t kMoltenBlast[] = {36916, 36917, 36918,
                                         36919, 36920, 36921};
    for (uint32_t id : kMoltenBlast)
        Aura::Source::AddDurationMod(id, kShaman, kFlameShockFlag, /*icon*/ 0,
                                     Aura::Source::DURATION_MOD_REFRESH,
                                     /*valueMs*/ 0);

    Aura::Source::AddDurationMod(kBestialWrath, kHunter, /*mask*/ 0,
                                 kScentOfBloodIcon,
                                 Aura::Source::DURATION_MOD_SET,
                                 BestialWrathDurationMs());
}

void OnTick() {
    if (g_registered || !Turtle::Detected())
        return;
    RegisterAll();
    g_registered = true;
}

const Tick::WorldTick::AutoSubscribe _tickSub{&OnTick};

} // namespace

} // namespace Turtle::DurationMods
