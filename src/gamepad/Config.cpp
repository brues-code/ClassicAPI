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

#include "gamepad/Config.h"

#include "Game.h"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace GamePad::Config {

namespace {

Layer g_layers[kLayerCount];
bool g_stagingSeeded = false;

constexpr const char *kDefaultFile = "WTF\\GamePadConfig_Default.json";
constexpr const char *kAddonsFile = "WTF\\GamePadConfig_Addons.json";
constexpr int kConfigVersion = 4;

float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

template <typename T> void Take(Opt<T> &dst, const Opt<T> &src) {
    if (src.set)
        dst = src;
}

} // namespace

Layer &LayerAt(int layer) { return g_layers[layer]; }

void ClearAll() {
    for (Layer &l : g_layers)
        l.clear();
}

void CopyLayer(int src, int dst) { g_layers[dst] = g_layers[src]; }

void MergeRecord(Record &dst, const Record &src) {
    Take(dst.comment, src.comment);
    Take(dst.name, src.name);
    Take(dst.labelStyle, src.labelStyle);
    for (const auto &[raw, m] : src.rawButtons) {
        RawButtonMapping &d = dst.rawButtons[raw];
        Take(d.button, m.button);
        if (m.axis.set) {
            d.axis = m.axis;
            d.axisValue = m.axisValue;
        }
        Take(d.comment, m.comment);
    }
    for (const auto &[raw, m] : src.rawAxes) {
        RawAxisMapping &d = dst.rawAxes[raw];
        Take(d.axis, m.axis);
        Take(d.comment, m.comment);
    }
    for (const auto &[axis, a] : src.axes) {
        AxisConfig &d = dst.axes[axis];
        Take(d.shift, a.shift);
        Take(d.scale, a.scale);
        Take(d.deadzone, a.deadzone);
        if (a.hasButtons) {
            d.hasButtons = true;
            d.buttonThreshold = a.buttonThreshold;
            d.buttonPos = a.buttonPos;
            d.buttonNeg = a.buttonNeg;
        }
        Take(d.comment, a.comment);
    }
    for (const auto &[stick, s] : src.sticks) {
        StickConfig &d = dst.sticks[stick];
        Take(d.axisX, s.axisX);
        Take(d.axisY, s.axisY);
        Take(d.deadzone, s.deadzone);
        Take(d.deadzoneX, s.deadzoneX);
        Take(d.deadzoneY, s.deadzoneY);
        Take(d.comment, s.comment);
    }
}

void SeedStaging() {
    if (g_stagingSeeded)
        return;
    g_stagingSeeded = true;
    CopyLayer(kAddons, kStaging);
}

void ResetStagingSeed() { g_stagingSeeded = false; }

// --- resolved config ----------------------------------------------------------

Resolved::Resolved() {
    for (int i = 0; i < Names::kAxisCount; ++i) {
        rawAxisMap[i] = static_cast<uint8_t>(i);
        scale[i] = 1.0f;
        rescale[i] = 1.0f;
    }
    static constexpr uint8_t kX[Names::kStickCount] = {0, 2, 4, 6, 0, 2, 4, 2};
    static constexpr uint8_t kY[Names::kStickCount] = {1, 3, 5, 7, 1, 3, 5, 3};
    // (X, Y, radial) per stick, 0x141266C70.
    static constexpr float kDz[Names::kStickCount][3] = {
        {0, 0, 0.25f},       {0, 0, 0.25f},        {0, 0, 0}, {0, 0, 0},
        {0.05f, 0.05f, 0.25f}, {0.05f, 0.2f, 0.25f}, {0, 0, 0}, {0, 0, 0},
    };
    for (int s = 0; s < Names::kStickCount; ++s) {
        stickX[s] = kX[s];
        stickY[s] = kY[s];
        stickDeadzoneX[s] = kDz[s][0];
        stickDeadzoneY[s] = kDz[s][1];
        stickDeadzone[s] = kDz[s][2];
    }
}

