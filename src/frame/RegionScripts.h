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

#include "frame/ScriptArgs.h"

#include <cstdint>

// Scripts on textures and font strings (and the Line and MaskTexture made
// from textures) - see RegionScripts.cpp.
namespace Frame::RegionScripts {

// 3.80.2's region script list (FUN_140593DF0).
enum Script {
    ON_LOAD,
    ON_SHOW,
    ON_HIDE,
    ON_ENTER,
    ON_LEAVE,
    ON_MOUSE_DOWN,
    ON_MOUSE_UP,
    ON_MOUSE_WHEEL,
    SCRIPT_COUNT
};

// True for a texture or a font string: an object whose script-name resolver
// is this module's. A frame answers false.
bool IsRegion(const void *object);

// The region's {handler ref, context} slot for `script`, or null when no
// handler was ever set.
const uint32_t *Slot(const void *region, Script script);

// True when the region has a handler for `script`.
inline bool Has(const void *region, Script script) {
    const uint32_t *slot = Slot(region, script);
    return slot != nullptr && slot[0] != 0;
}

// Runs the region's handler for `script`, if any, with the arguments `push`
// pushes (Frame::ScriptArgs::RunSlot).
template <class Push> void Run(void *region, Script script, Push &&push) {
    Frame::ScriptArgs::RunSlot(Slot(region, script), region, push);
}
inline void Run(void *region, Script script) { Frame::ScriptArgs::RunSlot(Slot(region, script), region); }

} // namespace Frame::RegionScripts
