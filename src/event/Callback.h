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

// Event callbacks: `RegisterEventCallback` and its kin, the frame forms, and
// `C_EventUtils.IsCallbackEvent`, as the 3.80.2 client has them. Only the
// eight callback events take callbacks. A module that fires one of them calls
// `Fire` after its frame dispatch (3.80.2 runs frame OnEvent handlers first).
namespace Event::Callback {

enum Id : int {
    MINIMAP_PING,
    COMBAT_LOG_APPLY_FILTER_SETTINGS,
    ENCOUNTER_STATE_CHANGED,
    TOOLTIP_SHOW_ITEM_COMPARISON,
    COMBAT_LOG_EVENT,
    COMBAT_LOG_EVENT_UNFILTERED,
    COMBAT_LOG_EVENT_INTERNAL_UNFILTERED,
    COMBAT_LOG_REFILTER_ENTRIES,
    ID_COUNT,
};

// True when a callback is registered for `id`.
bool HasCallbacks(Id id);

// Runs the callbacks of `id`. Each gets its owner (nil, or the frame's table
// for a frame registration) and then the `nargs` values at absolute stack
// index `firstArg`. With `unit` (MINIMAP_PING), the callbacks registered for
// that unit run after the plain ones. A registration or removal a callback
// makes takes effect once this fire ends.
void Fire(Id id, void *L, int firstArg, int nargs, const char *unit = nullptr);

} // namespace Event::Callback
