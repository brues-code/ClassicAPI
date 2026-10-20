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

// Shared observer for the frame destructor body FUN_FRAME_DESTRUCTOR, which
// every frame class's destructor runs (Offsets.h). It deletes the frame's
// regions and child frames, so a listener runs while they all still exist.
//
// MinHook allows ONE hook per address and more than one feature keeps state
// keyed by a frame (Frame::Alpha's own alpha, the animation groups), so this
// module owns the hook and fans out. Declare a file-scope
//   static const Frame::Destruction::AutoSubscribe _sub{&OnFrameDestroyed};
namespace Frame::Destruction {

using Listener = void (*)(void *frame);

struct AutoSubscribe {
    explicit AutoSubscribe(Listener fn);
    Listener fn;
    AutoSubscribe *next;
};

} // namespace Frame::Destruction
