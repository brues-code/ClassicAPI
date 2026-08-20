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

namespace LuaSyntax {

// Pushes the per-addon shared namespace table for `name` onto the Lua stack,
// creating it on first use. The same table object is returned on every call
// for a given name, so the internal addon-args preamble (which hands a file
// its `(addonName, addonTable)` varargs) and the public gated accessor
// `C_AddOns.GetAddOnLocalTable` share one table per addon. Backed by a map in
// the Lua registry, hidden from addons. Net stack effect: +1. `name` must be
// non-null.
//
// This is the ONE place the namespace map is read/created — do not duplicate
// the registry-key or map access elsewhere, or the two paths would hand out
// different tables for the same addon.
void PushAddonNamespace(void *L, const char *name);

} // namespace LuaSyntax
