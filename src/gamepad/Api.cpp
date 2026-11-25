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

// `C_GamePad`, as the 3.80.2 client has it (registrar 0x140C79760): its 23
// functions with their argument readers, returns and quirks.
//
// Two reader styles. The functions 3.80.2 runs as a pcall'd inner closure
// (DeleteConfig, GetAllConfigIDs, GetAllDeviceIDs, GetConfig,
// GetDeviceMappedState, GetDeviceRawState, SetConfig) raise argument errors
// naming '?'; the rest raise them naming the function.
//
// AddSDLMapping, ApplyConfigs, SetConfig and DeleteConfig are blocked in
// 3.80.2 only for tainted code in combat lockdown. 1.12 has no such
// lockdown, so they always run.

#include "Game.h"
#include "gamepad/Config.h"
#include "gamepad/Device.h"
#include "gamepad/GamePad.h"
#include "gamepad/Names.h"
#include "gamepad/State.h"
#include "lua/Arg.h"
#include "lua/MixinTable.h"

#include <cfloat>
#include <cmath>
#include <cstring>

namespace GamePad::Api {

namespace {

namespace Arg = Game::Lua::Arg;
using namespace Game::Lua;

// --- shared readers and pushers -----------------------------------------------

// A GamePadConfigID table (0x140C7ADB0): a table, with optional uint32
// vendorID / productID.
bool ConfigID(void *L, int index, const char *usage, Config::Key &key, Arg::Error &err,
              const char *vendorPath = nullptr, const char *productPath = nullptr) {
    if (Type(L, index) != TYPE_TABLE)
        return Arg::Usage(L, index, usage, &err);
    key = {0, 0};
    return Arg::FieldUInt32(L, index, "vendorID", usage, key.first, true, &err, vendorPath) &&
           Arg::FieldUInt32(L, index, "productID", usage, key.second, true, &err, productPath);
}

void PushConfigID(void *L, const Config::Key &key) {
    CreateTable(L, 0, 2);
    if (key.first != 0)
        SetFieldNumber(L, "vendorID", key.first);
    if (key.second != 0)
        SetFieldNumber(L, "productID", key.second);
}

void SetFieldFloat(void *L, const char *key, float v) { SetFieldNumber(L, key, static_cast<double>(v)); }

// A config field's button / axis byte by name: the name, "None" for 0xFF,
// null (left out) for anything else.
const char *ButtonField(uint8_t b) { return b < Names::kButtonCount ? Names::kButtonBinding[b] : b == Names::kNone ? "None" : nullptr; }
const char *AxisField(uint8_t a) { return a < Names::kAxisCount ? Names::kAxis[a] : a == Names::kNone ? "None" : nullptr; }

// The device a deviceID argument names: 0 is the active device.
uint32_t DeviceArg(uint32_t id) { return id == 0 ? ActiveID() : id; }

const Device::Slot *ConnectedSlot(uint32_t id, uint32_t maxID) {
    if (id < 1 || id > maxID)
        return nullptr;
    const Device::Slot *s = Device::SlotByID(id);
    return s != nullptr && s->connected ? s : nullptr;
}

// The name GetDeviceMappedState / GetDeviceRawState report (0x141259310).
const char *DeviceName(uint32_t id, const Device::Slot &s) {
    const std::string &cfgName = State::ConfigOf(id).name;
    return !cfgName.empty() ? cfgName.c_str() : s.name.c_str();
}

// An index argument (uint32), compared to `count` on its low byte, as 3.80.2
// compares it: 256 is index 0.
int IndexToName(void *L, const char *usage, const char *const *table, int count) {
    Arg::Error err;
    uint32_t index = 0;
    if (!Arg::UInt32(L, 1, usage, index, false, &err))
        Arg::RaiseNamed(L, err);
    const uint8_t low = static_cast<uint8_t>(index);
    if (low < count)
        PushString(L, table[low]);
    else
        PushNil(L);
    return 1;
}

// --- functions -----------------------------------------------------------------

int __fastcall Script_IsEnabled(void *L) {
    PushBool(L, Started());
    return 1;
}

int __fastcall Script_GetActiveDeviceID(void *L) {
    PushNumber(L, ActiveID());
    return 1;
}

int __fastcall Script_GetCombinedDeviceID(void *L) {
    PushNumber(L, Device::kCombinedID);
    return 1;
}

int __fastcall Script_GetAllDeviceIDs(void *L) {
    CreateTable(L, 0, 0);
    int n = 0;
    for (uint32_t id = 1; id <= Device::kSlotCount; ++id) {
        if (!Device::SlotByID(id)->connected)
            continue;
        PushNumber(L, id);
        RawSetI(L, -2, ++n);
    }
    return 1;
}

int __fastcall Script_GetAllConfigIDs(void *L) {
    Config::SeedStaging();
    const Config::Layer &staging = Config::LayerAt(Config::kStaging);
    CreateTable(L, static_cast<int>(staging.size()), 0);
    int n = 0;
    for (const auto &entry : staging) {
        PushConfigID(L, entry.first);
        RawSetI(L, -2, ++n);
    }
    return 1;
}

void PushRecord(void *L, const Config::Key &key, const Config::Record &rec) {
    CreateTable(L, 0, 8);
    if (rec.comment.set)
        SetFieldString(L, "comment", rec.comment.value.c_str());
    if (rec.name.set && !rec.name.value.empty())
        SetFieldString(L, "name", rec.name.value.c_str());
    PushString(L, "configID");
    PushConfigID(L, key);
    SetTable(L, -3);
    if (rec.labelStyle.set && rec.labelStyle.value < Names::kLabelStyleCount)
        SetFieldString(L, "labelStyle", Names::kLabelStyle[rec.labelStyle.value]);

    PushString(L, "rawButtonMappings");
    CreateTable(L, static_cast<int>(rec.rawButtons.size()), 0);
    int n = 0;
    for (const auto &[raw, m] : rec.rawButtons) {
        CreateTable(L, 0, 5);
        SetFieldNumber(L, "rawIndex", raw);
        if (m.button.set)
            if (const char *b = ButtonField(m.button.value))
                SetFieldString(L, "button", b);
        if (m.axis.set) {
            if (const char *a = AxisField(m.axis.value))
                SetFieldString(L, "axis", a);
            SetFieldFloat(L, "axisValue", m.axisValue);
        }
        if (m.comment.set)
            SetFieldString(L, "comment", m.comment.value.c_str());
        RawSetI(L, -2, ++n);
    }
    SetTable(L, -3);

    PushString(L, "rawAxisMappings");
    CreateTable(L, static_cast<int>(rec.rawAxes.size()), 0);
    n = 0;
    for (const auto &[raw, m] : rec.rawAxes) {
        CreateTable(L, 0, 3);
        SetFieldNumber(L, "rawIndex", raw);
        if (m.axis.set)
            if (const char *a = AxisField(m.axis.value))
                SetFieldString(L, "axis", a);
        if (m.comment.set)
            SetFieldString(L, "comment", m.comment.value.c_str());
        RawSetI(L, -2, ++n);
    }
    SetTable(L, -3);

    PushString(L, "axisConfigs");
    CreateTable(L, static_cast<int>(rec.axes.size()), 0);
    n = 0;
    for (const auto &[axis, c] : rec.axes) {
        const char *name = AxisField(axis);
        if (name == nullptr)
            continue;
        CreateTable(L, 0, 8);
        SetFieldString(L, "axis", name);
        if (c.shift.set)
            SetFieldFloat(L, "shift", c.shift.value);
        if (c.scale.set)
            SetFieldFloat(L, "scale", c.scale.value);
        if (c.deadzone.set)
            SetFieldFloat(L, "deadzone", c.deadzone.value);
        if (c.hasButtons) {
            SetFieldFloat(L, "buttonThreshold", c.buttonThreshold);
            if (c.buttonPos < Names::kButtonCount)
                SetFieldString(L, "buttonPos", Names::kButtonBinding[c.buttonPos]);
            if (c.buttonNeg < Names::kButtonCount)
                SetFieldString(L, "buttonNeg", Names::kButtonBinding[c.buttonNeg]);
        }
        if (c.comment.set)
            SetFieldString(L, "comment", c.comment.value.c_str());
        RawSetI(L, -2, ++n);
    }
    SetTable(L, -3);

    PushString(L, "stickConfigs");
    CreateTable(L, static_cast<int>(rec.sticks.size()), 0);
    n = 0;
    for (const auto &[stick, c] : rec.sticks) {
        if (stick >= Names::kStickCount)
            continue;
        CreateTable(L, 0, 7);
        SetFieldString(L, "stick", Names::kStick[stick]);
        if (c.axisX.set)
            if (const char *a = AxisField(c.axisX.value))
                SetFieldString(L, "axisX", a);
        if (c.axisY.set)
            if (const char *a = AxisField(c.axisY.value))
                SetFieldString(L, "axisY", a);
        if (c.deadzone.set)
            SetFieldFloat(L, "deadzone", c.deadzone.value);
        if (c.deadzoneX.set)
            SetFieldFloat(L, "deadzoneX", c.deadzoneX.value);
        if (c.deadzoneY.set)
            SetFieldFloat(L, "deadzoneY", c.deadzoneY.value);
        if (c.comment.set)
            SetFieldString(L, "comment", c.comment.value.c_str());
        RawSetI(L, -2, ++n);
    }
    SetTable(L, -3);
}

constexpr const char *kGetConfigUsage = "Usage: local config = C_GamePad.GetConfig(configID)";

int __fastcall Script_GetConfig(void *L) {
    Arg::Error err;
    Config::Key key;
    if (!ConfigID(L, 1, kGetConfigUsage, key, err))
        Arg::Raise(L, err);
    Config::SeedStaging();
    const Config::Layer &staging = Config::LayerAt(Config::kStaging);
    auto it = staging.find(key);
    if (it == staging.end()) {
        PushNil(L);
        return 1;
    }
    PushRecord(L, key, it->second);
    return 1;
}

// --- SetConfig (reader 0x140C768E0, store 0x1412712C0) -------------------------

constexpr const char *kSetConfigUsage = "Usage: C_GamePad.SetConfig(config)";

struct Reader {
    void *L;
    Arg::Error &err;

