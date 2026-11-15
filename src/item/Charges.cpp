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

#include "Charges.h"

#include "../Game.h"
#include "../Offsets.h"
#include "CGItem.h"

#include <cstdlib>

namespace Item::Charges {

int PushChargesForItem(void *L, const uint8_t *item) {
    if (item == nullptr)
        return 0;
    auto *descriptor = Item::ObjectFields(item);
    if (descriptor == nullptr)
        return 0;
    const int32_t stack = static_cast<int32_t>(Item::StackCount(item));
    const int32_t rawCharges = *reinterpret_cast<const int32_t *>(
        descriptor + Offsets::OFF_DESCRIPTOR_SPELL_CHARGES_0);
    const int32_t usesPerItem = (rawCharges < 0) ? -rawCharges : 1;
    Game::Lua::PushNumber(L, static_cast<double>(stack * usesPerItem));
    return 1;
}

} // namespace Item::Charges