namespace {

// 0x141266F70: one source config over the resolved one.
void MergeResolved(Resolved &cfg, const Record &src, bool vendorConfig) {
    if (vendorConfig && src.name.set)
        cfg.name = src.name.value;
    if (src.labelStyle.set)
        cfg.labelStyle = src.labelStyle.value;
    for (const auto &[raw, m] : src.rawButtons) {
        if (raw >= cfg.rawButtonCount || raw >= Names::kButtonCount)
            continue;
        if (m.button.set)
            cfg.rawButtonMap[raw] = m.button.value < Names::kButtonCount ? 1u << m.button.value : 0u;
        if (m.axis.set) {
            auto it = std::find_if(cfg.buttonAxis.begin(), cfg.buttonAxis.end(),
                                   [&](const ButtonAxis &e) { return e.rawButton == raw; });
            if (it == cfg.buttonAxis.end())
                it = cfg.buttonAxis.insert(cfg.buttonAxis.end(), ButtonAxis{raw, 0, 0.0f});
            it->axis = m.axis.value;
            it->value = m.axisValue;
        }
    }
    for (const auto &[raw, m] : src.rawAxes)
        if (raw < cfg.rawAxisCount && raw < Names::kAxisCount && m.axis.set)
            cfg.rawAxisMap[raw] = m.axis.value;
    for (const auto &[axis, a] : src.axes) {
        if (axis >= Names::kAxisCount)
            continue;
        if (a.shift.set)
            cfg.shift[axis] = a.shift.value;
        if (a.scale.set)
            cfg.scale[axis] = a.scale.value;
        if (a.deadzone.set) {
            cfg.deadzone[axis] = Clamp(a.deadzone.value, 0.0f, 0.99f);
            cfg.rescale[axis] = 1.0f / (1.0f - cfg.deadzone[axis]);
        }
        if (a.hasButtons) {
            auto it = std::find_if(cfg.axisButtons.begin(), cfg.axisButtons.end(),
                                   [&](const AxisButton &e) { return e.axis == axis; });
            if (it == cfg.axisButtons.end())
                it = cfg.axisButtons.insert(cfg.axisButtons.end(), AxisButton{axis, 0, 0, 0.0f});
            it->pos = a.buttonPos;
            it->neg = a.buttonNeg;
            it->threshold = a.buttonThreshold;
        }
    }
    for (const auto &[stick, s] : src.sticks) {
        if (stick >= Names::kStickCount)
            continue;
        if (s.axisX.set)
            cfg.stickX[stick] = s.axisX.value;
        if (s.axisY.set)
            cfg.stickY[stick] = s.axisY.value;
        if (s.deadzoneX.set)
            cfg.stickDeadzoneX[stick] = Clamp(s.deadzoneX.value, 0.0f, 0.99f);
        if (s.deadzoneY.set)
            cfg.stickDeadzoneY[stick] = Clamp(s.deadzoneY.value, 0.0f, 0.99f);
        if (s.deadzone.set)
            cfg.stickDeadzone[stick] = Clamp(s.deadzone.value, 0.0f, 0.99f);
    }
}

void MergeLayers(Resolved &cfg, uint32_t vendor, uint32_t product) {
    for (int layer = kDefault; layer <= kAddons; ++layer) {
        const Layer &l = g_layers[layer];
        auto merge = [&](Key key) {
            auto it = l.find(key);
            if (it != l.end())
                MergeResolved(cfg, it->second, key.first != 0);
        };
        merge({0, 0});
        if (vendor != 0) {
            merge({vendor, 0});
            if (product != 0)
                merge({vendor, product});
        }
    }
}

// A game controller's raw buttons (SDL_GamepadButton 0..20, then the touchpad
// sides) to buttons, 0x143F82E28.
constexpr uint8_t kGamepadButtons[23] = {
    Names::PAD1,         Names::PAD2,         Names::PAD3,       Names::PAD4,
    Names::PADBACK,      Names::PADSYSTEM,    Names::PADFORWARD, Names::PADLSTICK,
    Names::PADRSTICK,    Names::PADLSHOULDER, Names::PADRSHOULDER, Names::PADDUP,
    Names::PADDDOWN,     Names::PADDLEFT,     Names::PADDRIGHT,  Names::PAD5,
    Names::PADPADDLE1,   Names::PADPADDLE2,   Names::PADPADDLE3, Names::PADPADDLE4,
    Names::kNone,        Names::PADBACK,      Names::PAD6,
};
// Raw axes to axes, 0x143F82E40.
constexpr uint8_t kGamepadAxes[Names::kAxisCount] = {
    Names::LStickX, Names::LStickY, Names::RStickX,  Names::RStickY,  Names::LTrigger,
    Names::RTrigger, Names::GStickX, Names::GStickY, Names::PStickX, Names::PStickY,
};
// Per mapped axis, 0x143F82E78 / 0x143F82E50.
constexpr float kGamepadShift[Names::kAxisCount] = {0, 0, 0, 0, 0, 0, -0.5f, -0.5f, 0, 0};
constexpr float kGamepadScale[Names::kAxisCount] = {1, -1, 1, -1, 1, 1, 2, -2, 1, 1};

// 0x141274B40: the device's layer-0 default.
void BuildDeviceDefault(const DeviceDesc &d) {
    Record rec;
    char comment[512];
    const bool fakeSwitch = d.vendor == 0x57E && d.product == 3;
    std::snprintf(comment, sizeof comment, "Default Configuration for a %i:%i '%s'%s",
                  static_cast<int>(d.vendor), static_cast<int>(d.product), d.name,
                  fakeSwitch ? " (3rd party controller; VendorID and ProductID not actual values as "
                               "reported by hardware)"
                             : "");
    rec.comment.Set(comment);
    uint8_t style = d.vendor == 0x45E ? Names::Letters
                    : d.vendor == 0x54C ? Names::Shapes
                    : d.vendor == 0x57E ? Names::Reverse
                                        : Names::Generic;
    if (!d.gamepad) {
        for (uint32_t i = 0; i < d.buttonCount; ++i)
            rec.rawButtons[i].button.Set(static_cast<uint8_t>(i));
        if (d.hats && d.buttonCount >= 4) {
            const uint32_t n = d.buttonCount;
            rec.rawButtons[n - 4].button.Set(Names::PADDUP);
            rec.rawButtons[n - 3].button.Set(Names::PADDDOWN);
            rec.rawButtons[n - 2].button.Set(Names::PADDLEFT);
            rec.rawButtons[n - 1].button.Set(Names::PADDRIGHT);
        }
        for (uint32_t i = 0; i < d.axisCount; ++i)
            rec.rawAxes[i].axis.Set(static_cast<uint8_t>(i % Names::kAxisCount));
        for (uint8_t a = 0; a < Names::kAxisCount; ++a) {
            rec.axes[a].shift.Set(0.0f);
            rec.axes[a].scale.Set(1.0f);
        }
    } else {
        for (uint32_t i = 0; i < d.buttonCount && i < 23; ++i)
            rec.rawButtons[i].button.Set(kGamepadButtons[i]);
        for (uint32_t i = 0; i < d.axisCount && i < Names::kAxisCount; ++i)
            rec.rawAxes[i].axis.Set(kGamepadAxes[i]);
        for (uint8_t a = 0; a < Names::kAxisCount; ++a) {
            rec.axes[a].shift.Set(kGamepadShift[a]);
            rec.axes[a].scale.Set(kGamepadScale[a]);
        }
        if (d.touchpad) {
            rec.rawButtons[20].comment.Set("TouchPad hardware button");
            rec.rawButtons[21].comment.Set(
                "TouchPad left side (Also if hardware button clicked, but no touch detected)");
            rec.rawButtons[22].comment.Set("TouchPad right side");
        }
        switch (d.type) {
        case PadType::XboxOne:
            rec.rawButtons[15].button.Set(Names::PADSOCIAL);
            style = Names::Letters;
            break;
        case PadType::Xbox360:
            style = Names::Letters;
            break;
        case PadType::PS4:
        case PadType::PS5:
            rec.rawButtons[4].button.Set(Names::PADSOCIAL);
            style = Names::Shapes;
            break;
        case PadType::PS3:
            style = Names::Shapes;
            break;
        case PadType::SwitchPro:
            rec.rawButtons[15].button.Set(Names::PADSOCIAL);
            style = Names::Reverse;
            break;
        case PadType::Other:
            break;
        }
    }
    rec.labelStyle.Set(style);
    g_layers[kDefault][{d.vendor, d.product}] = std::move(rec);
}

} // namespace

