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

// Shared observer for the ScrollingMessageFrame line layout
// FUN_SMF_DISPLAY_REFRESH (Offsets.h), which lays out the lines the frame
// shows after every change to them: a message added at the bottom, a scroll,
// a resize, a font change, Clear. A listener runs after it, with the frame.
//
// MinHook allows ONE hook per address and more than one feature follows the
// layout (Text::InlineTexture's early icon apply, the display-refreshed
// callbacks), so this module owns the hook and fans out. Declare a file-scope
//   static const ScrollingMessageFrame::Layout::AutoSubscribe _sub{&OnLayout};
namespace ScrollingMessageFrame::Layout {

using Listener = void (*)(void *smf);

struct AutoSubscribe {
    explicit AutoSubscribe(Listener fn);
    Listener fn;
    AutoSubscribe *next;
};

} // namespace ScrollingMessageFrame::Layout
