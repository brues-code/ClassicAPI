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

#include "storm/HashTable.h"

#include "Game.h"
#include "storm/List.h"
#include "storm/String.h"

namespace Storm::HashTable {

const uint8_t *FindStri(uintptr_t slotsField, const char *name) {
    const auto *buckets = Game::Read<const uint8_t *>(slotsField);
    const uint32_t mask =
        Game::Read<uint32_t>(slotsField + (Offsets::OFF_HASHTABLE_MASK - Offsets::OFF_HASHTABLE_SLOTS));
    if (name == nullptr || *name == '\0' || buckets == nullptr || mask == 0xFFFFFFFFu)
        return nullptr;
    const uint32_t hash = Storm::HashHT(name);
    const uint8_t *bucket = buckets + (hash & mask) * Offsets::HASHTABLE_BUCKET_STRIDE;
    const int link = Game::Read<int>(bucket, Offsets::OFF_HASHTABLE_BUCKET_LINK);
    for (auto *node = Game::Read<const uint8_t *>(bucket, Offsets::OFF_HASHTABLE_BUCKET_HEAD);
         Storm::IsNode(node);
         node = Game::Read<const uint8_t *>(node, link + 4))
        if (Game::Read<uint32_t>(node, Offsets::OFF_HASHTABLE_NODE_HASH) == hash &&
            Game::StrCmpI(NodeName(node), name) == 0)
            return node;
    return nullptr;
}

} // namespace Storm::HashTable