    bool OptString(int table, const char *key, const char *path, Config::Opt<std::string> &out) {
        Arg::Text t;
        if (!Arg::FieldString(L, table, key, kSetConfigUsage, t, true, &err, path))
            return false;
        if (t.data != nullptr)
            out.Set(std::string(t.data, t.size));
        return true;
    }

    bool OptName(int table, const char *key, const char *path, std::string &out, bool &set) {
        Arg::Text t;
        if (!Arg::FieldString(L, table, key, kSetConfigUsage, t, true, &err, path))
            return false;
        set = t.data != nullptr;
        if (set)
            out.assign(t.data, t.size);
        return true;
    }

    bool OptFloat(int table, const char *key, const char *path, Config::Opt<float> &out) {
        float v = 0.0f;
        bool present = false;
        if (!Present(table, key, present))
            return false;
        if (!Arg::FieldFiniteFloat(L, table, key, kSetConfigUsage, v, true, &err, path))
            return false;
        if (present)
            out.Set(v);
        return true;
    }

    bool Present(int table, const char *key, bool &present) {
        PushString(L, key);
        GetTable(L, table);
        present = Type(L, -1) > TYPE_NIL;
        SetTop(L, -2);
        return true;
    }

    // An array field: required, a table; each element a table, else the
    // error names the array.
    template <typename Fn> bool Array(int table, const char *key, Fn fn) {
        PushString(L, key);
        GetTable(L, table);
        const int arr = GetTop(L);
        if (Type(L, arr) != TYPE_TABLE) {
            Arg::Usage(L, arr, kSetConfigUsage, &err, key);
            SetTop(L, -2);
            return false;
        }
        for (int i = 1;; ++i) {
            RawGetI(L, arr, i);
            const int e = GetTop(L);
            if (Type(L, e) <= TYPE_NIL) {
                SetTop(L, -2);
                break;
            }
            if (Type(L, e) != TYPE_TABLE) {
                Arg::Usage(L, e, kSetConfigUsage, &err, key);
                SetTop(L, -3);
                return false;
            }
            const bool ok = fn(e);
            SetTop(L, -2);
            if (!ok) {
                SetTop(L, -2);
                return false;
            }
        }
        SetTop(L, -2);
        return true;
    }
};

bool ReadConfig(void *L, Config::Key &key, Config::Record &rec, Arg::Error &err) {
    if (Type(L, 1) != TYPE_TABLE)
        return Arg::Usage(L, 1, kSetConfigUsage, &err);
    Reader r{L, err};
    if (!r.OptString(1, "comment", nullptr, rec.comment))
        return false;
    std::string name;
    bool hasName = false;
    if (!r.OptName(1, "name", nullptr, name, hasName))
        return false;
    if (hasName)
        rec.name.Set(name.substr(0, 255));
    PushString(L, "configID");
    GetTable(L, 1);
    const int id = GetTop(L);
    const bool idOk = ConfigID(L, id, kSetConfigUsage, key, err, "configID,vendorID", "configID,productID");
    if (!idOk && err.field == nullptr)
        err.field = "configID";
    SetTop(L, -2);
    if (!idOk)
        return false;
    std::string style;
    bool hasStyle = false;
    if (!r.OptName(1, "labelStyle", nullptr, style, hasStyle))
        return false;
    if (hasStyle) {
        const uint8_t s = Names::ParseLabelStyle(style.c_str());
        if (s != Names::kNone)
            rec.labelStyle.Set(s);
    }
    auto button = [](const std::string &s) { return Names::ParseButton(s.c_str()); };
    if (!r.Array(1, "rawButtonMappings", [&](int e) {
            uint32_t raw = 0;
            if (!Arg::FieldUInt32(L, e, "rawIndex", kSetConfigUsage, raw, false, &err,
                                  "rawButtonMappings,rawIndex"))
                return false;
            Config::RawButtonMapping m;
            std::string b, a;
            bool hasB = false, hasA = false;
            Config::Opt<float> value;
            if (!r.OptName(e, "button", "rawButtonMappings,button", b, hasB) ||
                !r.OptName(e, "axis", "rawButtonMappings,axis", a, hasA) ||
                !r.OptFloat(e, "axisValue", "rawButtonMappings,axisValue", value) ||
                !r.OptString(e, "comment", "rawButtonMappings,comment", m.comment))
                return false;
            if (hasB && button(b) != Names::kNone)
                m.button.Set(button(b));
            if (hasA && value.set && Names::ParseAxis(a.c_str()) != Names::kNone) {
                m.axis.Set(Names::ParseAxis(a.c_str()));
                m.axisValue = value.value;
            }
            Config::MergeRecord(rec, [&] {
                Config::Record one;
                one.rawButtons[raw] = m;
                return one;
            }());
            return true;
        }))
        return false;
    if (!r.Array(1, "rawAxisMappings", [&](int e) {
            uint32_t raw = 0;
            if (!Arg::FieldUInt32(L, e, "rawIndex", kSetConfigUsage, raw, false, &err, "rawAxisMappings,rawIndex"))
                return false;
            Config::RawAxisMapping m;
            std::string a;
            bool hasA = false;
            if (!r.OptName(e, "axis", "rawAxisMappings,axis", a, hasA) ||
                !r.OptString(e, "comment", "rawAxisMappings,comment", m.comment))
                return false;
            if (hasA && Names::ParseAxis(a.c_str()) != Names::kNone)
                m.axis.Set(Names::ParseAxis(a.c_str()));
            Config::Record one;
            one.rawAxes[raw] = m;
            Config::MergeRecord(rec, one);
            return true;
        }))
        return false;
    if (!r.Array(1, "axisConfigs", [&](int e) {
            Arg::Text axisName;
            if (!Arg::FieldString(L, e, "axis", kSetConfigUsage, axisName, false, &err, "axisConfigs,axis"))
                return false;
            const std::string axis(axisName.data, axisName.size);
            Config::AxisConfig c;
            Config::Opt<float> threshold;
            std::string pos, neg;
            bool hasPos = false, hasNeg = false;
            if (!r.OptFloat(e, "shift", "axisConfigs,shift", c.shift) ||
                !r.OptFloat(e, "scale", "axisConfigs,scale", c.scale) ||
                !r.OptFloat(e, "deadzone", "axisConfigs,deadzone", c.deadzone) ||
                !r.OptFloat(e, "buttonThreshold", "axisConfigs,buttonThreshold", threshold) ||
                !r.OptName(e, "buttonPos", "axisConfigs,buttonPos", pos, hasPos) ||
                !r.OptName(e, "buttonNeg", "axisConfigs,buttonNeg", neg, hasNeg) ||
                !r.OptString(e, "comment", "axisConfigs,comment", c.comment))
                return false;
            const uint8_t a = Names::ParseAxis(axis.c_str());
            if (a == Names::kNone)
                return true; // an unknown axis skips the entry
            if (threshold.set && (hasPos || hasNeg)) {
                c.hasButtons = true;
                c.buttonThreshold = threshold.value;
                c.buttonPos = hasPos ? button(pos) : Names::kNone;
                c.buttonNeg = hasNeg ? button(neg) : Names::kNone;
            }
            Config::Record one;
            one.axes[a] = c;
            Config::MergeRecord(rec, one);
            return true;
        }))
        return false;
    return r.Array(1, "stickConfigs", [&](int e) {
        Arg::Text stickName;
        if (!Arg::FieldString(L, e, "stick", kSetConfigUsage, stickName, false, &err, "stickConfigs,stick"))
            return false;
        const std::string stick(stickName.data, stickName.size);
        Config::StickConfig c;
        std::string x, y;
        bool hasX = false, hasY = false;
        if (!r.OptName(e, "axisX", "stickConfigs,axisX", x, hasX) ||
            !r.OptName(e, "axisY", "stickConfigs,axisY", y, hasY) ||
            !r.OptFloat(e, "deadzone", "stickConfigs,deadzone", c.deadzone) ||
            !r.OptFloat(e, "deadzoneX", "stickConfigs,deadzoneX", c.deadzoneX) ||
            !r.OptFloat(e, "deadzoneY", "stickConfigs,deadzoneY", c.deadzoneY) ||
            !r.OptString(e, "comment", "stickConfigs,comment", c.comment))
            return false;
        const uint8_t s = Names::ParseStick(stick.c_str());
        if (s == Names::kNone)
            return true; // an unknown stick skips the entry
        if (hasX && Names::ParseAxis(x.c_str()) != Names::kNone)
            c.axisX.Set(Names::ParseAxis(x.c_str()));
        if (hasY && Names::ParseAxis(y.c_str()) != Names::kNone)
            c.axisY.Set(Names::ParseAxis(y.c_str()));
        Config::Record one;
        one.sticks[s] = c;
        Config::MergeRecord(rec, one);
        return true;
    });
}

int __fastcall Script_SetConfig(void *L) {
    Arg::Error err;
    bool ok;
    {
        Config::Key key;
        Config::Record rec;
        ok = ReadConfig(L, key, rec, err);
        if (ok) {
            Config::SeedStaging();
            Config::MergeRecord(Config::LayerAt(Config::kStaging)[key], rec);
        }
    } // the record is gone before a raise, which is a longjmp
    if (!ok)
        Arg::Raise(L, err);
    return 0;
}

int __fastcall Script_DeleteConfig(void *L) {
    Arg::Error err;
    Config::Key key;
    if (!ConfigID(L, 1, "Usage: C_GamePad.DeleteConfig(configID)", key, err))
        Arg::Raise(L, err);
    // 3.80.2 does not seed the staging layer first, so before any staging
    // read this deletes nothing.
    Config::LayerAt(Config::kStaging).erase(key);
    return 0;
}

int __fastcall Script_ApplyConfigs(void *L) {
    (void)L;
    ApplyConfigs();
    return 0;
}

int __fastcall Script_GetDeviceMappedState(void *L) {
    constexpr const char *kUsage = "Usage: local state = C_GamePad.GetDeviceMappedState([deviceID])";
    Arg::Error err;
    uint32_t id = 0;
    if (!Arg::UInt32(L, 1, kUsage, id, true, &err))
        Arg::Raise(L, err);
    id = DeviceArg(id);
    const Device::Slot *s = Started() ? ConnectedSlot(id, Device::kSlotCount) : nullptr;
    if (s == nullptr) {
        PushNil(L);
        return 1;
    }
    const State::Mapped &m = State::MappedOf(id);
    const uint8_t style = State::ConfigOf(id).labelStyle;
    CreateTable(L, 0, 8);
    SetFieldString(L, "name", DeviceName(id, *s));
    SetFieldString(L, "labelStyle", Names::kLabelStyle[style < Names::kLabelStyleCount ? style : 0]);
    SetFieldNumber(L, "buttonCount", Names::kButtonCount);
    SetFieldNumber(L, "axisCount", Names::kAxisCount);
    SetFieldNumber(L, "stickCount", Names::kStickCount);
    PushString(L, "buttons");
    CreateTable(L, Names::kButtonCount, 0);
    for (int i = 0; i < Names::kButtonCount; ++i) {
        PushBool(L, (m.buttons >> i) & 1u);
        RawSetI(L, -2, i + 1);
    }
    SetTable(L, -3);
    PushString(L, "axes");
    CreateTable(L, Names::kAxisCount, 0);
    for (int i = 0; i < Names::kAxisCount; ++i) {
        PushNumber(L, static_cast<double>(m.axes[i]));
        RawSetI(L, -2, i + 1);
    }
    SetTable(L, -3);
    PushString(L, "sticks");
    CreateTable(L, Names::kStickCount, 0);
    for (int i = 0; i < Names::kStickCount; ++i) {
        CreateTable(L, 0, 3);
        SetFieldFloat(L, "x", m.sticks[i].x);
        SetFieldFloat(L, "y", m.sticks[i].y);
        SetFieldFloat(L, "len", m.sticks[i].len);
        RawSetI(L, -2, i + 1);
    }
    SetTable(L, -3);
    return 1;
}

int __fastcall Script_GetDeviceRawState(void *L) {
    constexpr const char *kUsage = "Usage: local rawState = C_GamePad.GetDeviceRawState(deviceID)";
    Arg::Error err;
    uint32_t id = 0;
    if (!Arg::UInt32(L, 1, kUsage, id, false, &err))
        Arg::Raise(L, err);
    id = DeviceArg(id);
    const Device::Slot *s = Started() ? ConnectedSlot(id, 4) : nullptr; // not the combined device
    if (s == nullptr) {
        PushNil(L);
        return 1;
    }
    const Device::Raw &raw = Device::RawOf(static_cast<int>(id) - 1);
    CreateTable(L, 0, 7);
    SetFieldString(L, "name", DeviceName(id, *s));
    SetFieldNumber(L, "vendorID", s->vendor);
    SetFieldNumber(L, "productID", s->product);
    SetFieldNumber(L, "rawButtonCount", s->buttonCount);
    SetFieldNumber(L, "rawAxisCount", s->axisCount);
    PushString(L, "rawButtons");
    CreateTable(L, static_cast<int>(s->buttonCount), 0);
    for (uint32_t i = 0; i < s->buttonCount && i < 32; ++i) {
        PushBool(L, (raw.buttons >> i) & 1u);
        RawSetI(L, -2, static_cast<int>(i) + 1);
    }
    SetTable(L, -3);
    PushString(L, "rawAxes");
    CreateTable(L, static_cast<int>(s->axisCount), 0);
    for (uint32_t i = 0; i < s->axisCount && i < Names::kAxisCount; ++i) {
        PushNumber(L, static_cast<double>(raw.cur[i]));
        RawSetI(L, -2, static_cast<int>(i) + 1);
    }
    SetTable(L, -3);
    return 1;
}

int __fastcall Script_GetPowerLevel(void *L) {
    Arg::Error err;
    uint32_t id = 0;
    if (!Arg::UInt32(L, 1, "Usage: local powerLevel = C_GamePad.GetPowerLevel([deviceID])", id, true, &err))
        Arg::RaiseNamed(L, err);
    const Device::Slot *s = ConnectedSlot(DeviceArg(id), Device::kSlotCount);
    PushNumber(L, s != nullptr ? s->power : Device::kPowerUnknown);
    return 1;
}

int __fastcall Script_GetLedColor(void *L) {
    const uint32_t rgb = LedColor();
    MixinTable::Push(L, "ColorMixin");
    SetFieldFloat(L, "r", static_cast<float>((rgb >> 16) & 0xFF) * 0.003921569f);
    SetFieldFloat(L, "g", static_cast<float>((rgb >> 8) & 0xFF) * 0.003921569f);
    SetFieldFloat(L, "b", static_cast<float>(rgb & 0xFF) * 0.003921569f);
    return 1;
}

int __fastcall Script_SetLedColor(void *L) {
    constexpr const char *kUsage = "Usage: C_GamePad.SetLedColor(color)";
    // 0x140B0EF20: a table with numbers r, g and b; anything else is the
    // usage error. Alpha is not read.
    uint32_t rgb = 0;
    bool ok = Type(L, 1) == TYPE_TABLE;
    for (const char *key : {"r", "g", "b"}) {
        if (!ok)
            break;
        PushString(L, key);
        GetTable(L, 1);
        const double v = IsNumber(L, -1) ? ToNumber(L, -1) : NAN;
        SetTop(L, -2);
        if (!std::isfinite(v) || v < -FLT_MAX || v > FLT_MAX) {
            ok = false;
            break;
        }
        float c = static_cast<float>(v);
        c = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
        rgb = (rgb << 8) | (static_cast<uint32_t>(static_cast<int64_t>(c * 255.0f + 0.5f)) & 0xFF);
    }
    if (!ok) {
        Arg::Error err;
        Arg::Usage(L, 1, kUsage, &err);
        Arg::RaiseNamed(L, err);
    }
    SetLedOverride(rgb);
    return 0;
}

int __fastcall Script_ClearLedColor(void *L) {
    (void)L;
    ClearLedOverride();
    return 0;
}

int __fastcall Script_SetVibration(void *L) {
    constexpr const char *kUsage = "Usage: C_GamePad.SetVibration(vibrationType, intensity)";
    Arg::Error err;
    Arg::Text type;
    float intensity = 0.0f;
    if (!Arg::StringView(L, 1, kUsage, type, false, &err) || !Arg::FiniteFloat(L, 2, kUsage, intensity, false, &err))
        Arg::RaiseNamed(L, err);
    // 0x143B83BC8; an unknown type does nothing.
    static const char *const kTypes[] = {"Low", "High", "LTrigger", "RTrigger"};
    for (int i = 0; i < 4; ++i)
        if (_stricmp(type.data, kTypes[i]) == 0) {
            SetVibration(i, intensity);
            break;
        }
    return 0;
}

int __fastcall Script_StopVibration(void *L) {
    (void)L;
    StopVibration();
    return 0;
}

int __fastcall Script_AddSDLMapping(void *L) {
    constexpr const char *kUsage = "Usage: local success = C_GamePad.AddSDLMapping(platform, mapping)";
    Arg::Error err;
    // The platform reader 0x140C7B260: a number whose low byte is 0 or 1.
    const bool platformOk =
        IsNumber(L, 1) && static_cast<uint8_t>(static_cast<int64_t>(ToNumber(L, 1))) <= 1;
    Arg::Text mapping;
    if (!platformOk) {
        Arg::Usage(L, 1, kUsage, &err);
        Arg::RaiseNamed(L, err);
    }
    if (!Arg::String(L, 2, kUsage, mapping, false, &err))
        Arg::RaiseNamed(L, err);
    const bool windows = static_cast<uint8_t>(static_cast<int64_t>(ToNumber(L, 1))) == 0;
    // SDL keeps mappings only while it runs.
    PushBool(L, windows && Device::Running() && Device::AddMapping(mapping.data) >= 0);
    return 1;
}

int __fastcall Script_ButtonBindingToIndex(void *L) {
    Arg::Error err;
    Arg::Text binding;
    if (!Arg::String(L, 1, "Usage: local buttonIndex = C_GamePad.ButtonBindingToIndex(bindingName)", binding,
                     false, &err))
        Arg::RaiseNamed(L, err);
    const uint8_t i = Names::BindingToIndex(binding.data);
    if (i == Names::kNone)
        PushNil(L);
    else
        PushNumber(L, i);
    return 1;
}

int __fastcall Script_ButtonIndexToBinding(void *L) {
    return IndexToName(L, "Usage: local bindingName = C_GamePad.ButtonIndexToBinding(buttonIndex)",
                       Names::kButtonBinding, Names::kButtonCount);
}

int __fastcall Script_ButtonIndexToConfigName(void *L) {
    return IndexToName(L, "Usage: local configName = C_GamePad.ButtonIndexToConfigName(buttonIndex)",
                       EverStarted() ? Names::kButtonBinding : Names::kButtonConfig, Names::kButtonCount);
}

int __fastcall Script_AxisIndexToConfigName(void *L) {
    return IndexToName(L, "Usage: local configName = C_GamePad.AxisIndexToConfigName(axisIndex)", Names::kAxis,
                       Names::kAxisCount);
}

int __fastcall Script_StickIndexToConfigName(void *L) {
    return IndexToName(L, "Usage: local configName = C_GamePad.StickIndexToConfigName(stickIndex)", Names::kStick,
                       Names::kStickCount);
}

// --- documentation ----------------------------------------------------------------

using Game::Doc::Opt;
using Game::Doc::Req;

const Game::Doc::Field kConfigIDArg[] = {Req("configID", "GamePadConfigID")};
const Game::Doc::Field kConfigArg[] = {Req("config", "GamePadConfig")};
const Game::Doc::Field kDeviceIDOptArg[] = {Opt("deviceID", "number", nullptr, "The device; the active one when nil or 0.")};
const Game::Doc::Field kDeviceIDArg[] = {Req("deviceID", "number", "The device; 0 is the active one.")};
const Game::Doc::Field kButtonIndexArg[] = {Req("buttonIndex", "number", "0 to 31.")};
const Game::Doc::Field kAxisIndexArg[] = {Req("axisIndex", "number", "0 to 9.")};
const Game::Doc::Field kStickIndexArg[] = {Req("stickIndex", "number", "0 to 7.")};
const Game::Doc::Field kBindingArg[] = {Req("bindingName", "cstring", "A gamepad key, such as PAD1.")};
const Game::Doc::Field kColorArg[] = {Req("color", "colorRGB")};
const Game::Doc::Field kVibrationArgs[] = {
    Req("vibrationType", "cstring", "Low, High, LTrigger or RTrigger."),
    Req("intensity", "number", "0 to 1, scaled by GamePadVibrationStrength."),
};
const Game::Doc::Field kMappingArgs[] = {
    Req("platform", "ClientPlatformType"),
    Req("mapping", "cstring", "An SDL gamepad mapping string."),
};

const Game::Doc::Field kEnabledRet[] = {Req("enabled", "bool")};
const Game::Doc::Field kDeviceIDRet[] = {Req("deviceID", "number")};
const Game::Doc::Field kDeviceIDsRet[] = {Req("deviceIDs", "table")};
const Game::Doc::Field kConfigIDsRet[] = {Req("configIDs", "table")};
const Game::Doc::Field kConfigRet[] = {Opt("config", "GamePadConfig")};
const Game::Doc::Field kMappedRet[] = {Opt("state", "GamePadMappedState")};
const Game::Doc::Field kRawRet[] = {Opt("rawState", "GamePadRawState")};
const Game::Doc::Field kPowerRet[] = {Req("powerLevel", "GamePadPowerLevel")};
const Game::Doc::Field kColorRet[] = {Req("color", "colorRGB")};
const Game::Doc::Field kSuccessRet[] = {Req("success", "bool")};
const Game::Doc::Field kBindingRet[] = {Opt("bindingName", "cstring")};
const Game::Doc::Field kButtonIndexRet[] = {Opt("buttonIndex", "number")};
const Game::Doc::Field kConfigNameRet[] = {Opt("configName", "cstring")};

const Game::Doc::Function kIsEnabled{"Returns whether gamepad support is on: GamePadEnable is set and it started.",
                                     {}, kEnabledRet};
const Game::Doc::Function kGetActiveDeviceID{
    "Returns the device gamepad input comes from: GamePadSingleActiveID, or the combined device.", {}, kDeviceIDRet};
const Game::Doc::Function kGetCombinedDeviceID{"Returns the ID of the device that combines every gamepad.", {},
                                               kDeviceIDRet};
const Game::Doc::Function kGetAllDeviceIDs{"Returns the IDs of the connected devices, the combined device included.",
                                           {}, kDeviceIDsRet};
const Game::Doc::Function kGetAllConfigIDs{"Returns the IDs of the configs set from Lua.", {}, kConfigIDsRet};
const Game::Doc::Function kGetConfig{"Returns a config set from Lua.", kConfigIDArg, kConfigRet};
const Game::Doc::Function kSetConfig{
    "Merges a config into the configs set from Lua. ApplyConfigs makes them take effect.", kConfigArg, {}};
const Game::Doc::Function kDeleteConfig{"Removes a config set from Lua.", kConfigIDArg, {}};
const Game::Doc::Function kApplyConfigs{
    "Applies the configs set from Lua to every device. They are saved on logout and exit.", {}, {}};
const Game::Doc::Function kGetDeviceMappedState{"Returns a device's buttons, axes and sticks after its config.",
                                                kDeviceIDOptArg, kMappedRet};
const Game::Doc::Function kGetDeviceRawState{"Returns a gamepad's raw buttons and axes.", kDeviceIDArg, kRawRet};
const Game::Doc::Function kGetPowerLevel{"Returns a device's battery level.", kDeviceIDOptArg, kPowerRet};
const Game::Doc::Function kGetLedColor{"Returns the colour of the gamepad light.", {}, kColorRet};
const Game::Doc::Function kSetLedColor{"Sets the colour of the gamepad light while you are in the world.", kColorArg,
                                       {}};
const Game::Doc::Function kClearLedColor{"Returns the gamepad light to its default colour.", {}, {}};
const Game::Doc::Function kSetVibration{"Vibrates the gamepad for half a second.", kVibrationArgs, {}};
const Game::Doc::Function kStopVibration{"Stops the gamepad vibration.", {}, {}};
const Game::Doc::Function kAddSDLMapping{"Adds an SDL mapping for a gamepad model until the client closes.",
                                         kMappingArgs, kSuccessRet};
const Game::Doc::Function kButtonBindingToIndex{"Returns the button index of a gamepad key.", kBindingArg,
                                                kButtonIndexRet};
const Game::Doc::Function kButtonIndexToBinding{"Returns the gamepad key of a button index.", kButtonIndexArg,
                                                kBindingRet};
const Game::Doc::Function kButtonIndexToConfigName{"Returns the config name of a button index.", kButtonIndexArg,
                                                   kConfigNameRet};
const Game::Doc::Function kAxisIndexToConfigName{"Returns the config name of an axis index.", kAxisIndexArg,
                                                 kConfigNameRet};
const Game::Doc::Function kStickIndexToConfigName{"Returns the config name of a stick index.", kStickIndexArg,
                                                  kConfigNameRet};

const Game::Doc::Field kConfigIDFields[] = {Opt("vendorID", "number"), Opt("productID", "number")};
const Game::Doc::Structure kConfigIDDoc{"GamePadConfigID", "GamePad", kConfigIDFields};
const Game::Doc::Field kStickFields[] = {Req("x", "number"), Req("y", "number"), Req("len", "number")};
const Game::Doc::Structure kStickDoc{"GamePadStick", "GamePad", kStickFields};
const Game::Doc::Field kMappedFields[] = {
    Req("name", "string"),        Req("labelStyle", "string"), Req("buttonCount", "number"),
    Req("axisCount", "number"),   Req("stickCount", "number"), Req("buttons", "table"),
    Req("axes", "table"),         Req("sticks", "table"),
};
const Game::Doc::Structure kMappedDoc{"GamePadMappedState", "GamePad", kMappedFields};
const Game::Doc::Field kRawFields[] = {
    Req("name", "string"),           Req("vendorID", "number"),     Req("productID", "number"),
    Req("rawButtonCount", "number"), Req("rawAxisCount", "number"), Req("rawButtons", "table"),
    Req("rawAxes", "table"),
};
const Game::Doc::Structure kRawDoc{"GamePadRawState", "GamePad", kRawFields};
const Game::Doc::Field kConfigFields[] = {
    Opt("comment", "string"),          Opt("name", "string"),
    Req("configID", "GamePadConfigID"), Opt("labelStyle", "string"),
    Req("rawButtonMappings", "table"), Req("rawAxisMappings", "table"),
    Req("axisConfigs", "table"),       Req("stickConfigs", "table"),
};
const Game::Doc::Structure kConfigDoc{"GamePadConfig", "GamePad", kConfigFields};

const Game::Lua::EnumIntegerEntry kPowerLevel[] = {
    {"Critical", 0}, {"Low", 1}, {"Medium", 2}, {"High", 3}, {"Wired", 4}, {"Unknown", 5},
};
const Game::Lua::EnumIntegerEntry kPlatform[] = {{"Windows", 0}, {"Macintosh", 1}};

struct Entry {
    const char *name;
    Game::Lua::CFunction fn;
    const Game::Doc::Function *doc;
};

const Entry kFunctions[] = {
    {"AddSDLMapping", &Script_AddSDLMapping, &kAddSDLMapping},
    {"ApplyConfigs", &Script_ApplyConfigs, &kApplyConfigs},
    {"AxisIndexToConfigName", &Script_AxisIndexToConfigName, &kAxisIndexToConfigName},
    {"ButtonBindingToIndex", &Script_ButtonBindingToIndex, &kButtonBindingToIndex},
    {"ButtonIndexToBinding", &Script_ButtonIndexToBinding, &kButtonIndexToBinding},
    {"ButtonIndexToConfigName", &Script_ButtonIndexToConfigName, &kButtonIndexToConfigName},
    {"ClearLedColor", &Script_ClearLedColor, &kClearLedColor},
    {"DeleteConfig", &Script_DeleteConfig, &kDeleteConfig},
    {"GetActiveDeviceID", &Script_GetActiveDeviceID, &kGetActiveDeviceID},
    {"GetAllConfigIDs", &Script_GetAllConfigIDs, &kGetAllConfigIDs},
    {"GetAllDeviceIDs", &Script_GetAllDeviceIDs, &kGetAllDeviceIDs},
    {"GetCombinedDeviceID", &Script_GetCombinedDeviceID, &kGetCombinedDeviceID},
    {"GetConfig", &Script_GetConfig, &kGetConfig},
    {"GetDeviceMappedState", &Script_GetDeviceMappedState, &kGetDeviceMappedState},
    {"GetDeviceRawState", &Script_GetDeviceRawState, &kGetDeviceRawState},
    {"GetLedColor", &Script_GetLedColor, &kGetLedColor},
    {"GetPowerLevel", &Script_GetPowerLevel, &kGetPowerLevel},
    {"IsEnabled", &Script_IsEnabled, &kIsEnabled},
    {"SetConfig", &Script_SetConfig, &kSetConfig},
    {"SetLedColor", &Script_SetLedColor, &kSetLedColor},
    {"SetVibration", &Script_SetVibration, &kSetVibration},
    {"StickIndexToConfigName", &Script_StickIndexToConfigName, &kStickIndexToConfigName},
    {"StopVibration", &Script_StopVibration, &kStopVibration},
};

// Environment All: the same table on glue and in the world.
void RegisterLuaFunctions() {
    for (const Entry &e : kFunctions)
        Game::Lua::RegisterTableFunction("C_GamePad", e.name, e.fn, e.doc);
    Game::Lua::RegisterIntegerEnum("Enum", "GamePadPowerLevel", kPowerLevel, "GamePad");
    Game::Lua::RegisterIntegerEnum("Enum", "ClientPlatformType", kPlatform, "Platform");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::GlueModuleAutoRegister _glueAutoreg{&RegisterLuaFunctions};

} // namespace

} // namespace GamePad::Api
