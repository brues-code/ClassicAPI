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

// The client locale by NAME, as GetLocale() reports it.
//
// Language rules (plurals, accent folding, number abbreviations) must pick
// by this name, never by VAR_LOCALE_INDEX. The 1.12 index is the Wow.ini
// tag's position in a table of eight codes (enUS koKR frFR deDE zhCN zhTW
// esES xxYY), not 4.3.4's nine-slot order, so a 4.3.4 index such as 8 for
// ruRU never occurs here. A Russian client is one whose table holds "ruRU",
// in whatever slot. The index stays right for picking a DBC string column.
namespace Localization::Locale {

// The locale code GetLocale() returns ("enUS", "frFR", ...).
const char *Client();

// Whether the client locale is `code`.
bool Is(const char *code);

} // namespace Localization::Locale