void BuildGeneric(bool stickAxisButtons) {
    Record rec;
    rec.name.Set("All Devices");
    // 0x143B83AC0: the axis deadzones and the trigger buttons.
    for (uint8_t a : {Names::LStickX, Names::LStickY, Names::RStickX, Names::RStickY})
        rec.axes[a].deadzone.Set(0.05f);
    for (uint8_t a : {Names::GStickX, Names::GStickY})
        rec.axes[a].deadzone.Set(0.01f);
    auto buttons = [&](uint8_t axis, uint8_t pos, uint8_t neg) {
        AxisConfig &c = rec.axes[axis];
        c.hasButtons = true;
        c.buttonThreshold = 0.5f;
        c.buttonPos = pos;
        c.buttonNeg = neg;
    };
    rec.axes[Names::LTrigger].deadzone.Set(0.12f);
    buttons(Names::LTrigger, Names::PADLTRIGGER, Names::kNone);
    rec.axes[Names::RTrigger].deadzone.Set(0.12f);
    buttons(Names::RTrigger, Names::PADRTRIGGER, Names::kNone);
    if (stickAxisButtons) {
        buttons(Names::LStickX, Names::PADLSTICKRIGHT, Names::PADLSTICKLEFT);
        buttons(Names::LStickY, Names::PADLSTICKUP, Names::PADLSTICKDOWN);
        buttons(Names::RStickX, Names::PADRSTICKRIGHT, Names::PADRSTICKLEFT);
        buttons(Names::RStickY, Names::PADRSTICKUP, Names::PADRSTICKDOWN);
    }
    const Resolved defaults;
    for (uint8_t s = 0; s < Names::kStickCount; ++s) {
        StickConfig &c = rec.sticks[s];
        c.axisX.Set(defaults.stickX[s]);
        c.axisY.Set(defaults.stickY[s]);
        if (defaults.stickDeadzoneX[s] > 0.0f)
            c.deadzoneX.Set(defaults.stickDeadzoneX[s]);
        if (defaults.stickDeadzoneY[s] > 0.0f)
            c.deadzoneY.Set(defaults.stickDeadzoneY[s]);
        if (defaults.stickDeadzone[s] > 0.0f)
            c.deadzone.Set(defaults.stickDeadzone[s]);
    }
    g_layers[kDefault][{0, 0}] = std::move(rec);
}

