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

// The engine's macro slot lookups. A slot is 0-based (the Lua index minus 1).

#include "Offsets.h"

#include <cstdint>

namespace Macro::Slot {

// What the lookups return on a miss. Entry() rejects it like any slot out of
// range, so a by-name and a by-index path can share one validation.
constexpr uint32_t kMiss = 0xFFFFFFFFu;

// FUN_MACRO_NAME_TO_SLOT: the slot of the macro named `name`, or kMiss.
inline uint32_t ForName(const char *name) {
    using Fn = uint32_t(__fastcall *)(const char *);
    return reinterpret_cast<Fn>(Offsets::FUN_MACRO_NAME_TO_SLOT)(name);
}

// FUN_MACRO_ID_TO_SLOT: the slot of macro id `macroID`, or kMiss.
inline uint32_t ForId(uint32_t macroID) {
    using Fn = uint32_t(__fastcall *)(uint32_t);
    return reinterpret_cast<Fn>(Offsets::FUN_MACRO_ID_TO_SLOT)(macroID);
}

// FUN_MACRO_SLOT_TO_ENTRY: the macro record in `slot0`, or nullptr when the
// slot is out of range or empty.
inline uint8_t *Entry(uint32_t slot0) {
    using Fn = uint8_t *(__fastcall *)(uint32_t);
    return reinterpret_cast<Fn>(Offsets::FUN_MACRO_SLOT_TO_ENTRY)(slot0);
}

} // namespace Macro::Slot
