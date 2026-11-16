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

#pragma once

#include "Offsets.h"

#include <cstdint>

namespace Spell::Known {

// True iff the local player knows `spellID`, per the engine's known-spell
// bitmap at `[VAR_PLAYER_SPELL_BITMAP]` (the bit the helper at 0x0060C740
// reads): trained abilities, talents and talent passives, racials,
// profession recipes. Rebuilt for each character at login. False for IDs
// out of range or before the bitmap exists.
bool IsKnown(uint32_t spellID);

// Calls `fn(spellID)` for every spell IsKnown is true for, in ascending ID
// order, until `fn` returns false. The bitmap covers IDs 0..spell count, so
// a pass costs one compare per 32 IDs plus one call per known spell. No-op
// before the bitmap exists.
template <class F> void ForEach(F fn) {
    const uint32_t *bitmap = *reinterpret_cast<const uint32_t *const *>(
        static_cast<uintptr_t>(Offsets::VAR_PLAYER_SPELL_BITMAP));
    if (bitmap == nullptr)
        return;
    const uint32_t count = *reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_SPELL_RECORD_COUNT));
    for (uint32_t word = 0; word <= (count >> 5); ++word) {
        for (uint32_t bits = bitmap[word]; bits != 0; bits &= bits - 1) {
            uint32_t bit = 0;
            while (((bits >> bit) & 1u) == 0)
                ++bit;
            const uint32_t spellID = (word << 5) | bit;
            if (spellID == 0 || spellID > count)
                continue;
            if (!fn(spellID))
                return;
        }
    }
}

// True iff the local player can hold a weapon in the off hand.
bool CanDualWield();

} // namespace Spell::Known
