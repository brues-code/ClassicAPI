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

#include "gamepad/Config.h"
#include "gamepad/Device.h"
#include "gamepad/Names.h"

#include <cstdint>

// The mapped state of each device: raw input through its resolved config,
// as 3.80.2 builds it every frame (0x141259A90, the stick pass 0x1412590A0,
// the combined device 0x14125A000).
namespace GamePad::State {

struct StickValue {
    float x = 0, y = 0, len = 0;
};

// 0x1468BF570, stride 0xFC.
struct Mapped {
    uint32_t buttons = 0, pressed = 0, released = 0, held = 0, prevButtons = 0;
    float axes[Names::kAxisCount] = {};
    float axesMin[Names::kAxisCount] = {};
    float axesMax[Names::kAxisCount] = {};
    StickValue sticks[Names::kStickCount];
    float touchDelta[2] = {};      // +0xEC, the raw state's touch cursor move
    uint32_t touchButton = 0;      // +0xF4, its touch click (1 left, 4 right)
    uint32_t prevTouchButton = 0;  // +0xF8, last frame's
};

// Slot `id`'s (1..5) resolved config and mapped state.
Config::Resolved &ConfigOf(uint32_t id);
Mapped &MappedOf(uint32_t id);

// Resolves slot `id` (0x141267F70): a physical slot from its device, the
// combined slot from the generic configs.
void Resolve(uint32_t id);

// A raw state at rest under its config (0x14125C080): no buttons, each axis
// at the value its config maps to 0.
void RawReset(Device::Raw &raw, const Config::Resolved &cfg);

// Maps every connected physical slot, then builds the combined device.
void MapAll();

// Clears a slot to empty (0x14125BFC0), keeping its id.
void ResetSlot(uint32_t id);

// Forces the combined device to resolve again on its next build.
void InvalidateCombined();

} // namespace GamePad::State
