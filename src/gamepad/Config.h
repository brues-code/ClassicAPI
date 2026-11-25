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

#include "gamepad/Names.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

// GamePad configs, as the 3.80.2 client keeps them: per (vendorID, productID)
// records in four layers, merged field by field into one resolved config per
// device.
//
//   layer 0  Default  the generated generic config ("All Devices") and one
//                     default per connected device (WTF\GamePadConfig_Default.json
//                     is this layer written out, for reference)
//   layer 1  Files    every other WTF\GamePadConfig_*.json, in sorted order
//   layer 2  Addons   WTF\GamePadConfig_Addons.json, saved on logout and exit
//   layer 3  Staging  what C_GamePad reads and writes; ApplyConfigs copies it
//                     over layer 2
//
// vendorID 0 is the generic config, productID 0 a vendor-wide one. A device
// resolves, for each of layers 0 to 2, generic, then vendor, then vendor +
// product (0x141259480), and a later source overrides only the fields it sets
// (0x141266F70).
//
// 3.80.2 keeps each layer in a hash map, so its GetAllConfigIDs order and the
// order of a config's entries are hash order. Ours are sorted maps: the same
// records, in ascending (vendorID, productID) and index order.
namespace GamePad::Config {

template <typename T> struct Opt {
    bool set = false;
    T value{};
    void Set(T v) { set = true; value = std::move(v); }
};

struct RawButtonMapping {
    Opt<uint8_t> button;    // a button index, or Names::kNone for "None"
    Opt<uint8_t> axis;      // set together with axisValue
    float axisValue = 0.0f;
    Opt<std::string> comment;
};

struct RawAxisMapping {
    Opt<uint8_t> axis;
    Opt<std::string> comment;
};

struct AxisConfig {
    Opt<float> shift, scale, deadzone;
    // The button group: buttonThreshold with buttonPos / buttonNeg (each a
    // button index or Names::kNone). Set as one.
    bool hasButtons = false;
    float buttonThreshold = 0.0f;
    uint8_t buttonPos = Names::kNone, buttonNeg = Names::kNone;
    Opt<std::string> comment;
};

struct StickConfig {
    Opt<uint8_t> axisX, axisY;
    Opt<float> deadzone, deadzoneX, deadzoneY;
    Opt<std::string> comment;
};

struct Record {
    Opt<std::string> comment;
    Opt<std::string> name;
    Opt<uint8_t> labelStyle;
    std::map<uint32_t, RawButtonMapping> rawButtons; // by rawIndex
    std::map<uint32_t, RawAxisMapping> rawAxes;      // by rawIndex
    std::map<uint8_t, AxisConfig> axes;              // by axis
    std::map<uint8_t, StickConfig> sticks;           // by stick
};

using Key = std::pair<uint32_t, uint32_t>; // (vendorID, productID)
using Layer = std::map<Key, Record>;

enum : int { kDefault = 0, kFiles = 1, kAddons = 2, kStaging = 3, kLayerCount = 4 };

Layer &LayerAt(int layer);
void ClearAll();
// Clears `dst` and copies `src` into it (0x141267A00).
void CopyLayer(int src, int dst);
// Merges `src` into `dst` field by field; entries merge by key (the
// SetConfig store 0x1412712C0 and the file reader both upsert).
void MergeRecord(Record &dst, const Record &src);

// Lua staging is seeded from layer 2 on its first use after a (re)load: every
// staging read and SetConfig seeds, DeleteConfig and ApplyConfigs do not.
void SeedStaging();
void ResetStagingSeed();

// --- the resolved config of one device (0x1468BE730, stride 0x2D8) --------

struct ButtonAxis {
    uint32_t rawButton;
    uint8_t axis;
    float value;
};

struct AxisButton {
    uint8_t axis;
    uint8_t pos, neg;
    float threshold;
};

struct Resolved {
    std::string name;
    uint8_t labelStyle = Names::Generic;
    uint32_t rawButtonCount = 0;
    uint32_t rawButtonMap[Names::kButtonCount] = {}; // mapped-button mask per raw button
    std::vector<ButtonAxis> buttonAxis;
    uint32_t rawAxisCount = 0;
    uint8_t rawAxisMap[Names::kAxisCount] = {};
    float shift[Names::kAxisCount] = {};
    float scale[Names::kAxisCount] = {};
    float deadzone[Names::kAxisCount] = {};
    float rescale[Names::kAxisCount] = {};
    std::vector<AxisButton> axisButtons;
    uint8_t stickX[Names::kStickCount] = {};
    uint8_t stickY[Names::kStickCount] = {};
    float stickDeadzoneX[Names::kStickCount] = {};
    float stickDeadzoneY[Names::kStickCount] = {};
    float stickDeadzone[Names::kStickCount] = {};

    Resolved(); // the defaults of 0x141266C70
};

// What a device's default config is built from (0x141274B40).
enum class PadType { Other, Xbox360, XboxOne, PS3, PS4, PS5, SwitchPro };

struct DeviceDesc {
    uint32_t vendor = 0, product = 0;
    bool gamepad = false;   // opened as an SDL gamepad, else a raw joystick
    bool hats = false;      // a raw joystick's last four buttons are its hat
    bool touchpad = false;
    PadType type = PadType::Other;
    uint32_t buttonCount = 0, axisCount = 0;
    const char *name = "";
};

// Rebuilds layer 0's generic config (0x141268150), with the stick-direction
// buttons when `stickAxisButtons` (the config reload, 0x141263280).
void BuildGeneric(bool stickAxisButtons);

// Resolves a physical device: (re)builds its layer-0 default, then merges
// layers 0..2 over the defaults.
void ResolveDevice(const DeviceDesc &device, Resolved &out);
// Resolves the combined device: generic configs only, with every raw mapping
// and axis transform cleared (sticks, name and label style kept).
void ResolveCombined(Resolved &out);

// --- files (WTF\) -----------------------------------------------------------

// Writes layer 0 to WTF\GamePadConfig_Default.json (0x141262870).
void WriteDefaultFile();
// Loads every user file into layer 1 (0x141262AF0). True when a file loaded.
bool LoadFiles();
// Loads / saves layer 2, WTF\GamePadConfig_Addons.json (0x141263100 / 0x141263160).
bool LoadAddons();
void SaveAddons();

} // namespace GamePad::Config
