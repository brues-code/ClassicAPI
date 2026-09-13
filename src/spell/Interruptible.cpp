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

// `Spell::Interruptible` — `notInterruptible` for UnitCastingInfo /
// UnitChannelInfo.
//
// The value is RELATIVE TO THE LOCAL PLAYER: "can none of MY interrupts or
// silences stop this cast?" — a rogue and a mage read different answers for
// the same boss cast. That is the 3.3.5 client's design, decompiled on the
// Frostmourne binary and mirrored here piece by piece:
//
// 1. Capability (CGSpellBook rebuild `FUN_0053ca70`, the `param_3` block):
//    walk the player's spellbook and OR each spell's school mask into two
//    globals — spells with an INTERRUPT_CAST effect, and spells applying a
//    MOD_SILENCE aura. Both zero ⇒ the reader below never returns true.
//    (3.3.5 also keeps NPC-only variants from AttributesEx7 bits 0x800 /
//    0x1000; 1.12 has no AttributesEx7, so those masks are simply 0.)
// 2. Per-cast verdict (`FUN_007262e0`, run from the cast / channel setters
//    for the target, focus, player and pet, and again on their aura updates):
//    bail "interruptible" on AttributesEx7 CAN_ALWAYS_BE_INTERRUPTED (absent
//    in 1.12); flag "not interruptible" when PreventionType != SILENCE; else
//    strip from the two masks everything the caster is immune to — the
//    SMSG_SPELL_START immunity block (CAST_FLAG_IMMUNITY 0x04000000: a school
//    mask and a mechanic mask, stored at CGUnit +0xa64 / +0xa68 by the 0x131
//    body `FUN_00806700`) and the caster's immunity AURAS: EFFECT_IMMUNITY of
//    INTERRUPT_CAST, STATE_IMMUNITY of MOD_SILENCE, SCHOOL_IMMUNITY (its misc
//    is a school mask), MECHANIC_IMMUNITY of interrupt / silence. Not
//    interruptible when nothing survives.
// 3. Reader (`FUN_0071ab20`, what Script_UnitCastingInfo / UnitChannelInfo
//    push): the per-cast flag AND the player has any capability.
//
// What 1.12 changes:
//  - No immunity block in SMSG_SPELL_START (tortoise SendSpellStart writes
//    only 0x02 | AMMO), so a creature's `creature_template` interrupt immunity
//    is invisible — 295 of 12,701 Turtle templates, 64 of 229 rank-3 bosses
//    carry the MECHANIC_INTERRUPT bit. Documented gap in API.md; the aura part
//    of step 2 is fully available.
//  - Spell.dbc carries a single `School` column, so a spell's mask is
//    `1 << School` (the server's GetSpellSchoolMask does the same).
//  - The vanilla server's interrupt EFFECT has an extra static gate the 3.3.5
//    client never checks: tortoise and vmangos `Spell::EffectInterruptCast`
//    only stop a cast with InterruptFlags 0x02 (a channel: ChannelInterrupt-
//    Flags 0x04). Blizzard data keeps those consistent with PreventionType;
//    vanilla creature spells do not (225 timed casts have PreventionType
//    SILENCE and no 0x02 — Nefarian's Shadow Flame 0x08, Frost Breath 0). A
//    SILENCE aura ignores the flags (`Aura::HandleAuraModSilence` tests
//    PreventionType only), so the gate applies to the interrupt mask alone.
//  - A druid's interrupt lives on the TRIGGERED "Feral Charge Effect" 19675,
//    not on Feral Charge 16979 (effect TRIGGER_SPELL). 3.3.5 covered that case
//    with an AttributesEx7 bit on 16979; here the capability walk follows one
//    TRIGGER_SPELL hop instead.
//
// Evaluation is lazy — at query time, from the caster's live aura slots — rather
// than stamped at cast start and re-evaluated on aura updates like 3.3.5. Same
// answer, no extra hooks; the 3.3.5 UNIT_SPELLCAST_INTERRUPTIBLE /
// _NOT_INTERRUPTIBLE change events are not fired (yet).

#include "Game.h"
#include "Offsets.h"
#include "aura/Data.h"
#include "player/StatSignal.h"
#include "spell/Interruptible.h"
#include "spell/Lookup.h"

#include <cstdint>

