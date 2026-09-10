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

#include "item/Icon.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "item/Record.h"

#include <cstdio>

namespace Item::Icon {

bool PathForDisplayInfoID(uint32_t displayInfoID, char *out, size_t outSize) {
    if (out == nullptr || outSize == 0)
        return false;
    out[0] = '\0';
    const char *iconName = DBC::StringField(
        Offsets::VAR_ITEMDISPLAYINFO_RECORDS, Offsets::VAR_ITEMDISPLAYINFO_COUNT,
        displayInfoID, Offsets::OFF_ITEMDISPLAYINFO_ICON);
    if (iconName == nullptr || iconName[0] == '\0')
        return false;
    std::snprintf(out, outSize, "Interface\\Icons\\%s", iconName);
    return true;
}

bool PathForItemID(uint32_t itemID, char *out, size_t outSize) {
    if (out == nullptr || outSize == 0)
        return false;
    out[0] = '\0';
    const uint8_t *record = Item::PeekRecord(itemID);
    if (record == nullptr)
        return false;
    return PathForDisplayInfoID(
        Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID), out, outSize);
}

} // namespace Item::Icon
