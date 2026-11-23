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

#include "classes/ColorCache.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Names.h"

#include <vector>

namespace Classes::ColorCache {

namespace {

// By ChrClasses.dbc id; 0 where RAID_CLASS_COLORS gave no colour (a packed
// colour always has alpha 0xFF).
std::vector<uint32_t> g_colors;

// `t[key]` of the table at absolute `index` as one channel byte, or -1 when
// it is not a number.
int Channel(void *L, int index, const char *key) {
    Game::Lua::PushString(L, key);
    Game::Lua::GetTable(L, index);
    int byte = -1;
    if (Game::Lua::IsNumber(L, -1)) {
        double value = Game::Lua::ToNumber(L, -1);
        value = value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
        byte = static_cast<int>(value * 255.0 + 0.5);
    }
    Game::Lua::SetTop(L, -2);
    return byte;
}

} // namespace

void Refresh(void *L) {
    const int count = *reinterpret_cast<const int *>(Offsets::VAR_CHRCLASSES_COUNT);
    g_colors.assign(count > 0 ? static_cast<size_t>(count) + 1 : 0, 0);

    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, "RAID_CLASS_COLORS");
    Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE) {
        for (int id = 1; id <= count; ++id) {
            const char *token = DBC::ClassToken(static_cast<uint32_t>(id));
            if (token == nullptr)
                continue;
            Game::Lua::PushString(L, token);
            Game::Lua::GetTable(L, top + 1);
            if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE) {
                const int r = Channel(L, top + 2, "r");
                const int g = Channel(L, top + 2, "g");
                const int b = Channel(L, top + 2, "b");
                if (r >= 0 && g >= 0 && b >= 0)
                    g_colors[id] = 0xFF000000u | (static_cast<uint32_t>(r) << 16) |
                                   (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
            }
            Game::Lua::SetTop(L, top + 1);
        }
    }
    Game::Lua::SetTop(L, top);
}

bool Get(uint32_t classId, uint32_t &argb) {
    if (classId >= g_colors.size() || g_colors[classId] == 0)
        return false;
    argb = g_colors[classId];
    return true;
}

} // namespace Classes::ColorCache