namespace Spell::Interruptible {

namespace {

// School masks of what the local player can stop a cast with.
struct Capability {
    uint32_t interrupt = 0; // schools of the player's INTERRUPT_CAST spells
    uint32_t silence = 0;   // schools of the player's MOD_SILENCE spells
    bool Any() const { return (interrupt | silence) != 0; }
};

uint32_t SchoolMask(const uint8_t *rec) {
    const uint32_t school = Game::Read<uint32_t>(rec, Offsets::OFF_SPELL_RECORD_SCHOOL);
    return school < 32 ? (1u << school) : 0;
}

// ORs `rec`'s school into the capability for each stopping effect it carries.
// `followTrigger` takes one TRIGGER_SPELL hop (Feral Charge — see the header);
// the triggered spell contributes ITS school, since that is the spell that lands.
void Fold(const uint8_t *rec, Capability &cap, bool followTrigger) {
    const uint32_t mask = SchoolMask(rec);
    auto *effect = Game::Ptr<const uint32_t>(rec, Offsets::OFF_SPELL_RECORD_EFFECT);
    auto *aura = Game::Ptr<const uint32_t>(rec, Offsets::OFF_SPELL_RECORD_EFFECT_APPLY_AURA_NAME);
    auto *trigger = Game::Ptr<const int32_t>(rec, Offsets::OFF_SPELL_RECORD_EFFECT_TRIGGER_SPELL);
    for (int i = 0; i < Offsets::SPELL_RECORD_EFFECT_COUNT; ++i) {
        if (effect[i] == Offsets::SPELL_EFFECT_INTERRUPT_CAST) {
            cap.interrupt |= mask;
        } else if (effect[i] == Offsets::SPELL_EFFECT_APPLY_AURA &&
                   aura[i] == Offsets::SPELL_AURA_MOD_SILENCE) {
            cap.silence |= mask;
        } else if (followTrigger && effect[i] == Offsets::SPELL_EFFECT_TRIGGER_SPELL &&
                   trigger[i] > 0) {
            if (const uint8_t *t = Spell::Lookup::RecordForID(trigger[i]))
                Fold(t, cap, false);
        }
    }
}

// The player's capability, recomputed only when the spellbook may have changed:
// keyed on Player::StatSignal, which Spell::Learn bumps from the engine's learn /
// unlearn writers (the same invalidation the spell-knowledge caches use).
uint32_t g_epoch = 0;
bool g_valid = false;
Capability g_cap;

const Capability &PlayerCapability() {
    const uint32_t epoch = Player::StatSignal::Epoch();
    if (g_valid && epoch == g_epoch)
        return g_cap;
    Capability cap;
    const int count = Game::Read<int>(Offsets::VAR_PLAYER_SPELLBOOK_COUNT);
    for (int slot = 1; slot <= count && slot <= Offsets::SPELLBOOK_MAX_SLOTS; ++slot) {
        const int id = Spell::Lookup::SpellbookSlotToID(slot, /*player*/ 0);
        if (id <= 0)
            continue;
        if (const uint8_t *rec = Spell::Lookup::RecordForID(id))
            Fold(rec, cap, /*followTrigger*/ true);
    }
    g_cap = cap;
    g_epoch = epoch;
    g_valid = true;
    return g_cap;
}

// Strips from `cap` everything `caster`'s auras make it immune to — the 3.3.5
// aura walk of `FUN_007262e0`, over the raw descriptor slots (hidden passive
// immunities count too, so no visibility gate). Stops once nothing is left.
void StripImmunities(const uint8_t *caster, Capability &cap) {
    for (int slot = 0; slot < Offsets::UNIT_AURA_TOTAL && cap.Any(); ++slot) {
        const uint32_t id = Aura::Data::ReadSpellID(caster, slot);
        if (id == 0)
            continue;
        const uint8_t *rec = Spell::Lookup::RecordForID(static_cast<int>(id));
        if (rec == nullptr)
            continue;
        auto *aura = Game::Ptr<const uint32_t>(rec, Offsets::OFF_SPELL_RECORD_EFFECT_APPLY_AURA_NAME);
        auto *misc = Game::Ptr<const int32_t>(rec, Offsets::OFF_SPELL_RECORD_EFFECT_MISC_VALUE);
        for (int i = 0; i < Offsets::SPELL_RECORD_EFFECT_COUNT; ++i) {
            switch (aura[i]) {
            case Offsets::SPELL_AURA_EFFECT_IMMUNITY:
                if (misc[i] == Offsets::SPELL_EFFECT_INTERRUPT_CAST)
                    cap.interrupt = 0;
                break;
            case Offsets::SPELL_AURA_STATE_IMMUNITY:
                if (misc[i] == Offsets::SPELL_AURA_MOD_SILENCE)
                    cap.silence = 0;
                break;
            case Offsets::SPELL_AURA_SCHOOL_IMMUNITY:
                cap.interrupt &= ~static_cast<uint32_t>(misc[i]);
                cap.silence &= ~static_cast<uint32_t>(misc[i]);
                break;
            case Offsets::SPELL_AURA_MECHANIC_IMMUNITY:
                if (misc[i] == Offsets::MECHANIC_INTERRUPT)
                    cap.interrupt = 0;
                else if (misc[i] == Offsets::MECHANIC_SILENCE)
                    cap.silence = 0;
                break;
            default:
                break;
            }
        }
    }
}

} // namespace

bool NotInterruptible(const uint8_t *caster, const uint8_t *spellRecord, bool isChannel) {
    if (spellRecord == nullptr)
        return false;
    // Reader rule first: with no interrupt or silence known, the flag is never
    // reported — whatever the cast is.
    Capability cap = PlayerCapability();
    if (!cap.Any())
        return false;
    // Static: PreventionType is the one gate shared by the server's interrupt
    // effect, the server's silence aura and the 3.3.5 client.
    if (Game::Read<int>(spellRecord, Offsets::OFF_SPELL_RECORD_PREVENTION_TYPE) !=
        Offsets::SPELL_PREVENTION_TYPE_SILENCE)
        return true;
    if (caster != nullptr)
        StripImmunities(caster, cap);
    // The interrupt EFFECT additionally needs the cast's flag bit (server gate);
    // a silence does not.
    const uint32_t flags = Game::Read<uint32_t>(
        spellRecord, isChannel ? Offsets::OFF_SPELL_RECORD_CHANNEL_INTERRUPT_FLAGS
                               : Offsets::OFF_SPELL_RECORD_INTERRUPT_FLAGS);
    const uint32_t needed = isChannel ? Offsets::CHANNEL_FLAG_INTERRUPT
                                      : Offsets::SPELL_INTERRUPT_FLAG_DAMAGE;
    const bool interruptWorks = cap.interrupt != 0 && (flags & needed) != 0;
    const bool silenceWorks = cap.silence != 0;
    return !(interruptWorks || silenceWorks);
}

} // namespace Spell::Interruptible