void ResolveDevice(const DeviceDesc &device, Resolved &out) {
    out = Resolved();
    out.rawButtonCount = device.buttonCount;
    out.rawAxisCount = device.axisCount;
    BuildDeviceDefault(device);
    MergeLayers(out, device.vendor, device.product);
}

void ResolveCombined(Resolved &out) {
    out = Resolved();
    MergeLayers(out, 0, 0);
    out.rawButtonCount = 0;
    std::memset(out.rawButtonMap, 0, sizeof out.rawButtonMap);
    out.buttonAxis.clear();
    out.rawAxisCount = 0;
    std::memset(out.rawAxisMap, 0, sizeof out.rawAxisMap);
    std::memset(out.shift, 0, sizeof out.shift);
    std::memset(out.scale, 0, sizeof out.scale);
    std::memset(out.deadzone, 0, sizeof out.deadzone);
    std::memset(out.rescale, 0, sizeof out.rescale);
    out.axisButtons.clear();
}

// --- JSON ----------------------------------------------------------------------

namespace {

using Value = rapidjson::Value;

const char *Str(const Value &obj, const char *key) {
    auto it = obj.FindMember(key);
    return it != obj.MemberEnd() && it->value.IsString() ? it->value.GetString() : nullptr;
}

bool Num(const Value &obj, const char *key, double &out) {
    auto it = obj.FindMember(key);
    if (it == obj.MemberEnd() || !it->value.IsNumber())
        return false;
    out = it->value.GetDouble();
    return true;
}

void ReadFloat(const Value &obj, const char *key, Opt<float> &out) {
    double v;
    if (Num(obj, key, v))
        out.Set(static_cast<float>(v));
}

void ReadComment(const Value &obj, Opt<std::string> &out) {
    if (const char *s = Str(obj, "comment"))
        out.Set(s);
}

// A button name from a file: the two name tables, or "None" (what the writer
// prints for a raw button mapped to nothing).
uint8_t FileButton(const char *name, bool &ok) {
    ok = name != nullptr;
    if (!ok)
        return Names::kNone;
    if (_stricmp(name, "None") == 0)
        return Names::kNone;
    const uint8_t b = Names::ParseButton(name);
    ok = b != Names::kNone;
    return b;
}

template <typename Fn> void ForEach(const Value &obj, const char *key, Fn fn) {
    auto it = obj.FindMember(key);
    if (it == obj.MemberEnd() || !it->value.IsArray())
        return;
    for (const Value &e : it->value.GetArray())
        if (e.IsObject())
            fn(e);
}

// One config object into a record, with the rules C_GamePad.SetConfig's store
// applies: unknown names are skipped, an axis needs its axisValue, a button
// group needs buttonPos or buttonNeg.
Key ReadRecord(const Value &obj, Record &rec) {
    double v;
    Key key{0, 0};
    if (Num(obj, "vendorID", v) && v >= 0)
        key.first = static_cast<uint32_t>(v);
    if (Num(obj, "productID", v) && v >= 0)
        key.second = static_cast<uint32_t>(v);
    ReadComment(obj, rec.comment);
    if (const char *s = Str(obj, "name"))
        rec.name.Set(std::string(s).substr(0, 255));
    if (const char *s = Str(obj, "labelStyle")) {
        const uint8_t style = Names::ParseLabelStyle(s);
        if (style != Names::kNone)
            rec.labelStyle.Set(style);
    }
    ForEach(obj, "rawButtonMappings", [&](const Value &e) {
        if (!Num(e, "rawIndex", v) || v < 0)
            return;
        RawButtonMapping &m = rec.rawButtons[static_cast<uint32_t>(v)];
        bool ok;
        const uint8_t b = FileButton(Str(e, "button"), ok);
        if (ok)
            m.button.Set(b);
        double value;
        const uint8_t axis = Names::ParseAxis(Str(e, "axis"));
        if (axis != Names::kNone && Num(e, "axisValue", value)) {
            m.axis.Set(axis);
            m.axisValue = static_cast<float>(value);
        }
        ReadComment(e, m.comment);
    });
    ForEach(obj, "rawAxisMappings", [&](const Value &e) {
        if (!Num(e, "rawIndex", v) || v < 0)
            return;
        RawAxisMapping &m = rec.rawAxes[static_cast<uint32_t>(v)];
        const uint8_t axis = Names::ParseAxis(Str(e, "axis"));
        if (axis != Names::kNone)
            m.axis.Set(axis);
        ReadComment(e, m.comment);
    });
    ForEach(obj, "axisConfigs", [&](const Value &e) {
        const uint8_t axis = Names::ParseAxis(Str(e, "axis"));
        if (axis == Names::kNone)
            return;
        AxisConfig &c = rec.axes[axis];
        ReadFloat(e, "shift", c.shift);
        ReadFloat(e, "scale", c.scale);
        ReadFloat(e, "deadzone", c.deadzone);
        double threshold;
        const char *pos = Str(e, "buttonPos");
        const char *neg = Str(e, "buttonNeg");
        if (Num(e, "buttonThreshold", threshold) && (pos != nullptr || neg != nullptr)) {
            bool ok;
            c.hasButtons = true;
            c.buttonThreshold = static_cast<float>(threshold);
            c.buttonPos = FileButton(pos, ok);
            c.buttonNeg = FileButton(neg, ok);
        }
        ReadComment(e, c.comment);
    });
    ForEach(obj, "stickConfigs", [&](const Value &e) {
        const uint8_t stick = Names::ParseStick(Str(e, "stick"));
        if (stick == Names::kNone)
            return;
        StickConfig &c = rec.sticks[stick];
        const uint8_t x = Names::ParseAxis(Str(e, "axisX"));
        if (x != Names::kNone)
            c.axisX.Set(x);
        const uint8_t y = Names::ParseAxis(Str(e, "axisY"));
        if (y != Names::kNone)
            c.axisY.Set(y);
        ReadFloat(e, "deadzone", c.deadzone);
        ReadFloat(e, "deadzoneX", c.deadzoneX);
        ReadFloat(e, "deadzoneY", c.deadzoneY);
        ReadComment(e, c.comment);
    });
    return key;
}

bool ReadFile(const char *path, std::string &out) {
    FILE *f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || f == nullptr)
        return false;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        out.append(buf, n);
    std::fclose(f);
    return true;
}

