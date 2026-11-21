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

// `AddOns::LoadState` — tracks which addons are mid-load.
//
// The engine's own "loaded" byte (`entry+0x18`) is set at the START of a
// load, before the addon's files run, so on its own it can't tell
// "loading" from "loaded". That distinction is observable: an addon's
// file-scope code, and every dependency pulled in during it, execute
// while the byte already reads set.
//
// A co-hook on the per-addon loader keeps a stack of the names currently
// being loaded, which is the missing half of
// `C_AddOns.IsAddOnLoaded`'s `(loadedOrLoading, loaded)` pair.
namespace AddOns::LoadState {

// True while `name` is inside the engine's per-addon load call. Matching
// is case-insensitive, as addon-name matching is everywhere else.
bool IsLoading(const char *name);

// True while any addon is inside the engine's per-addon load call.
bool AnyLoading();

// The innermost addon being loaded (a dependency loads inside the addon
// that needs it), or nullptr when no load is running or the stack is deeper
// than it tracks.
const char *Current();

} // namespace AddOns::LoadState
