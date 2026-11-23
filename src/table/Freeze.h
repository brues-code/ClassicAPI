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

namespace Table::Freeze {

// The engine's `luaV_settable` (Offsets::FUN_LUAV_SETTABLE).
using SetTable_t = void(__fastcall *)(void *L, const void *t, void *key, void *val);

// `t[key] = val` with 3.80.2's frozen branch: a frozen table never takes the
// raw store, the write goes to its __newindex. Every other value goes to
// `store`, the engine's own luaV_settable. Table::Settable owns the hook and
// calls this for every store.
void SetTable(void *L, const void *t, void *key, void *val, SetTable_t store);

} // namespace Table::Freeze
