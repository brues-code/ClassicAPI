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

namespace Item::TypeInfo {

// Localized ItemClass.dbc name for a class ID, "" when unknown.
const char *ClassName(uint32_t classID);

// The ItemSubClass.dbc row for (classID, subClassID), or nullptr.
const uint8_t *FindSubClass(uint32_t classID, uint32_t subClassID);

// The row's localized verbose name, else its short name, else "".
const char *SubClassName(const uint8_t *rec);

// The row's localized short name ("Sword", "Cloth"), "" when none.
const char *SubClassShortName(const uint8_t *rec);

// Whether either localized name of the row equals `name`, ignoring case.
bool SubClassNamed(const uint8_t *rec, const char *name);

// The equipLoc key ("INVTYPE_HEAD", …) for an inventory type, "" when none.
const char *InvTypeKey(int invType);

// The equip slot's display name ("Head", "Two-Hand") for an inventory type:
// the global string its key names. "" when none.
const char *InvTypeName(int invType);

} // namespace Item::TypeInfo