// Merges every config of one file into `layer`. False when the file is not
// readable JSON with a configs array.
bool LoadInto(const char *path, int layer) {
    std::string text;
    if (!ReadFile(path, text))
        return false;
    rapidjson::Document doc;
    doc.Parse(text.c_str());
    if (doc.HasParseError() || !doc.IsObject())
        return false;
    auto configs = doc.FindMember("configs");
    if (configs == doc.MemberEnd() || !configs->value.IsArray())
        return false;
    for (const Value &obj : configs->value.GetArray()) {
        if (!obj.IsObject())
            continue;
        Record rec;
        const Key key = ReadRecord(obj, rec);
        MergeRecord(g_layers[layer][key], rec);
    }
    return true;
}

using Writer = rapidjson::PrettyWriter<rapidjson::StringBuffer>;

// 3.80.2 writes each float as v + 0.005 (away from zero) cut to 2 decimals.
void WriteFloat(Writer &w, const char *key, float v) {
    w.Key(key);
    w.Double(static_cast<double>(v) + (v >= 0.0f ? 0.005 : -0.005));
}

void WriteString(Writer &w, const char *key, const std::string &s) {
    w.Key(key);
    w.String(s.c_str(), static_cast<rapidjson::SizeType>(s.size()));
}

void WriteButton(Writer &w, const char *key, uint8_t button) {
    w.Key(key);
    w.String(button < Names::kButtonCount ? Names::kButtonBinding[button] : "None");
}

