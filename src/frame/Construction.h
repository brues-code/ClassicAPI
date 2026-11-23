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

#include "AutoList.h"

// Shared observer for the frame constructor CSimpleFrame::CSimpleFrame
// (FUN_SIMPLEFRAME_CONSTRUCTOR), which every frame class's constructor runs:
// the factories of CreateFrame and the XML loader, and the engine's own
// frames. The counterpart of Frame::Destruction. A listener runs after the
// base constructor, so the frame's base part exists and a derived class's
// part does not yet.
//
// This module owns the one hook and fans out. Declare a file-scope
//   static const Frame::Construction::AutoSubscribe _sub{&OnFrameConstructed};
namespace Frame::Construction {

using Listener = void (*)(void *frame);

using AutoSubscribe = AutoList<struct AutoSubscribeTag, Listener>;

} // namespace Frame::Construction
