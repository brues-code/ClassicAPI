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

// `<KeyValues>` on XML elements — see KeyValues.cpp.

namespace Xml::KeyValues {

// Sets each <KeyValues> entry of `node` on the Lua table at stack index
// `index`. `owner` names the element in load warnings.
void Apply(void *L, int index, const void *node, void *status, const char *owner);

// Apply() for a frame, texture or font string: pushes the object's wrapper
// only when `node` has a <KeyValues> child.
void ApplyToRegion(void *object, const void *node, void *status);

// Pushes `node`'s own entries as an array of { key, keyType, type, value }
// (C_XMLUtil.GetTemplateInfo's keyValues), the strings as written.
void PushInfo(void *L, const void *node);

} // namespace Xml::KeyValues
