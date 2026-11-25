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
#include "gamepad/Names.h"

#include <cstdint>
#include <string>

// The gamepad devices, as the 3.80.2 client keeps them over SDL: five slots,
// 1..4 for physical devices and 5 for all of them combined, each with a raw
// input state the per-frame poll fills from SDL's events.
//
// 3.80.2 links SDL2; this is SDL3, statically linked. The two report the same
// buttons and axes in the same order (SDL_GamepadButton 0..20 is
// SDL_GameControllerButton 0..20, positional as 3.80.2's
// SDL_GAMECONTROLLER_USE_BUTTON_LABELS=0 makes SDL2), and take the same
// mapping strings. SDL3 reports battery charge as a percentage where SDL2 had
// levels; PowerLevel() converts back the way sdl2-compat does.
namespace GamePad::Device {

constexpr int kSlotCount = 5;
constexpr uint32_t kCombinedID = 5;

// GamePadPowerLevel: Critical 0, Low 1, Medium 2, High 3, Wired 4, Unknown 5.
constexpr uint8_t kPowerUnknown = 5;

// One slot (0x1444A6B60, stride 0x120).
struct Slot {
    uint32_t id = 0;
    bool connected = false;
    bool opened = false; // a physical device is assigned
    bool hasGyro = false, hasTouchpad = false;
    bool hasRumble = false, hasTriggerRumble = false, hasLED = false;
    std::string name;
    uint32_t vendor = 0, product = 0;
    uint32_t buttonCount = 0, axisCount = 0;
    uint8_t power = kPowerUnknown;
    Config::DeviceDesc desc; // what its default config is built from
};

// The raw state of one physical device (0x1468BFA60, 0xA0 each).
struct Raw {
    uint64_t stamp = 0;     // ns of the last poll its device was open, 0 once closed
    uint64_t lastInput = 0; // ns of its last input event
    uint32_t buttons = 0, pressed = 0, released = 0;
    float cur[Names::kAxisCount] = {};
    float min[Names::kAxisCount] = {};
    float max[Names::kAxisCount] = {};
};

// Slot `id` (1..5), or null.
Slot *SlotByID(uint32_t id);
Raw &RawOf(int index); // index 0..3

// SDL lifetime (0x1412740B0 / the SDL half of 0x141258FE0). Init false when
// SDL could not start.
bool Init();
void Quit();
bool Running();

// One poll's SDL work for the four physical slots (0x1412766A0): rumble and
// LED pushes, the event drain into the raw states, the gyro axes.
void Frame(uint64_t now);

// A device SDL reported added and no slot took yet (0x1468BFF70).
bool TakePendingAdd();
// Assigns the first open, unassigned device to the empty slot (0x141274350).
// False when there is none.
bool Fill(Slot &slot);
// Releases a slot's device, which stays open for a later Fill (0x141259650's
// backend half).
void Release(Slot &slot);
// True while the slot's device is still open in SDL.
bool Alive(const Slot &slot);

// Rumble (SetVibration) and LED for a slot's device. `type` is 0 Low, 1 High,
// 2 LTrigger, 3 RTrigger; `level` 0..1; `untilNs` the expiry.
void SetRumble(const Slot &slot, int type, float level, uint64_t untilNs);
void StopRumble(const Slot &slot, uint64_t now);
void SetLED(const Slot &slot, uint32_t rgb);

// Adds an SDL mapping string (SDL_AddGamepadMapping). -1 on a parse error,
// 0 for an update, 1 for a new mapping.
int AddMapping(const char *mapping);

uint64_t NowNs();

// The touchpad cvars the drain reads (GamePadTouchCursorMoveThreshold,
// GamePadTouchTapMaxMs, GamePadTouchTapButtons), set by their callbacks.
struct TouchSettings {
    float moveThreshold = 0.042f;
    uint32_t tapMaxMs = 200;
    bool tapButtons = false;
};
TouchSettings &Touch();

// True while the game window is the foreground window. 3.80.2 drops
// controller input while its window is inactive (0x144C95BBF).
bool AppActive();

} // namespace GamePad::Device
