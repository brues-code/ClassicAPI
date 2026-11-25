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

#include <cstdint>

// The gamepad's name tables, as the 3.80.2 client has them. Indices are the
// ones its configs, mapped state and C_GamePad use: 32 buttons, 10 axes,
// 8 sticks, 4 label styles. 0xFF is "none" everywhere a byte holds one.
namespace GamePad::Names {

constexpr int kButtonCount = 32;
constexpr int kAxisCount = 10;
constexpr int kStickCount = 8;
constexpr int kLabelStyleCount = 4;
constexpr uint8_t kNone = 0xFF;

// Button binding names, 0x143A822E0: PADDUP ... PADSOCIAL. These are the
// binding keys and, once the gamepad has started, the config names too.
extern const char *const kButtonBinding[kButtonCount];
// Button config names, 0x143B83CD0: Up, Right, Down, Left, Face1 ... Social.
// A config file may use either table.
extern const char *const kButtonConfig[kButtonCount];
// Axis names, 0x143B83C80: LStickX ... RTrigger.
extern const char *const kAxis[kAxisCount];
// Stick names, 0x143B83C40: Left, Right, Gyro, Pad, Movement, Camera, Look, Cursor.
extern const char *const kStick[kStickCount];
// Label styles, 0x143B83BE8: Generic, Letters, Reverse, Shapes.
extern const char *const kLabelStyle[kLabelStyleCount];

// Button indices the device defaults and the stick-direction configs name.
enum Button : uint8_t {
    PADDUP = 0, PADDRIGHT, PADDDOWN, PADDLEFT,
    PAD1, PAD2, PAD3, PAD4, PAD5, PAD6,
    PADLSTICK, PADRSTICK, PADLSHOULDER, PADRSHOULDER, PADLTRIGGER, PADRTRIGGER,
    PADLSTICKUP, PADLSTICKRIGHT, PADLSTICKDOWN, PADLSTICKLEFT,
    PADRSTICKUP, PADRSTICKRIGHT, PADRSTICKDOWN, PADRSTICKLEFT,
    PADPADDLE1, PADPADDLE2, PADPADDLE3, PADPADDLE4,
    PADFORWARD, PADBACK, PADSYSTEM, PADSOCIAL,
};

enum Axis : uint8_t {
    LStickX = 0, LStickY, RStickX, RStickY, GStickX, GStickY, PStickX, PStickY, LTrigger, RTrigger,
};

enum Stick : uint8_t { Left = 0, Right, Gyro, Pad, Movement, Camera, Look, Cursor };

enum LabelStyle : uint8_t { Generic = 0, Letters, Reverse, Shapes };

// Case-insensitive parsers, kNone when the name is not in the table.
// ParseButton tries the binding names first, then the config names
// (0x141266980), so "PAD1" and "Face1" both give 4.
uint8_t ParseButton(const char *name);
uint8_t ParseAxis(const char *name);
uint8_t ParseStick(const char *name);
uint8_t ParseLabelStyle(const char *name);

// The name a config field shows for a button byte: the binding name below
// 32, else null. (GetConfig and the file writer print the binding names,
// since the gamepad start points the config-name pointer at them.)
const char *ButtonName(uint8_t button);

// ButtonBindingToIndex (0x1405974D0): "PAD" (any case) followed by the rest
// of one binding name, compared without case. kNone otherwise.
uint8_t BindingToIndex(const char *binding);

} // namespace GamePad::Names
