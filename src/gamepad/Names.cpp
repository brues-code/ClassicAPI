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

#include "gamepad/Names.h"

#include <cstring>

namespace GamePad::Names {

const char *const kButtonBinding[kButtonCount] = {
    "PADDUP",        "PADDRIGHT",     "PADDDOWN",      "PADDLEFT",       "PAD1",
    "PAD2",          "PAD3",          "PAD4",          "PAD5",           "PAD6",
    "PADLSTICK",     "PADRSTICK",     "PADLSHOULDER",  "PADRSHOULDER",   "PADLTRIGGER",
    "PADRTRIGGER",   "PADLSTICKUP",   "PADLSTICKRIGHT", "PADLSTICKDOWN", "PADLSTICKLEFT",
    "PADRSTICKUP",   "PADRSTICKRIGHT", "PADRSTICKDOWN", "PADRSTICKLEFT", "PADPADDLE1",
    "PADPADDLE2",    "PADPADDLE3",    "PADPADDLE4",    "PADFORWARD",     "PADBACK",
    "PADSYSTEM",     "PADSOCIAL",
};

const char *const kButtonConfig[kButtonCount] = {
    "Up",        "Right",      "Down",       "Left",       "Face1",    "Face2",     "Face3",
    "Face4",     "Face5",      "Face6",      "LStickIn",   "RStickIn", "LShoulder", "RShoulder",
    "LTrigger",  "RTrigger",   "LStickUp",   "LStickRight", "LStickDown", "LStickLeft",
    "RStickUp",  "RStickRight", "RStickDown", "RStickLeft", "Paddle1",  "Paddle2",   "Paddle3",
    "Paddle4",   "Forward",    "Back",       "System",     "Social",
};

const char *const kAxis[kAxisCount] = {
    "LStickX", "LStickY", "RStickX", "RStickY", "GStickX",
    "GStickY", "PStickX", "PStickY", "LTrigger", "RTrigger",
};

const char *const kStick[kStickCount] = {
    "Left", "Right", "Gyro", "Pad", "Movement", "Camera", "Look", "Cursor",
};

const char *const kLabelStyle[kLabelStyleCount] = {"Generic", "Letters", "Reverse", "Shapes"};

namespace {

template <int N> uint8_t Find(const char *const (&table)[N], const char *name) {
    if (name == nullptr)
        return kNone;
    for (int i = 0; i < N; ++i)
        if (_stricmp(table[i], name) == 0)
            return static_cast<uint8_t>(i);
    return kNone;
}

} // namespace

uint8_t ParseButton(const char *name) {
    const uint8_t b = Find(kButtonBinding, name);
    return b != kNone ? b : Find(kButtonConfig, name);
}

uint8_t ParseAxis(const char *name) { return Find(kAxis, name); }
uint8_t ParseStick(const char *name) { return Find(kStick, name); }
uint8_t ParseLabelStyle(const char *name) { return Find(kLabelStyle, name); }

const char *ButtonName(uint8_t button) {
    return button < kButtonCount ? kButtonBinding[button] : nullptr;
}

uint8_t BindingToIndex(const char *binding) {
    if (binding == nullptr || _strnicmp(binding, "PAD", 3) != 0)
        return kNone;
    for (int i = 0; i < kButtonCount; ++i)
        if (_stricmp(kButtonBinding[i] + 3, binding + 3) == 0)
            return static_cast<uint8_t>(i);
    return kNone;
}

} // namespace GamePad::Names
