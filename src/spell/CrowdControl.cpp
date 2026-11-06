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

#include "spell/CrowdControl.h"

#include "Offsets.h"
#include "spell/Lookup.h"

namespace Spell::CrowdControl {

const char *Classify(const uint8_t *rec) {
    if (rec == nullptr)
        return nullptr;
    auto has = [rec](int type) { return Spell::Lookup::HasAuraEffect(rec, type); };
    if (has(Offsets::SPELL_AURA_MOD_POSSESS)) return "POSSESS";
    if (has(Offsets::SPELL_AURA_MOD_CHARM)) return "CHARM";
    if (has(Offsets::SPELL_AURA_MOD_STUN)) return "STUN";
    if (has(Offsets::SPELL_AURA_MOD_FEAR)) return "FEAR";
    if (has(Offsets::SPELL_AURA_MOD_CONFUSE)) return "CONFUSE";
    if (has(Offsets::SPELL_AURA_MOD_PACIFY_SILENCE)) return "PACIFYSILENCE";
    if (has(Offsets::SPELL_AURA_MOD_SILENCE)) return "SILENCE";
    if (has(Offsets::SPELL_AURA_MOD_PACIFY)) return "PACIFY";
    if (has(Offsets::SPELL_AURA_MOD_ROOT)) return "ROOT";
    if (has(Offsets::SPELL_AURA_MOD_DISARM)) return "DISARM";
    return nullptr;
}

bool IsCrowdControl(uint32_t spellID) {
    const uint8_t *rec = Spell::Lookup::RecordForID(static_cast<int>(spellID));
    if (rec == nullptr)
        return false;
    // Crowd control is broader than loss of control: it also counts movement
    // slows / snares, which `Classify` (the LoC set) deliberately excludes.
    if (Classify(rec) != nullptr)
        return true;
    return Spell::Lookup::HasAuraEffect(rec, Offsets::SPELL_AURA_MOD_DECREASE_SPEED);
}

} // namespace Spell::CrowdControl
