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

#include <cstdint>

// Return values that are mixin objects (a Vector2DMixin position, a ColorMixin,
// an ItemLocationMixin, ...), built the way 3.80.2's generated API code builds
// every one of them. That code (FindMixin 0x140A61520, the per-mixin stubs
// such as 0x140A6FCF0 / 0x140AD1A40, and the same recipe inlined at ~45 return
// sites, e.g. C_Debug.GetMapDebugObjects 0x140C05610, C_UIColor.GetColors
// 0x140E122B0) never calls CreateVector2D / CreateColor. It:
//   1. picks an environment: walking the call stack from level 0, the
//      environment of the first Lua function, else the globals (the wrapper
//      sets its "globals" flag to 0 and its function ref to -1 at entry, so the
//      walk is the path every API call takes);
//   2. rawgets the mixin by name there; not a table -> luaL_error
//      "unable to find mixin or metatable (%s)";
//   3. makes a new table, copies every key of the mixin into it (lua_next +
//      rawset, 0x140A60290), removes the mixin;
//   4. sets the data fields.
// The result has no metatable and runs no OnLoad / Init. A mixin changed later
// does not change tables already built.
namespace Game::Lua::MixinTable {

// Pushes a new table holding every key of the mixin `name` (steps 1-3). The
// caller sets the fields on the table on top.
void Push(void *L, const char *name);

// Pushes a ColorMixin of a colour packed b | g << 8 | r << 16 | a << 24, each
// channel as float(byte) * (1/255.0f), as 0x140AD1900 sets them.
void PushColor(void *L, uint32_t argb);

} // namespace Game::Lua::MixinTable
