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

// Shared observer for CSimpleFrame::SetParent (FUN_FRAME_SET_PARENT), which
// Script SetParent, CreateFrame's parent argument, the XML frame factory and
// the frame constructor all go through (Offsets.h). Listeners run after the
// engine has moved the frame, so it already has the new parent's strata,
// level and child list.
//
// MinHook allows ONE hook per address and more than one feature follows a
// frame's parent (Frame::Alpha's drawn alpha, Frame::ClipChildren's clip
// root), so this module owns the hook and fans out. Declare a file-scope
//   static const Frame::ParentChange::AutoSubscribe _sub{&OnParentChanged};
namespace Frame::ParentChange {

using Listener = void (*)(void *frame);

struct AutoSubscribe {
    explicit AutoSubscribe(Listener fn);
    Listener fn;
    AutoSubscribe *next;
};

} // namespace Frame::ParentChange
