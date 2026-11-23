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
#include "storm/List.h"

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
// early, e.g. once a by-name search finds its entry). A visitor must not
// unlink the entry it is given and then continue: the walk reads that
// entry's next-pointer after the call. Shared by `Addons::Embedded` (find the
// embedded entry) and `Addons::Rescan` (snapshot / diff / fix-up passes).
template <typename Fn> void ForEachEntry(Fn fn) {
    const int linkOffset = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_ADDON_LIST_CTRL));
    uintptr_t entry = *reinterpret_cast<const uintptr_t *>(
        static_cast<uintptr_t>(Offsets::VAR_ADDON_LIST_HEAD));
    while (Storm::IsNode(entry)) {
        if constexpr (std::is_void_v<decltype(fn(entry))>) {
            fn(entry);
        } else {
            if (!fn(entry))
                return;
        }
        entry = *reinterpret_cast<const uintptr_t *>(entry + linkOffset + 4);
    }
}

// Resolves Lua arg 1 (index or name) to the addon's registry record, with
// the arg handling of stock `Script_GetAddOnDependencies` (`0x0048E5E0`) —
// the same shape 3.3.5 gives every `(index or "name")` addon global:
// `lua_isnumber` first (so a numeric string like `"3"` is an index), an
// out-of-range index raises the engine's own range error with the addon
// count, anything else non-string raises `usage`, and an unknown *name*
// returns nullptr with no error.
const uint8_t *ResolveRecord(void *L, const char *usage);

// The argument reader every C_AddOns function shares, as 3.80.2's
// FUN_140b281f0 reads it. A number is an index: outside 1 to the count it
// raises `bad argument #1 to '<fn>' (Invalid index %d. AddOn index must be
// in the range of 1 to %d - <usage>)`. A string is a name, taken as given:
// an unknown name is no error and returns nullptr. Anything else, nil
// included, raises `bad argument #1 to '<fn>' (<usage>)`. `*outName` gets the
// name the caller asked for: the registered name for an index, the argument
// string for a name (valid while the argument is on the stack).
const uint8_t *ResolveAddOnArg(void *L, const char *usage, const char **outName);

// The record of the addon named `name` (any case), or nullptr.
const uint8_t *FindRecord(const char *name);

// The registered name of a record ResolveAddOnArg returned.
const char *RecordName(const uint8_t *record);

// The engine's security index of the addon named `name`
// (FUN_ADDON_GET_SECURITY_INDEX): 0 SECURE, 1 INSECURE, 2 BANNED, indexing
// VAR_ADDON_SECURITY_TABLE.
int SecurityIndex(const char *name);

// An optional character argument at `idx`, as 3.80.2 reads it
// (FUN_140a6dc70, then its name-to-GUID step). nil or none means every
// character and returns nullptr. A number or string is a character name,
// except a string that reads as the zero GUID: one that is a prefix of
// "0000000000000000" (so "", "0" and the number 0) or "WOWGUID_NULL", which
// also mean every character. A player GUID string is that character's name.
// Any other type raises
// `bad argument #<idx> to '<fn>' (<usage>)`.
const char *CharacterArg(void *L, int idx, const char *usage);

} // namespace Addons
