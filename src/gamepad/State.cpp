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

#include "gamepad/State.h"

#include <cmath>

namespace GamePad::State {

namespace {

Config::Resolved g_config[Device::kSlotCount];
Mapped g_mapped[Device::kSlotCount];
int g_combinedCount = 0; // devices the combined config was built for (0x1468BFF90)

constexpr float kEps = 4.7683716e-07f;
constexpr float kLenEps = 2.3841858e-07f;

float Sign(float v) { return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); }

float Curve(const Config::Resolved &cfg, uint8_t d, float r) {
    const float v = (r + cfg.shift[d]) * cfg.scale[d];
    const float z = cfg.deadzone[d];
    const float k = cfg.rescale[d];
    if (v > z) {
        const float o = (v - z) * k;
        return o < 0.0f ? 0.0f : (o > 1.0f ? 1.0f : o);
    }
    if (v < -z) {
        const float o = (v + z) * k;
        return o < -1.0f ? -1.0f : (o > 0.0f ? 0.0f : o);
    }
    return 0.0f;
}

float Length(float x, float y) {
    const float l2 = x * x + y * y;
    return l2 > kLenEps ? std::sqrt(l2) : 0.0f;
}

// 0x1412590A0. The radial pass scales by k, not k / l (so the output length
// is l * k), and len is never clamped.
void Stick(Mapped &m, const Config::Resolved &cfg, int s) {
    m.sticks[s] = StickValue{};
    const uint8_t ax = cfg.stickX[s], ay = cfg.stickY[s];
    if (ax >= Names::kAxisCount || ay >= Names::kAxisCount)
        return;
    float x = m.axes[ax], y = m.axes[ay];
    float dzx = cfg.stickDeadzoneX[s], dzy = cfg.stickDeadzoneY[s];
    const float dz = cfg.stickDeadzone[s];
    if (dz > kEps) {
        const float m2 = x * x + y * y;
        if (m2 <= dz * dz + kEps)
            return;
        const float l = std::sqrt(m2);
        if (dzx * dzx + dzy * dzy > kEps) {
            dzy = std::fmax(dzy - std::fabs(y / l * dz), 0.0f);
            dzx = std::fmax(dzx - std::fabs(x / l * dz), 0.0f);
        }
        float k = (l - dz) / (1.0f - dz);
        k = k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k);
        x *= k;
        y *= k;
    }
    if (dzx * dzx + dzy * dzy > kEps) {
        const float sx = Sign(x), sy = Sign(y);
        x = std::fmax(x * sx - dzx, 0.0f) / (1.0f - dzx) * sx;
        y = std::fmax(y * sy - dzy, 0.0f) / (1.0f - dzy) * sy;
    }
    m.sticks[s].x = x;
    m.sticks[s].y = y;
    m.sticks[s].len = Length(x, y);
}

// 0x141259A90 for one physical slot.
void Map(const Device::Raw &raw, const Config::Resolved &cfg, Mapped &m) {
    for (const Config::ButtonAxis &e : cfg.buttonAxis)
        if (e.axis < Names::kAxisCount)
            m.axesMin[e.axis] = m.axesMax[e.axis] = m.axes[e.axis];
    for (uint32_t i = 0; i < cfg.rawAxisCount && i < Names::kAxisCount; ++i) {
        const uint8_t d = cfg.rawAxisMap[i];
        if (d >= Names::kAxisCount)
            continue;
        m.axes[d] = Curve(cfg, d, raw.cur[i]);
        m.axesMin[d] = Curve(cfg, d, raw.min[i]);
        m.axesMax[d] = Curve(cfg, d, raw.max[i]);
    }
    m.prevButtons = m.buttons;
    m.buttons = m.pressed = m.released = 0;
    for (uint32_t j = 0; j < cfg.rawButtonCount && j < Names::kButtonCount; ++j) {
        const uint32_t bit = 1u << j;
        if (raw.buttons & bit)
            m.buttons |= cfg.rawButtonMap[j];
        if (raw.pressed & bit)
            m.pressed |= cfg.rawButtonMap[j];
        if (raw.released & bit)
            m.released |= cfg.rawButtonMap[j];
    }
    m.held = m.buttons & ~(m.pressed | m.released);
    for (const Config::ButtonAxis &e : cfg.buttonAxis) {
        if (e.axis >= Names::kAxisCount)
            continue;
        const uint32_t bit = 1u << (e.rawButton & 31);
        const bool down = (raw.buttons & bit) != 0;
        if (down || (raw.released & bit) != 0) {
            m.axes[e.axis] = (down ? 1.0f : 0.0f) * e.value;
            m.axesMin[e.axis] = std::fmin(m.axesMin[e.axis], e.value);
            m.axesMax[e.axis] = std::fmax(m.axesMax[e.axis], e.value);
        }
    }
    for (const Config::AxisButton &e : cfg.axisButtons) {
        if (e.axis >= Names::kAxisCount)
            continue;
        const uint8_t a = e.axis;
        auto edge = [&](uint8_t button, float sign) {
            if (button >= Names::kButtonCount)
                return;
            const uint32_t b = 1u << button;
            auto on = [&](float v) { return e.threshold <= v * sign; };
            const bool was = (m.prevButtons & b) != 0;
            const bool cur = on(m.axes[a]);
            const bool cross = on(m.axesMax[a]) != on(m.axesMin[a]);
            if (cur)
                m.buttons |= b;
            if (cross && (!was || cur))
                m.pressed |= b;
            if (cross && (was || !cur))
                m.released |= b;
            if (!cross && cur)
                m.held |= b;
        };
        edge(e.pos, 1.0f);
        edge(e.neg, -1.0f);
    }
    for (int s = 0; s < Names::kStickCount; ++s)
        Stick(m, cfg, s);
}

