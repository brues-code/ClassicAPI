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

// Listeners run when a region (a texture or a font string) is destroyed, from
// the region destructor body every region type ends in (SimpleRegionDestruct).
// By then the subclass part is torn down, so a listener may only use the
// pointer as a key. The block then goes back to the region pool and can come
// back as another region, so a module that keeps a region pointer must drop it
// here. Usually this runs inside a frame's destructor (regions die with their
// frame), so a listener must not change other regions or frames either: drop
// the pointer, and defer any work to a tick. Same pattern as
// Frame::Destruction.
namespace Frame::RegionDestruction {

using Listener = void (*)(void *region);

struct AutoSubscribe {
    explicit AutoSubscribe(Listener fn);
    Listener fn;
    AutoSubscribe *next;
};

} // namespace Frame::RegionDestruction
