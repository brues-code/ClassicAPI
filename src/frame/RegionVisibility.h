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

// Listeners run when a texture or a font string becomes visible or stops
// being visible: shown, with its owner frame visible (the region's
// OFF_REGION_ACTUALLY_SHOWN). See RegionVisibility.cpp.
namespace Frame::RegionVisibility {

using Listener = void (*)(void *region, bool visible);

using AutoSubscribe = AutoList<struct AutoSubscribeTag, Listener>;

// True when the region is visible: shown, and its owner frame is visible.
bool Visible(const void *region);

} // namespace Frame::RegionVisibility