void WriteAxis(Writer &w, const char *key, uint8_t axis) {
    w.Key(key);
    w.String(axis < Names::kAxisCount ? Names::kAxis[axis] : "None");
}

// The serializer 0x14126B4E0: fields in its order, each only when set.
void WriteRecord(Writer &w, const Key &key, const Record &rec) {
    w.StartObject();
    WriteString(w, "separator", std::string(80, '='));
    if (rec.comment.set)
        WriteString(w, "comment", rec.comment.value);
    if (rec.name.set)
        WriteString(w, "name", rec.name.value);
    if (rec.labelStyle.set && rec.labelStyle.value < Names::kLabelStyleCount)
        WriteString(w, "labelStyle", Names::kLabelStyle[rec.labelStyle.value]);
    if (key.first != 0) {
        w.Key("vendorID");
        w.Uint(key.first);
    }
    if (key.second != 0) {
        w.Key("productID");
        w.Uint(key.second);
    }
    if (!rec.rawButtons.empty()) {
        w.Key("rawButtonMappings");
        w.StartArray();
        for (const auto &[raw, m] : rec.rawButtons) {
            w.StartObject();
            w.Key("rawIndex");
            w.Uint(raw);
            if (m.button.set)
                WriteButton(w, "button", m.button.value);
            if (m.axis.set) {
                WriteAxis(w, "axis", m.axis.value);
                WriteFloat(w, "axisValue", m.axisValue);
            }
            if (m.comment.set)
                WriteString(w, "comment", m.comment.value);
            w.EndObject();
        }
        w.EndArray();
    }
    if (!rec.rawAxes.empty()) {
        w.Key("rawAxisMappings");
        w.StartArray();
        for (const auto &[raw, m] : rec.rawAxes) {
            w.StartObject();
            w.Key("rawIndex");
            w.Uint(raw);
            if (m.axis.set)
                WriteAxis(w, "axis", m.axis.value);
            if (m.comment.set)
                WriteString(w, "comment", m.comment.value);
            w.EndObject();
        }
        w.EndArray();
    }
    if (!rec.axes.empty()) {
        w.Key("axisConfigs");
        w.StartArray();
        for (const auto &[axis, a] : rec.axes) {
            w.StartObject();
            WriteAxis(w, "axis", axis);
            if (a.deadzone.set)
                WriteFloat(w, "deadzone", a.deadzone.value);
            if (a.shift.set)
                WriteFloat(w, "shift", a.shift.value);
            if (a.scale.set)
                WriteFloat(w, "scale", a.scale.value);
            if (a.hasButtons) {
                WriteFloat(w, "buttonThreshold", a.buttonThreshold);
                if (a.buttonPos < Names::kButtonCount)
                    WriteButton(w, "buttonPos", a.buttonPos);
                if (a.buttonNeg < Names::kButtonCount)
                    WriteButton(w, "buttonNeg", a.buttonNeg);
            }
            if (a.comment.set)
                WriteString(w, "comment", a.comment.value);
            w.EndObject();
        }
        w.EndArray();
    }
    if (!rec.sticks.empty()) {
        w.Key("stickConfigs");
        w.StartArray();
        for (const auto &[stick, s] : rec.sticks) {
            w.StartObject();
            w.Key("stick");
            w.String(Names::kStick[stick]);
            if (s.axisX.set)
                WriteAxis(w, "axisX", s.axisX.value);
            if (s.axisY.set)
                WriteAxis(w, "axisY", s.axisY.value);
            if (s.deadzoneX.set)
                WriteFloat(w, "deadzoneX", s.deadzoneX.value);
            if (s.deadzoneY.set)
                WriteFloat(w, "deadzoneY", s.deadzoneY.value);
            if (s.deadzone.set)
                WriteFloat(w, "deadzone", s.deadzone.value);
            if (s.comment.set)
                WriteString(w, "comment", s.comment.value);
            w.EndObject();
        }
        w.EndArray();
    }
    w.EndObject();
}

