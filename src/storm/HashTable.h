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

// Storm's TSHashTable, read the way the engine's inlined lookups read it
// (FUN_BINDING_COMMAND_EXECUTE, FUN_XML_TEMPLATE_LOOKUP, FUN_XML_FRAME_BUILD).
// A table is addressed by its bucket-pointer field (table + OFF_HASHTABLE_SLOTS,
// or a global like VAR_XML_TEMPLATE_TABLE); the bucket mask follows 8 bytes
// on, and is 0xFFFFFFFF before the first insert. Layout at OFF_HASHTABLE_*.

#include "Offsets.h"

#include <cstdint>

namespace Storm::HashTable {

// Calls `fn(node)` for every node, bucket by bucket, until it returns false.
template <class Fn> void ForEach(uintptr_t slotsField, Fn &&fn) {
    const auto *buckets = *reinterpret_cast<const uint8_t *const *>(slotsField);
    const uint32_t mask = *reinterpret_cast<const uint32_t *>(
        slotsField + (Offsets::OFF_HASHTABLE_MASK - Offsets::OFF_HASHTABLE_SLOTS));
    if (buckets == nullptr || mask == 0xFFFFFFFFu)
        return;
    for (uint32_t b = 0; b <= mask; ++b) {
        const uint8_t *bucket = buckets + b * Offsets::HASHTABLE_BUCKET_STRIDE;
        const int link = *reinterpret_cast<const int *>(bucket + Offsets::OFF_HASHTABLE_BUCKET_LINK);
        for (auto *node = *reinterpret_cast<const uint8_t *const *>(bucket + Offsets::OFF_HASHTABLE_BUCKET_HEAD);
             node != nullptr && (reinterpret_cast<uintptr_t>(node) & 1) == 0;
             node = *reinterpret_cast<const uint8_t *const *>(node + link + 4))
            if (!fn(node))
                return;
    }
}

// The node named `name` in a HASHKEY_STRI table (hash first, then SStrCmpI,
// as the engine compares), or nullptr.
const uint8_t *FindStri(uintptr_t slotsField, const char *name);

// A HASHKEY_STRI node's name.
inline const char *NodeName(const uint8_t *node) {
    return *reinterpret_cast<const char *const *>(node + Offsets::OFF_HASHTABLE_NODE_NAME);
}

} // namespace Storm::HashTable
