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

#include "spell/HealingAura.h"

namespace Spell::HealingAura {

namespace {

// Chained at static-init by each `AutoResolver` constructor, before
// `DllMain`. Zero-initialized ahead of any dynamic init.

} // namespace

long Resolve(int auraIndex, long amount, int32_t spirit, int32_t armor) {
    for (AutoResolver *r = AutoResolver::head; r != nullptr; r = r->next)
        if (const long v = r->fn(auraIndex, amount, spirit, armor))
            return v;
    return 0;
}

} // namespace Spell::HealingAura
