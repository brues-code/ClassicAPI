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
#include <string>

// The equipment manager's player-facing text: localized globals, the error
// messages 4.3.4 shows through UI_ERROR_MESSAGE, and the set-name list its
// item tooltips and `GetContainerItemEquipmentSetInfo` share.
namespace EquipmentSet::Text {

// `_G[name]` when it is a string, else `fallback` (4.3.4 FrameScript_GetText).
std::string Global(const char *name, const char *fallback);

// Fires UI_ERROR_MESSAGE with `_G[globalName]` (or `fallback`), its first
// `%s` replaced by `setName` when one is given — 4.3.4's
// `FrameScript_SignalEvent(UI_ERROR_MESSAGE, "%s", text)`.
void UIError(const char *globalName, const char *fallback, const char *setName = nullptr);

// 4.3.4 `CGEquipmentManager::GetSetList`: the names of every set holding the
// item instance `itemGuid`, joined by PLAYER_LIST_DELIMITER. Empty when none.
std::string SetList(uint64_t itemGuid);

// `format` with its first `%s` replaced by `arg`. By hand, not printf: the
// format is a Lua global any addon can reassign.
std::string FormatS(const std::string &format, const std::string &arg);

} // namespace EquipmentSet::Text
