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

#include "spell/Mod.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "unit/Identity.h"

namespace Spell::Mod {

namespace {

// The local player's class SpellFamilyName, or 0 if it can't be resolved
// (e.g. at the login screen). Gates the SpellMod family match — only
// spells of the player's own class family receive the player's mods, so
// a same-flag-bit spell of another family isn't falsely modified.
uint32_t PlayerSpellFamily() {
    const uint8_t *desc = Unit::Identity::PlayerDescriptor();
    if (desc == nullptr)
        return 0;
    const uint8_t classByte = Unit::Identity::Class(desc);
    const uint8_t *cls = DBC::Record(Offsets::VAR_CHRCLASSES_RECORDS,
                                     Offsets::VAR_CHRCLASSES_COUNT, classByte);
    if (cls == nullptr)
        return 0;
    return Game::Read<uint32_t>(cls, Offsets::OFF_CHRCLASSES_SPELL_FAMILY);
}

} // namespace

float Apply(const uint8_t *spellRecord, int op, float base) {
    if (spellRecord == nullptr)
        return base;

    const uint32_t familyName = Game::Read<uint32_t>(
        spellRecord, Offsets::OFF_SPELL_RECORD_FAMILY_NAME);
    const uint32_t attrEx3 = Game::Read<uint32_t>(
        spellRecord, Offsets::OFF_SPELL_RECORD_ATTRIBUTES_EX3);
    if (familyName == 0 ||
        (attrEx3 & Offsets::SPELL_ATTR_EX3_IGNORE_CASTER_MODIFIERS) != 0)
        return base;
    if (familyName != PlayerSpellFamily())
        return base;

    const uint64_t familyFlags = Game::Read<uint64_t>(
        spellRecord, Offsets::OFF_SPELL_RECORD_FAMILY_FLAGS);
    if (familyFlags == 0)
        return base;

    const auto *flatTable = reinterpret_cast<const uint8_t *>(Offsets::VAR_SPELLMOD_FLAT_TABLE);
    const auto *pctTable = reinterpret_cast<const uint8_t *>(Offsets::VAR_SPELLMOD_PCT_TABLE);
    const int opByteOffset = op * 4;

    int flat = 0;
    int pct = 0;
    for (int slot = 0; slot < Offsets::SPELLMOD_SLOT_COUNT; ++slot) {
        if (((familyFlags >> slot) & 1ull) == 0)
            continue;
        const int byteOff = slot * Offsets::SPELLMOD_SLOT_STRIDE + opByteOffset;
        flat += *reinterpret_cast<const int *>(flatTable + byteOff);
        pct += *reinterpret_cast<const int *>(pctTable + byteOff);
    }
    if (flat == 0 && pct == 0)
        return base;

    int pctTotal = pct + 100;
    if (pctTotal < 0)
        pctTotal = 0;
    return (base + static_cast<float>(flat)) * static_cast<float>(pctTotal) * 0.01f;
}

} // namespace Spell::Mod
