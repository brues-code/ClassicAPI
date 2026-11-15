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

#include "object/Corpse.h"

#include "Game.h"
#include "Offsets.h"
#include "object/Resolve.h"

namespace Object::Corpse {

namespace {

const uint8_t *Fields(const void *corpse) {
    return Game::Read<const uint8_t *>(corpse, Offsets::OFF_UNIT_DESCRIPTOR);
}

uint64_t Owner(const void *corpse) {
    const uint8_t *fields = Fields(corpse);
    return fields != nullptr ? Game::Read<uint64_t>(fields, Offsets::OFF_CORPSE_FIELD_OWNER) : 0;
}

} // namespace

void *ByOwner(uint64_t owner) {
    if (owner == 0)
        return nullptr;
    void *corpse = nullptr;
    // Bones keep their owner (tortoise-wow Map.cpp, the corpse-to-bones
    // conversion), so every earlier death leaves one beside the live corpse;
    // 4.3.4 skips them.
    Object::ForEachVisible([&](uint64_t guid) {
        void *obj = Object::ByGuid(Offsets::TYPEMASK_CORPSE, guid, nullptr, 0);
        if (obj == nullptr || Owner(obj) != owner ||
            (Game::Read<uint32_t>(Fields(obj), Offsets::OFF_CORPSE_FIELD_FLAGS) &
             Offsets::CORPSE_FLAG_BONES) != 0)
            return true;
        corpse = obj;
        return false;
    });
    return corpse;
}

uint64_t OwnerOf(uint64_t guid) {
    const void *obj = Object::ByGuid(Offsets::TYPEMASK_CORPSE, guid);
    return obj != nullptr ? Owner(obj) : 0;
}

} // namespace Object::Corpse
