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

namespace Tooltip::SetEvents {

// RAII guard that suppresses tooltip set-event firing (OnTooltipSetItem etc.)
// for the duration of an engine build we invoke *internally* — currently
// Tooltip::Compare building the equipped item into a shopping tooltip. Without
// it, a handler would run in the middle of our line-shift / delta rendering and
// see a half-built tooltip. Only the item builder runs during that internal
// build, so in practice this gates OnTooltipSetItem; it's global for safety.
// Nestable (ref-counted).
struct Suppressor {
    Suppressor();
    ~Suppressor();
    Suppressor(const Suppressor &) = delete;
    Suppressor &operator=(const Suppressor &) = delete;
};

// Runs the tooltip's OnTooltipSetFrameStack handler, if it has one, with the
// highlighted frame (or nil) as its argument — retail's (self, highlightFrame).
// Tooltip::FrameStack calls it after it adds the frame-stack lines.
void FireFrameStack(void *tooltip, void *highlight);

// Drop every per-tooltip handler cell. Must run at each UI teardown
// (FrameScript_Initialize_h — /reload and /logout): the engine destroys and
// recreates the tooltip objects, so the cells' tooltip pointers go stale and
// their handler refs die with the reset Lua state. Without this the fixed
// cell table leaks one entry per recreated tooltip per reload until it fills
// (SetScript then errors "doesn't have a 'OnTooltipSetX' script") and a
// recycled tooltip address can match a stale cell and fire a dead handler
// ref — issue #33.
void PrepareForReload();

} // namespace Tooltip::SetEvents
