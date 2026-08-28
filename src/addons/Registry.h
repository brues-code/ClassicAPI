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
#include <type_traits>

namespace Addons {

// Walk the engine's intrusive AddOn-registry linked list, calling `fn(entry)`
// for each `AddOnEntry *`. Head at `[VAR_ADDON_LIST_HEAD]`; the next-pointer
// field is at `entry + [VAR_ADDON_LIST_CTRL] + 4` (= `entry + 0x10`); a
// low-bit-1 sentinel or NULL terminates. This is the same traversal the
// engine's own load pass (`FUN_0051F600`) and scan tail loop use.
//
// `fn` may return `void` (always continue) or `bool` (return `false` to STOP
// early). Early stop is load-bearing for a visitor that mutates the list —
// e.g. re-linking an entry to the head invalidates the current next-pointer,
// so the walk must not follow it. Shared by `Addons::Embedded` (find + re-link
// the embedded entry) and `Addons::Rescan` (snapshot / diff / fix-up passes).
template <typename Fn> void ForEachEntry(Fn fn) {
    const int linkOffset = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_ADDON_LIST_CTRL));
    uintptr_t entry = *reinterpret_cast<const uintptr_t *>(
        static_cast<uintptr_t>(Offsets::VAR_ADDON_LIST_HEAD));
    while ((entry & 1) == 0 && entry != 0) {
        if constexpr (std::is_void_v<decltype(fn(entry))>) {
            fn(entry);
        } else {
            if (!fn(entry))
                return;
        }
        entry = *reinterpret_cast<const uintptr_t *>(entry + linkOffset + 4);
    }
}

} // namespace Addons
