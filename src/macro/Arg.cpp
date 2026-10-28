// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

#include "macro/Arg.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Macro::Arg {

int Slot(void *L) {
    using NameToSlot_t = uint32_t(__fastcall *)(const char *name);
    using SlotToEntry_t = const uint8_t *(__fastcall *)(uint32_t slot0);
    uint32_t slot0 = 0xFFFFFFFFu;
    if (Game::Lua::IsNumber(L, 1))
        slot0 = static_cast<uint32_t>(static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1);
    else if (Game::Lua::IsString(L, 1))
        slot0 = reinterpret_cast<NameToSlot_t>(Offsets::FUN_MACRO_NAME_TO_SLOT)(
            Game::Lua::ToString(L, 1));
    if (reinterpret_cast<SlotToEntry_t>(Offsets::FUN_MACRO_SLOT_TO_ENTRY)(slot0) == nullptr)
        return 0;
    return static_cast<int>(slot0) + 1;
}

} // namespace Macro::Arg
