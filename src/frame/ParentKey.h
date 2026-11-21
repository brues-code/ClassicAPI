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

#include <string>

// An object's parent key, as the 3.80.2 client keeps it: nowhere but in the
// parent's Lua table. Each read searches that table (FUN_140592bb0), and
// `self:GetParentKey()`, `self:SetParentKey(parentKey [, clearOtherKeys])`,
// `self:ClearParentKey()` and GetDebugName all go through the search.
// 3.80.2 binds the methods on every script object that has a parent; here
// that is regions and our own script objects (animation groups, animations,
// control points, scene actors), each with its own parent
// (Animation::ParentOf).
namespace Frame::ParentKey {

// The key under which the parent's table holds `object`. The first string
// key whose value is the object wins; a later one replaces it only when it
// starts with A-Z and the first does not. False with no such key or no
// parent.
bool Find(void *L, void *object, std::string &key);

// `self` of a method that every parented script object has: one of our
// objects, else a region (raising the engine's type error otherwise).
void *ResolveSelf(void *L);

int __fastcall Script_GetParentKey(void *L);
int __fastcall Script_SetParentKey(void *L);
int __fastcall Script_ClearParentKey(void *L);

} // namespace Frame::ParentKey
