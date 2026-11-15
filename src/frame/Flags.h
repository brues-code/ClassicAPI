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

// A frame's flag word (OFF_FRAME_FLAGS, the FRAME_FLAG_* bits).
namespace Frame::Flags {

// True when any bit of `mask` is set.
inline bool Has(const void *frame, uint32_t mask) {
    return (*reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(frame) +
                                                Offsets::OFF_FRAME_FLAGS) &
            mask) != 0;
}

// True when every bit of `mask` is set.
inline bool HasAll(const void *frame, uint32_t mask) {
    return (*reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(frame) +
                                                Offsets::OFF_FRAME_FLAGS) &
            mask) == mask;
}

// Sets or clears `mask` through CSimpleFrame::SetFrameFlag
// (FUN_FRAME_SET_FRAME_FLAG, `__thiscall(frame, mask, on)`, `ret 8`), the
// function every write of the word goes through.
inline void Set(void *frame, uint32_t mask, bool on) {
    using Fn = void(__thiscall *)(void *, uint32_t, int);
    reinterpret_cast<Fn>(Offsets::FUN_FRAME_SET_FRAME_FLAG)(frame, mask, on ? 1 : 0);
}

} // namespace Frame::Flags