// The Default file's header (0x144C746A0), verbatim.
void WriteHeader(Writer &w) {
    static const char *const kLines[] = {
        "This is the default GamePad config generated by the game. Do not modify, it is for reference "
        "only and will get overwritten.",
        "",
        "You can create your own GamePadConfig_*.json config files, such as GamePadConfig_MyConfigs.json",
        "Multiple config files will be loaded in alphabetical order. (Not counting the Default and AddOns "
        "files, which come first/last respectively)",
        "",
        "Indexes for raw button and axis inputs are device dependent.",
        "Each device has a specific 'vendorID' and 'productID' associated with it.",
        "To make a device specific config, set both 'vendorID' and 'productID'.",
        "To make a config for all devices from a vendor (eg. all XBox controllers) only set 'vendorID'.",
        "To make a global config that applies to all devices, do not set either ID.",
        "",
        "Raw Stick Axis values should get mapped into +/-1 range.",
        "Raw Trigger Axis values should get mapped into 0-1 range.",
        "",
        "Config Layout: [] around field type means it's optional",
        "{",
        "    [string] comment",
        "    [string] name",
        "    [string] labelStyle",
        "    [array] rawButtonMappings {",
        "        [string] comment",
        "        int      rawIndex",
        "        [string] button",
        "        [string] axis (must be set along with axisValue)",
        "        [float]  axisValue",
        "    }",
        "    [array] rawAxisMappings {",
        "        [string] comment",
        "        int      rawIndex",
        "        string   axis",
        "    }",
        "    [array] axisConfigs {",
        "        [string] comment",
        "        string   axis",
        "        [float]  shift (Value shift when mapping from a raw axis)",
        "        [float]  scale (Value scale when mapping from a raw axis)",
        "        [float]  deadzone (deadzone applied when mapping from a raw axis)",
        "        [float]  buttonThreshold (Must be set if setting buttonPos or buttonNeg)",
        "        [string] buttonPos",
        "        [string] buttonNeg",
        "    }",
        "    [array] stickConfigs {",
        "        [string] comment",
        "        string   stick",
        "        [string] axisX",
        "        [string] axisY",
        "        [float]  deadzone (2D deadzone applied when normalizing the stick input length)",
        "        [float]  deadzoneX (X axis deadzone applied when mapping to stick)",
        "        [float]  deadzoneY (Y axis deadzone applied when mapping to stick)",
        "    }",
        "",
        "Valid values for each type of enumerated field:",
    };
    static const char kRule[] = "----------------------------------------";
    w.Key("headerComments");
    w.StartArray();
    for (const char *line : kLines)
        w.String(line);
    w.String(kRule);
    w.String("'labelStyle' values:");
    for (const char *s : Names::kLabelStyle)
        w.String(s);
    w.String(kRule);
    w.String("'axis' values: (Also 'axisX' and 'axisY' in stickConfigs)");
    for (const char *s : Names::kAxis)
        w.String(s);
    w.String(kRule);
    w.String("'button' values: (Also 'buttonPos' and 'buttonNeg' in axisConfigs)");
    for (int i = 0; i < Names::kButtonCount; ++i) {
        char line[64];
        std::snprintf(line, sizeof line, "%-16s (or %s)", Names::kButtonBinding[i], Names::kButtonConfig[i]);
        w.String(line);
    }
    w.String(kRule);
    w.String("'stick' values:");
    for (const char *s : Names::kStick)
        w.String(s);
    w.EndArray();
}