// 0x14125A000.
void Combine() {
    Device::Slot &slot5 = *Device::SlotByID(Device::kCombinedID);
    Mapped &c = g_mapped[Device::kCombinedID - 1];
    Mapped out;
    int n = 0;
    uint8_t power = Device::kPowerUnknown;
    const Device::Slot *only = nullptr;
    bool gyro = false, touch = false, rumble = false, triggerRumble = false, led = false;
    uint8_t firstStyle = Names::Generic;
    for (uint32_t id = 1; id <= 4; ++id) {
        const Device::Slot &s = *Device::SlotByID(id);
        if (!s.connected)
            continue;
        const Mapped &m = g_mapped[id - 1];
        out.buttons |= m.buttons;
        out.prevButtons |= m.prevButtons;
        out.pressed |= m.pressed;
        out.released |= m.released;
        if (s.power < power)
            power = s.power;
        gyro |= s.hasGyro;
        touch |= s.hasTouchpad;
        rumble |= s.hasRumble;
        triggerRumble |= s.hasTriggerRumble;
        led |= s.hasLED;
        if (n == 0) {
            for (int a = 0; a < Names::kAxisCount; ++a) {
                out.axes[a] = m.axes[a];
                out.axesMin[a] = m.axesMin[a];
                out.axesMax[a] = m.axesMax[a];
            }
            for (int k = 0; k < Names::kStickCount; ++k)
                out.sticks[k] = m.sticks[k];
            only = &s;
            firstStyle = g_config[id - 1].labelStyle;
        } else {
            for (int a = 0; a < Names::kAxisCount; ++a) {
                out.axesMin[a] = std::fmin(out.axesMin[a], m.axesMin[a]);
                out.axesMax[a] = std::fmax(out.axesMax[a], m.axesMax[a]);
                if (std::fabs(m.axes[a]) > std::fabs(out.axes[a]))
                    out.axes[a] = m.axes[a];
            }
            for (int k = 0; k < Names::kStickCount; ++k) {
                if (std::fabs(m.sticks[k].x) > std::fabs(out.sticks[k].x))
                    out.sticks[k].x = m.sticks[k].x;
                if (std::fabs(m.sticks[k].y) > std::fabs(out.sticks[k].y))
                    out.sticks[k].y = m.sticks[k].y;
            }
        }
        ++n;
    }
    if (n == 0) {
        if (slot5.connected) {
            slot5.connected = false;
            ResetSlot(Device::kCombinedID);
        }
        g_combinedCount = 0;
        return;
    }
    if (!slot5.connected || n != g_combinedCount) {
        Config::Resolved &cfg = g_config[Device::kCombinedID - 1];
        Config::ResolveCombined(cfg);
        slot5.connected = true;
        if (n != 1) {
            slot5.name = "Combined GamePad";
            cfg.name.clear();
        } else {
            slot5.name = only->name;
            cfg.name = g_config[only->id - 1].name;
        }
        slot5.hasGyro = gyro;
        slot5.hasTouchpad = touch;
        slot5.hasRumble = rumble;
        slot5.hasTriggerRumble = triggerRumble;
        slot5.hasLED = led;
        if (cfg.labelStyle == Names::Generic)
            cfg.labelStyle = firstStyle;
        g_combinedCount = n;
    }
    slot5.power = power;
    out.held = c.held; // not written for the combined device
    // A press on one device and a release on another in the same frame.
    const uint32_t pressed = out.pressed;
    out.pressed |= out.released & out.buttons;
    out.released |= pressed & ~out.buttons;
    for (int k = 0; k < Names::kStickCount; ++k)
        out.sticks[k].len = Length(out.sticks[k].x, out.sticks[k].y);
    c = out;
}

} // namespace

Config::Resolved &ConfigOf(uint32_t id) { return g_config[id - 1]; }
Mapped &MappedOf(uint32_t id) { return g_mapped[id - 1]; }

void Resolve(uint32_t id) {
    const Device::Slot &s = *Device::SlotByID(id);
    if (id == Device::kCombinedID) {
        Config::ResolveCombined(g_config[id - 1]);
        // The combined device rebuilds its name and label style on its next
        // build (0x141259480 marks it disconnected for that).
        Device::SlotByID(id)->connected = false;
        g_combinedCount = 0;
        return;
    }
    if (s.opened)
        Config::ResolveDevice(s.desc, g_config[id - 1]);
    else
        g_config[id - 1] = Config::Resolved();
}

void RawReset(Device::Raw &raw, const Config::Resolved &cfg) {
    raw.lastInput = 0;
    raw.buttons = raw.pressed = raw.released = 0;
    for (int i = 0; i < Names::kAxisCount; ++i) {
        const uint8_t d = cfg.rawAxisMap[i];
        const float rest = d < Names::kAxisCount ? -cfg.shift[d] : 0.0f;
        raw.cur[i] = raw.min[i] = raw.max[i] = rest;
    }
}

void MapAll() {
    for (uint32_t id = 1; id <= 4; ++id)
        if (Device::SlotByID(id)->connected)
            Map(Device::RawOf(static_cast<int>(id) - 1), g_config[id - 1], g_mapped[id - 1]);
    Combine();
}

void ResetSlot(uint32_t id) {
    Device::Slot &s = *Device::SlotByID(id);
    s = Device::Slot();
    s.id = id;
    g_config[id - 1] = Config::Resolved();
    g_mapped[id - 1] = Mapped();
    if (id <= 4)
        Device::RawOf(static_cast<int>(id) - 1) = Device::Raw();
}

void InvalidateCombined() { g_combinedCount = 0; }

} // namespace GamePad::State
