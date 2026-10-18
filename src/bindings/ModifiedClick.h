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

namespace Bindings::ModifiedClick {

// 4.3.4's CGUIBindings::IsModifiedClick. `action` null: any modifier key is
// held or any modified-click action is active. A modifier string ("SHIFT",
// "LCTRL-ALT"): those keys are held. Otherwise: the named action's binding in
// the current set is active.
bool IsModifiedClick(const char *action);

} // namespace Bindings::ModifiedClick