void WriteLayer(const char *path, int layer, bool header) {
    rapidjson::StringBuffer buf;
    Writer w(buf);
    w.SetIndent(' ', 4);
    w.SetMaxDecimalPlaces(2);
    w.StartObject();
    if (header)
        WriteHeader(w);
    w.Key("configVersion");
    w.Int(kConfigVersion);
    w.Key("configs");
    w.StartArray();
    for (const auto &[key, rec] : g_layers[layer])
        WriteRecord(w, key, rec);
    w.EndArray();
    w.EndObject();
    FILE *f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || f == nullptr)
        return;
    std::fwrite(buf.GetString(), 1, buf.GetSize(), f);
    std::fclose(f);
}

bool WtfExists() {
    const DWORD attr = GetFileAttributesA("WTF");
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

} // namespace

void WriteDefaultFile() {
    if (WtfExists())
        WriteLayer(kDefaultFile, kDefault, true);
}

bool LoadFiles() {
    std::vector<std::string> names;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("WTF\\GamePadConfig*", &fd);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            continue;
        const char *dot = std::strrchr(fd.cFileName, '.');
        if (dot == nullptr || _stricmp(dot, ".json") != 0)
            continue;
        if (_stricmp(fd.cFileName, "GamePadConfig_Default.json") == 0 ||
            _stricmp(fd.cFileName, "GamePadConfig_Addons.json") == 0)
            continue;
        names.emplace_back(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end(),
              [](const std::string &a, const std::string &b) { return _stricmp(a.c_str(), b.c_str()) < 0; });
    bool loaded = false;
    for (const std::string &name : names)
        loaded |= LoadInto(("WTF\\" + name).c_str(), kFiles);
    if (!names.empty())
        Game::Console::Write("GamePad configs loaded.");
    return loaded;
}

bool LoadAddons() { return LoadInto(kAddonsFile, kAddons); }

void SaveAddons() {
    if (WtfExists())
        WriteLayer(kAddonsFile, kAddons, false);
}

} // namespace GamePad::Config
