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
#include <cstdint>

namespace AddOns::LoadGate {

// Whole-addon gates from TOC metadata, as the 3.80.2 client applies them.
//
// 3.80.2 stores `## AllowLoadGameType:` (every line OR'ed into a game-type
// mask, default 0) and `## AllowLoad:` (one word glue / game / both, the last
// line wins, default game, an unknown word reads 0) per addon, and its load
// check FUN_141af6140 refuses a load with
//   WRONG_ACTIVE_INTERFACE (11) when the AllowLoad value lacks the current
//                               interface (game, for addons here), then
//   WRONG_GAME_TYPE (12)        when the mask is set and shares no game type
//                               with the client,
// after MISSING and before every other reason. It applies them only to a real
// load: `GetAddOnInfo` and `IsAddOnLoadable` call the check with param_6 = 1,
// which skips them, so those queries never report either reason. `LoadAddOn`
// re-runs the check for a real load on failure and returns the reason.
//
// Here: TocRewrite records the values whenever an addon's base TOC is read
// (registration scan, load pass, rescan). 1.12's load check FUN_0051e780 is
// redirected at the two real-load calls (the loader FUN_0051F240 and
// Script_LoadAddOn's reason re-check) and at its own required-dependency
// recursion, which inherits the real-load mode, as 3.80.2's does. The reason
// formatter call in Script_LoadAddOn names the two new codes.

// Records the gates of the addon named [name, name + nameLen): the OR of its
// `## AllowLoadGameType:` masks (TocRewrite::GameTypeMask) and its
// `## AllowLoad:` value (1 glue, 2 game, 3 both, 0 unknown).
void Record(const char *name, size_t nameLen, uint32_t gameTypes, uint32_t allowLoad);

// 3.80.2's addon index list (FUN_141af4fe0, what GetNumAddOns counts) leaves
// out an addon that fails `## AllowLoadGameType:`; the name still resolves.
// Sets the entry's filter-out byte (OFF_ADDON_ENTRY_FILTER_OUT, which 1.12's
// list builders skip) on every such non-secure entry, and clears it on an
// entry it hid before that no longer fails. Called before each list build:
// 1.12's own (FUN_ADDON_BUILD_DISPLAY at SMSG_ADDON_INFO, redirected here) and
// AddOns::Rescan's.
void ApplyListFilter();

} // namespace AddOns::LoadGate
