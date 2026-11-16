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

#include "object/ClosestByEntry.h"

#include "Game.h"
#include "Offsets.h"
#include "object/Resolve.h"
#include "unit/Position.h"

#include <cstdint>

namespace Object::ClosestByEntry {

Result Find(uint32_t typeMask, Guid::Type guidType, uint32_t entry) {
    Result r{false, 0.0f, 0.0f, 0.0f};
    // The player resolve reads the object registry, NULL during glue /
    // character-select.
    if (entry == 0 || !Object::RegistryReady())
        return r;

    float ppos[3];
    if (!Unit::Position::Read(Game::ResolveUnitToken("player"), ppos))
        return r;

    Object::ForEachVisible([&](uint64_t guid) {
        // Type + entry checks are pure GUID bit-reads, so we skip the (costly)
        // object resolve for everything that can't match.
        if (Guid::Classify(guid) != guidType ||
            static_cast<uint32_t>((guid >> 24) & 0xFFFFFFu) != entry)
            return true;
        float pos[3];
        if (!Unit::Position::Read(Object::ByGuid(typeMask, guid, nullptr, 0), pos))
            return true;
        const float distSq = Game::DistanceSq(pos, ppos);
        if (!r.found || distSq < r.distSq)
            r = {true, pos[0], pos[1], distSq};
        return true;
    });
    return r;
}

} // namespace Object::ClosestByEntry
