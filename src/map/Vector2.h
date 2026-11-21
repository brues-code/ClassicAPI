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

#include "Game.h"
#include "lua/MixinTable.h"

// Shared `vector2` marshalling for the `C_Map` surface. Map positions cross
// the C/Lua boundary as `Vector2DMixin` objects (so consumers can call
// `pos:GetXY()` as well as read `.x` / `.y`), and several modules both return
// and accept them — this keeps the stack handling in one place.

namespace Map {

// Pushes a `Vector2DMixin` of (x, y), built as 3.80.2 builds every vector2
// return (lua/MixinTable.h). Leaves exactly one value on the stack.
inline void PushVector2D(void *L, double x, double y) {
    Game::Lua::MixinTable::Push(L, "Vector2DMixin");
    Game::Lua::SetFieldNumber(L, "x", x);
    Game::Lua::SetFieldNumber(L, "y", y);
}

// Reads `t.x` / `t.y` from the table at absolute stack index `idx` (a
// `Vector2DMixin` or a plain `{x, y}`). `idx` must be positive so it survives
// the key pushes. Returns false when the slot isn't a table. Stack-balanced.
inline bool ReadVector2D(void *L, int idx, double *x, double *y) {
    if (Game::Lua::Type(L, idx) != Game::Lua::TYPE_TABLE)
        return false;
    Game::Lua::PushString(L, "x");
    Game::Lua::GetTable(L, idx);
    *x = Game::Lua::ToNumber(L, -1);
    Game::Lua::SetTop(L, -2);
    Game::Lua::PushString(L, "y");
    Game::Lua::GetTable(L, idx);
    *y = Game::Lua::ToNumber(L, -1);
    Game::Lua::SetTop(L, -2);
    return true;
}

} // namespace Map
