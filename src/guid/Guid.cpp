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

#include "Guid.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace Guid {

bool Parse(const char *str, uint64_t *out) {
    if (str == nullptr)
        return false;
    if (str[0] != '0' || (str[1] != 'x' && str[1] != 'X'))
        return false;
    const char *hex = str + 2;
    const std::size_t len = std::strlen(hex);
    if (len != 8 && len != 16)
        return false;
    uint64_t value = 0;
    for (std::size_t i = 0; i < len; ++i) {
        const char c = hex[i];
        uint64_t digit;
        if (c >= '0' && c <= '9')       digit = c - '0';
        else if (c >= 'a' && c <= 'f')  digit = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F')  digit = 10 + (c - 'A');
        else                             return false;
        value = (value << 4) | digit;
    }
    *out = value;
    return true;
}

const char *FormatAsString(uint64_t guid, char *buf, std::size_t cap) {
    if (buf == nullptr || cap < STRING_SIZE)
        return "";
    std::snprintf(buf, cap, "0x%08X%08X",
                  static_cast<uint32_t>(guid >> 32),
                  static_cast<uint32_t>(guid));
    return buf;
}

uint32_t CreatureEntry(uint64_t guid) {
    if (Classify(guid) != Type::Creature) // a pet GUID packs the pet number here
        return 0;
    return static_cast<uint32_t>((guid >> 24) & 0xFFFFFFu);
}

Type Classify(uint64_t guid) {
    if (guid == 0)
        return Type::Unknown;
    const uint16_t prefix = static_cast<uint16_t>(guid >> 48);
    switch (prefix) {
        case PREFIX_PLAYER:         return Type::Player;
        case PREFIX_ITEM:           return Type::Item;
        case PREFIX_CREATURE:       return Type::Creature;
        case PREFIX_PET:            return Type::Pet;
        case PREFIX_GAMEOBJECT:     return Type::GameObject;
        case PREFIX_DYNAMICOBJECT:  return Type::DynamicObject;
        case PREFIX_CORPSE:         return Type::Corpse;
        default:                    return Type::Unknown;
    }
}

} // namespace Guid
