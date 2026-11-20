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

#include <cstddef>
#include <string>
#include <vector>

namespace AddOns::LoadManagers {

// `## LoadManagers:`, as the 3.80.2 client applies it.
//
// 3.80.2 appends every `## LoadManagers:` name (split on space or comma) to a
// list per addon. Its login auto-load pass (FUN_141af8680) loads each listed
// manager in order, a real load, and at the first one that loads it marks the
// addon load-on-demand (+0x43) and does not load it. When no manager loads,
// the addon loads as usual. The load progress total (FUN_14168e710
// @0x14168f71d) leaves out an addon one of whose managers can load.
//
// Here: TocRewrite records the names whenever an addon's base TOC is read.
// 1.12's load pass FUN_0051F600 calls the loader FUN_ADDON_LOAD_ONE for each
// entry that is not load-on-demand (0x0051F62B), and its progress sum calls
// the load check FUN_ADDON_CAN_LOAD for the same entries (0x0051F215). Both
// calls are redirected; a manager that loads sets the entry's LoadOnDemand
// byte, so the addon then reports as load-on-demand like in 3.80.2.

// Records the `## LoadManagers:` names of the addon named
// [name, name + nameLen). An empty list removes the record.
void Record(const char *name, size_t nameLen, const std::vector<std::string> &managers);

} // namespace AddOns::LoadManagers
