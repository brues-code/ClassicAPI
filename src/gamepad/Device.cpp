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

#include "gamepad/Device.h"

#include "Offsets.h"

#include <SDL3/SDL.h>

#include <GamepadMotion.hpp>

#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <cctype>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace GamePad::Device {

namespace {

// 3.80.2's raw axis scale, 1/32767 (0x14127xxxx: value * 3.051851e-05).
constexpr float kAxisScale = 3.051851e-05f;
constexpr uint64_t kSecondNs = 1000000000ull;
constexpr uint32_t kTouchClick = 1u << 20; // SDL_GAMEPAD_BUTTON_TOUCHPAD
constexpr uint32_t kTouchLeft = 1u << 21;
constexpr uint32_t kTouchRight = 1u << 22;
constexpr int kNoFinger = 2;

// The touchpad state (0x40 bytes in 3.80.2): finger 0 and 1 only.
struct TouchState {
    float xy[2][2] = {};
    float anchorX = 0, anchorY = 0;
    int active = kNoFinger;
    uint32_t mask = 0;     // the touch-side buttons it holds down
    uint32_t startMs = 0;  // when the active finger touched, 0 when none
    int tapFinger = kNoFinger;
    bool tapCandidate = false;
    bool fingerDown[2] = {};
    bool moved = false;
};

// The motion state: GamepadMotionHelpers, as 3.80.2 links it, fed the
// previous gyro + accel pair whenever a sample with a new timestamp arrives.
struct SensorState {
    GamepadMotion motion;
    uint32_t lastTs = 0, prevTs = 0;
    float accel[3] = {};
    float gyro[3] = {};
    uint64_t calibUntil = 0; // continuous calibration ends then; 0 once fixed
};

// One SDL device (0x1444A7C58, stride 0x70).
struct Record {
    int slot = -1; // 0..3 while a slot holds it
    SDL_JoystickID instance = 0;
    SDL_Joystick *joy = nullptr;
    SDL_Gamepad *pad = nullptr;
    Config::PadType type = Config::PadType::Other;
    int hats = 0;
    uint32_t led = 0; // 0xRRGGBB
    uint16_t rumble[4] = {};
    uint64_t expiry[4] = {};
    std::unique_ptr<TouchState> touch;
    std::unique_ptr<SensorState> sensor;
};

std::vector<std::unique_ptr<Record>> g_records;
Record *g_slotRecord[4] = {};
Slot g_slots[kSlotCount];
Raw g_raw[4];
int g_pendingAdds = 0;
bool g_sdl = false;
TouchSettings g_touch;

// A case-insensitive substring test (3.80.2's stristr).
bool ContainsI(const char *hay, const char *needle) {
    if (hay == nullptr)
        return false;
    const size_t n = std::strlen(needle);
    for (; *hay != 0; ++hay)
        if (_strnicmp(hay, needle, n) == 0)
            return true;
    return false;
}

Record *ByInstance(SDL_JoystickID id) {
    for (auto &r : g_records)
        if (r->instance == id && id != 0)
            return r.get();
    return nullptr;
}

// The record an input event belongs to: assigned to a slot whose device is
// open, else null (3.80.2 drops it).
Record *Assigned(SDL_JoystickID id, int &slot) {
    Record *r = ByInstance(id);
    if (r == nullptr || r->slot < 0 || !g_slots[r->slot].opened)
        return nullptr;
    slot = r->slot;
    return r;
}

Config::PadType TypeOf(SDL_GamepadType t) {
    switch (t) {
    case SDL_GAMEPAD_TYPE_XBOX360: return Config::PadType::Xbox360;
    case SDL_GAMEPAD_TYPE_XBOXONE: return Config::PadType::XboxOne;
    case SDL_GAMEPAD_TYPE_PS3: return Config::PadType::PS3;
    case SDL_GAMEPAD_TYPE_PS4: return Config::PadType::PS4;
    case SDL_GAMEPAD_TYPE_PS5: return Config::PadType::PS5;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: return Config::PadType::SwitchPro;
    default: return Config::PadType::Other;
    }
}

// SDL2's power level from SDL3's power info, as sdl2-compat converts it
// (SDL_JoystickCurrentPowerLevel and its battery event): on battery above 70%
// full, above 20% medium, above 5% low, else empty; wired for a wired
// device; unknown otherwise. SDL2's levels EMPTY..WIRED are 3.80.2's 0..4
// (its table 0x143F82EA0).
uint8_t PowerOf(SDL_PowerState state, int percent, bool wired) {
    if (wired)
        return 4;
    if (state != SDL_POWERSTATE_ON_BATTERY)
        return kPowerUnknown;
    return percent > 70 ? 3 : percent > 20 ? 2 : percent > 5 ? 1 : 0;
}

uint8_t PowerOf(SDL_Joystick *joy) {
    const SDL_JoystickConnectionState c = SDL_GetJoystickConnectionState(joy);
    if (c == SDL_JOYSTICK_CONNECTION_WIRED)
        return 4;
    if (c != SDL_JOYSTICK_CONNECTION_WIRELESS)
        return kPowerUnknown;
    int percent = -1;
    return PowerOf(SDL_GetJoystickPowerInfo(joy, &percent), percent, false);
}

void Close(Record &r) {
    if (r.joy != nullptr) {
        SDL_RumbleJoystick(r.joy, 0, 0, 0);
        SDL_RumbleJoystickTriggers(r.joy, 0, 0, 0);
        SDL_SetJoystickLED(r.joy, 0, 0, 0);
    }
    if (r.pad != nullptr)
        SDL_CloseGamepad(r.pad);
    else if (r.joy != nullptr)
        SDL_CloseJoystick(r.joy);
    r.pad = nullptr;
    r.joy = nullptr;
    r.instance = 0;
    r.touch.reset();
    r.sensor.reset();
}

// SDL_EVENT_JOYSTICK_ADDED / SDL_EVENT_GAMEPAD_ADDED (0x141273CB0).
void Add(SDL_JoystickID id) {
    if (ByInstance(id) != nullptr)
        return;
    Sleep(100); // 3.80.2 lets a new device settle before opening it
    Record *rec = nullptr;
    for (auto &r : g_records)
        if (r->instance == 0) {
            rec = r.get();
            break;
        }
    if (rec == nullptr) {
        g_records.push_back(std::make_unique<Record>());
        rec = g_records.back().get();
    }
    if (!SDL_IsGamepad(id)) {
        SDL_Joystick *joy = SDL_OpenJoystick(id);
        if (joy == nullptr)
            return;
        const char *name = SDL_GetJoystickName(joy);
        // 3.80.2 skips raw "XBox One" joysticks (and leaves them open).
        if (ContainsI(name, "XBox One"))
            return;
        rec->joy = joy;
        rec->pad = nullptr;
        rec->type = Config::PadType::Other;
        rec->hats = SDL_GetNumJoystickHats(joy);
    } else {
        SDL_Gamepad *pad = SDL_OpenGamepad(id);
        if (pad == nullptr)
            return;
        rec->pad = pad;
        rec->joy = SDL_GetGamepadJoystick(pad);
        rec->type = TypeOf(SDL_GetRealGamepadType(pad));
        rec->hats = 0;
    }
    rec->instance = id;
    rec->slot = -1;
    rec->led = 0;
    std::memset(rec->rumble, 0, sizeof rec->rumble);
    std::memset(rec->expiry, 0, sizeof rec->expiry);
    rec->touch.reset();
    rec->sensor.reset();
    ++g_pendingAdds;
}

void Remove(SDL_JoystickID id);

// --- touchpad (0x141275570 / 0x1412759A0) --------------------------------------
//
// The touch-cursor half (the cursor delta and the click button it emulates)
// belongs to the gamepad cursor and is not here.

void TouchButtons(Record &r, Raw &raw) {
    TouchState *t = r.touch.get();
    if (t == nullptr)
        return;
    const bool click = (raw.buttons & kTouchClick) != 0;
    const int tf = t->tapFinger;
    const uint32_t prev = t->mask;
    if (!click && tf > 1 && prev == 0)
        return;
    t->tapCandidate = false;
    uint32_t now = 0;
    if ((click && t->startMs != 0) || tf <= 1) {
        const bool c0 = t->fingerDown[0] || tf == 0;
        const bool c1 = t->fingerDown[1] || tf == 1;
        const uint32_t b0 = t->xy[0][0] > 0.5f ? kTouchRight : kTouchLeft;
        const uint32_t b1 = t->xy[1][0] > 0.5f ? kTouchRight : kTouchLeft;
        now = (c0 ? b0 : 0) | (c1 ? b1 : 0);
    } else if (click) {
        now = kTouchLeft; // clicked with no touch: the left side
    }
    if (!(click || prev != 0 || (tf <= 1 && g_touch.tapButtons)))
        return;
    raw.buttons = (raw.buttons & ~prev) | now;
    raw.pressed |= ~prev & now;
    raw.released |= prev & ~now;
    t->mask = now;
}

void TouchEvent(Record &r, Raw &raw, const SDL_GamepadTouchpadEvent &ev, uint32_t ts) {
    TouchState *t = r.touch.get();
    if (t == nullptr || ev.touchpad != 0 || ev.finger < 0 || ev.finger > 1)
        return;
    const int f = ev.finger;
    if (ev.type == SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN || ev.type == SDL_EVENT_GAMEPAD_TOUCHPAD_UP) {
        t->fingerDown[f] = ev.type == SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN;
        t->xy[f][0] = ev.x;
        t->xy[f][1] = ev.y;
    } else if (!t->fingerDown[f]) {
        t->xy[f][0] = ev.x;
        t->xy[f][1] = ev.y;
    } else { // two-sample smoothing
        t->xy[f][0] = (t->xy[f][0] - ev.x) * 0.5f + ev.x;
        t->xy[f][1] = (t->xy[f][1] - ev.y) * 0.5f + ev.y;
    }
    int a = kNoFinger;
    if (t->active < kNoFinger && t->fingerDown[t->active])
        a = t->active;
    else if (t->fingerDown[0])
        a = 0;
    else if (t->fingerDown[1])
        a = 1;
    if (a == kNoFinger) {
        raw.cur[8] = raw.cur[9] = 0.5f;
        if (t->startMs != 0) {
            if (t->tapCandidate && t->startMs < ts && ts - t->startMs <= g_touch.tapMaxMs)
                t->tapFinger = t->active;
            t->startMs = 0;
            t->tapCandidate = false;
            t->moved = false;
        }
    } else {
        const float x = t->xy[a][0], y = t->xy[a][1];
        raw.cur[8] = x;
        raw.cur[9] = y;
        if (t->startMs == 0 || a != t->active) {
            t->anchorX = x;
            t->anchorY = y;
            t->active = a;
            t->startMs = ts != 0 ? ts : 1;
            t->tapFinger = kNoFinger;
            t->tapCandidate = true;
        } else if (!t->moved) {
            float thr = g_touch.moveThreshold;
            thr = thr < 0.0f ? 0.0f : (thr > 0.5f ? 0.5f : thr);
            const float dx = x - t->anchorX, dy = y - t->anchorY;
            if (dx * dx + dy * dy > thr * thr) {
                t->tapCandidate = false;
                t->moved = true;
                t->anchorX = x;
                t->anchorY = y;
            }
        }
    }
    TouchButtons(r, raw);
}

// --- sensors -----------------------------------------------------------------

void SensorEvent(Record &r, const SDL_GamepadSensorEvent &ev, uint32_t ts) {
    SensorState *s = r.sensor.get();
    if (s == nullptr)
        return;
    if (s->lastTs != 0 && ts != s->lastTs) {
        const float dt = s->prevTs != 0 ? static_cast<float>(ts - s->lastTs) * 0.001f : 0.0f;
        s->motion.ProcessMotion(s->gyro[0], s->gyro[1], s->gyro[2], s->accel[0], s->accel[1],
                                s->accel[2], dt);
        s->prevTs = s->lastTs;
    }
    if (ev.sensor == SDL_SENSOR_ACCEL) {
        for (int i = 0; i < 3; ++i)
            s->accel[i] = ev.data[i] * 0.10204081f; // m/s^2 to g
        s->lastTs = ts;
    } else if (ev.sensor == SDL_SENSOR_GYRO) {
        s->lastTs = ts;
        for (int i = 0; i < 3; ++i)
            s->gyro[i] = ev.data[i] * 57.29578f; // rad/s to deg/s
    }
}

// The gyro "stick" axes 6 / 7 from the fused gravity vector, and the
// calibration window: 2 s of continuous calibration after a connect or a
// lost gravity vector, then fixed.
void GyroAxes(Record &r, const Slot &slot, Raw &raw, uint64_t now) {
    SensorState *s = r.sensor.get();
    if (s == nullptr)
        return;
    float g[3];
    s->motion.GetGravity(g[0], g[1], g[2]);
    const float m2 = g[0] * g[0] + g[1] * g[1] + g[2] * g[2];
    const bool have = m2 >= 2.3841858e-07f;
    if (s->calibUntil == 0) {
        if (!have) {
            s->motion.ResetContinuousCalibration();
            s->motion.SetCalibrationMode(GamepadMotionHelpers::Stillness | GamepadMotionHelpers::SensorFusion);
            s->calibUntil = now + 2 * kSecondNs;
            return;
        }
    } else if (have && s->calibUntil < now) {
        s->motion.SetCalibrationMode(GamepadMotionHelpers::Manual);
        s->calibUntil = 0;
    }
    if (have) {
        const float inv = 1.0f / std::sqrt(m2);
        raw.cur[6] = std::asin(g[0] * inv) * 1.25f;
        const float a = std::asin(g[2] * inv);
        raw.cur[7] = a * (a >= 0.0f ? 1.25f : 1.5f);
    }
    for (uint32_t i = 6; i < slot.axisCount && i < Names::kAxisCount; ++i)
        raw.min[i] = raw.max[i] = raw.cur[i];
}

// --- the drain (0x141275B60) -----------------------------------------------

void Axis(int slot, Raw &raw, int a, int16_t value, uint64_t now) {
    if (a < 0 || static_cast<uint32_t>(a) >= g_slots[slot].axisCount || a >= Names::kAxisCount)
        return;
    const float v = static_cast<float>(value) * kAxisScale;
    raw.lastInput = now;
    raw.cur[a] = v;
    if (v < raw.min[a])
        raw.min[a] = v;
    if (v > raw.max[a])
        raw.max[a] = v;
}

void SetBits(Raw &raw, uint32_t oldBits, uint32_t newBits) {
    raw.buttons = newBits;
    raw.pressed |= ~oldBits & newBits;
    raw.released |= oldBits & ~newBits;
}

void Button(int slot, Raw &raw, int b, bool down, uint64_t now) {
    if (b < 0 || static_cast<uint32_t>(b) >= g_slots[slot].buttonCount || b >= 32)
        return;
    const uint32_t bit = 1u << b;
    raw.lastInput = now;
    SetBits(raw, raw.buttons, down ? raw.buttons | bit : raw.buttons & ~bit);
}

// A raw joystick's hat: its last four raw buttons, whatever the hat index.
void Hat(int slot, Raw &raw, uint8_t value) {
    const uint32_t n = g_slots[slot].buttonCount;
    if (n < 4)
        return;
    uint32_t bits = raw.buttons & ~(0xFu << (n - 4));
    if (value & SDL_HAT_UP)
        bits |= 1u << (n - 4);
    if (value & SDL_HAT_DOWN)
        bits |= 1u << (n - 3);
    if (value & SDL_HAT_LEFT)
        bits |= 1u << (n - 2);
    if (value & SDL_HAT_RIGHT)
        bits |= 1u << (n - 1);
    SetBits(raw, raw.buttons, bits);
}

void Drain(uint64_t now) {
    SDL_PumpEvents();
    SDL_Event events[64];
    int n;
    while ((n = SDL_PeepEvents(events, 64, SDL_GETEVENT, SDL_EVENT_JOYSTICK_AXIS_MOTION,
                               SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED)) > 0) {
        for (int i = 0; i < n; ++i) {
            const SDL_Event &ev = events[i];
            const uint32_t ts = static_cast<uint32_t>(ev.common.timestamp / 1000000ull);
            int slot = -1;
            Record *r;
            switch (ev.type) {
            case SDL_EVENT_JOYSTICK_ADDED:
            case SDL_EVENT_GAMEPAD_ADDED:
                Add(ev.jdevice.which);
                break;
            case SDL_EVENT_JOYSTICK_REMOVED:
            case SDL_EVENT_GAMEPAD_REMOVED:
                Remove(ev.jdevice.which);
                break;
            case SDL_EVENT_JOYSTICK_AXIS_MOTION:
                if ((r = Assigned(ev.jaxis.which, slot)) != nullptr && r->pad == nullptr)
                    Axis(slot, g_raw[slot], ev.jaxis.axis, ev.jaxis.value, now);
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if ((r = Assigned(ev.gaxis.which, slot)) != nullptr)
                    Axis(slot, g_raw[slot], ev.gaxis.axis, ev.gaxis.value, now);
                break;
            case SDL_EVENT_JOYSTICK_HAT_MOTION:
                if ((r = Assigned(ev.jhat.which, slot)) != nullptr && r->pad == nullptr)
                    Hat(slot, g_raw[slot], ev.jhat.value);
                break;
            case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
            case SDL_EVENT_JOYSTICK_BUTTON_UP:
                if ((r = Assigned(ev.jbutton.which, slot)) != nullptr && r->pad == nullptr)
                    Button(slot, g_raw[slot], ev.jbutton.button, ev.jbutton.down, now);
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                if ((r = Assigned(ev.gbutton.which, slot)) != nullptr) {
                    Button(slot, g_raw[slot], ev.gbutton.button, ev.gbutton.down, now);
                    if (ev.gbutton.button == SDL_GAMEPAD_BUTTON_TOUCHPAD)
                        TouchButtons(*r, g_raw[slot]);
                }
                break;
            case SDL_EVENT_JOYSTICK_BATTERY_UPDATED:
                if ((r = Assigned(ev.jbattery.which, slot)) != nullptr)
                    g_slots[slot].power = PowerOf(ev.jbattery.state, ev.jbattery.percent,
                                                  ev.jbattery.state == SDL_POWERSTATE_CHARGING ||
                                                      ev.jbattery.state == SDL_POWERSTATE_CHARGED);
                break;
            case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
            case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
            case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
                if ((r = Assigned(ev.gtouchpad.which, slot)) != nullptr) {
                    g_raw[slot].lastInput = now;
                    TouchEvent(*r, g_raw[slot], ev.gtouchpad, ts);
                }
                break;
            case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
                if ((r = Assigned(ev.gsensor.which, slot)) != nullptr) {
                    g_raw[slot].lastInput = now;
                    SensorEvent(*r, ev.gsensor, ts);
                }
                break;
            default:
                break;
            }
        }
    }
    // Nothing else reads SDL's queue; drop what is not ours so it cannot fill.
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
}

void Remove(SDL_JoystickID id) {
    Record *r = ByInstance(id);
    if (r == nullptr)
        return;
    if (r->slot >= 0)
        g_raw[r->slot].stamp = 0; // the poll drops the slot
    Close(*r);
}

void Push(Record &r, const Slot &slot, uint64_t now) {
    if (slot.hasRumble && (r.expiry[0] != 0 || r.expiry[1] != 0)) {
        SDL_RumbleJoystick(r.joy, r.rumble[0], r.rumble[1], 250);
        for (int i = 0; i < 2; ++i)
            if (r.expiry[i] != 0 && r.expiry[i] <= now) {
                r.expiry[i] = 0;
                r.rumble[i] = 0;
            }
    }
    if (slot.hasTriggerRumble && (r.expiry[2] != 0 || r.expiry[3] != 0)) {
        SDL_RumbleJoystickTriggers(r.joy, r.rumble[2], r.rumble[3], 250);
        for (int i = 2; i < 4; ++i)
            if (r.expiry[i] != 0 && r.expiry[i] <= now) {
                r.expiry[i] = 0;
                r.rumble[i] = 0;
            }
    }
    if (slot.hasLED)
        SDL_SetJoystickLED(r.joy, static_cast<uint8_t>(r.led >> 16), static_cast<uint8_t>(r.led >> 8),
                           static_cast<uint8_t>(r.led));
}

Record *RecordOf(const Slot &slot) {
    return slot.id >= 1 && slot.id <= 4 ? g_slotRecord[slot.id - 1] : nullptr;
}

} // namespace

Slot *SlotByID(uint32_t id) {
    if (id < 1 || id > kSlotCount)
        return nullptr;
    Slot &s = g_slots[id - 1];
    s.id = id;
    return &s;
}

Raw &RawOf(int index) { return g_raw[index]; }

TouchSettings &Touch() { return g_touch; }

uint64_t NowNs() { return SDL_GetTicksNS(); }

bool AppActive() {
    using GetWindow_t = HWND(__fastcall *)(int which);
    const HWND game = reinterpret_cast<GetWindow_t>(static_cast<uintptr_t>(Offsets::FUN_OS_GUI_GET_WINDOW))(0);
    return game != nullptr && GetForegroundWindow() == game;
}

bool Init() {
    for (auto &r : g_records)
        r->slot = -1;
    for (Record *&r : g_slotRecord)
        r = nullptr;
    g_pendingAdds = 0;
    SDL_SetHint(SDL_HINT_APP_NAME, "World of Warcraft");
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
    // (3.80.2 also sets SDL_ACCELEROMETER_AS_JOYSTICK=0, which is SDL3's
    // behaviour: the hint is gone.)
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");
    g_sdl = SDL_Init(SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD);
    if (!g_sdl)
        return false;
    // 3.80.2's built-in mappings, verbatim.
    static const char *const kMappings[] = {
        "030000006d0400001ac2000000000000,Logitech Precision Gamepad,platform:Windows,a:b1,b:b2,x:b0,"
        "y:b3,back:b8,start:b9,leftshoulder:b4,rightshoulder:b5,leftx:a0,lefty:a1,lefttrigger:b6,"
        "righttrigger:b7,",
        "030000004f0400000204000000000000,Joystick - HOTAS Warthog,platform:Windows,crc:1883,a:b12,"
        "b:b11,x:b13,y:b10,back:h0.8,guide:h0.1,start:h0.2,leftstick:b1,rightstick:b18,leftshoulder:b2,"
        "rightshoulder:b4,dpup:b6,dpdown:b8,dpleft:b9,dpright:b7,misc1:h0.4,leftx:a0,lefty:a1,"
        "-rightx:b17,+rightx:b15,-righty:b14,+righty:b16,lefttrigger:b3,righttrigger:b0,",
        "030000004f0400000404000000000000,Throttle - HOTAS Warthog,platform:Windows,crc:3a9d,a:h0.1,"
        "b:h0.2,x:h0.8,y:h0.4,back:b11,guide:b14,start:b10,leftstick:b0,rightstick:b1,leftshoulder:b7,"
        "rightshoulder:-a2,dpup:b12,dpdown:b13,dpleft:b9,dpright:b8,misc1:b20,paddle1:b15,paddle2:b16,"
        "paddle3:b30,paddle4:b31,leftx:a0,lefty:a1~,-rightx:b2,+rightx:b4,-righty:b3,+righty:b5,"
        "lefttrigger:b6,righttrigger:+a3,",
        "030000001d2300000102000000000000,VKBsim Gladiator EVO L,platform:Windows,crc:a5ec,a:b20,b:b23,"
        "x:b22,y:b21,back:b30,guide:b29,start:b31,leftstick:b13,rightstick:b14,leftshoulder:b15,"
        "rightshoulder:b9,dpup:+a4,dpdown:-a4,dpleft:-a3,dpright:+a3,misc1:b28,paddle1:b34,paddle2:b12,"
        "paddle3:b11,paddle4:b32,leftx:a0,lefty:a1,-rightx:b26,+rightx:b27,-righty:b25,+righty:b24,"
        "lefttrigger:b10,righttrigger:b35,",
        "0300000044330000f801000000000000,VPC Rudder Pedals,platform:Windows,crc:58b0,leftx:a0,"
        "-lefty:a1,+lefty:a2,",
    };
    for (const char *m : kMappings)
        SDL_AddGamepadMapping(m);
    // WTF\gamecontrollerdb.txt: created with 3.80.2's header when missing,
    // then loaded. Only when WTF exists.
    const DWORD attr = GetFileAttributesA("WTF");
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        constexpr const char *kDb = "WTF/gamecontrollerdb.txt";
        if (GetFileAttributesA(kDb) == INVALID_FILE_ATTRIBUTES) {
            FILE *f = nullptr;
            if (fopen_s(&f, kDb, "wb") == 0 && f != nullptr) {
                static const char kHeader[] =
                    "# SDL2 Game Controller Mappings File\r\n"
                    "# Add SDL mapping strings here for new or custom game controller mappings.\r\n"
                    "# This file will not be overwritten once it has been created.\r\n";
                std::fwrite(kHeader, 1, sizeof kHeader - 1, f);
                std::fclose(f);
            }
        }
        SDL_AddGamepadMappingsFromFile(kDb);
    }
    return true;
}

void Quit() {
    if (!g_sdl)
        return;
    g_sdl = false;
    for (auto &r : g_records)
        if (r->pad != nullptr)
            Close(*r);
    g_records.clear();
    for (Record *&r : g_slotRecord)
        r = nullptr;
    SDL_QuitSubSystem(SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD);
}

bool Running() { return g_sdl; }

void Frame(uint64_t now) {
    if (!g_sdl)
        return;
    for (int i = 0; i < 4; ++i) {
        Record *r = g_slotRecord[i];
        const Slot &s = g_slots[i];
        if (r == nullptr || !s.connected || !s.opened || r->instance == 0)
            continue;
        Push(*r, s, now);
        // A tap is a press for one poll.
        if (s.hasTouchpad && r->touch && r->touch->tapFinger < kNoFinger) {
            r->touch->tapFinger = kNoFinger;
            TouchButtons(*r, g_raw[i]);
        }
    }
    Drain(now);
    for (int i = 0; i < 4; ++i) {
        Record *r = g_slotRecord[i];
        if (r == nullptr)
            continue;
        g_raw[i].stamp = r->instance != 0 ? now : 0;
        if (g_slots[i].hasGyro)
            GyroAxes(*r, g_slots[i], g_raw[i], now);
    }
}

bool TakePendingAdd() {
    if (g_pendingAdds == 0)
        return false;
    --g_pendingAdds;
    return true;
}

bool Fill(Slot &slot) {
    const int i = static_cast<int>(slot.id) - 1;
    if (i < 0 || i > 3 || g_slotRecord[i] != nullptr)
        return false;
    Record *rec = nullptr;
    for (auto &r : g_records)
        if (r->instance != 0 && r->slot < 0) {
            rec = r.get();
            break;
        }
    if (rec == nullptr)
        return false;
    g_slotRecord[i] = rec;
    rec->slot = i;
    const char *name = rec->pad != nullptr ? SDL_GetGamepadName(rec->pad) : nullptr;
    if (name == nullptr)
        name = SDL_GetJoystickName(rec->joy);
    char fallback[32];
    if (name == nullptr) {
        std::snprintf(fallback, sizeof fallback, "GamePad %u", slot.id);
        name = fallback;
    }
    slot.name.assign(name, std::min<size_t>(std::strlen(name), 255));
    slot.vendor = SDL_GetJoystickVendor(rec->joy);
    slot.product = SDL_GetJoystickProduct(rec->joy);
    if (slot.vendor == 0) {
        if (slot.product == 0 && ContainsI(slot.name.c_str(), "Nintendo") &&
            ContainsI(slot.name.c_str(), "Switch")) {
            slot.vendor = 0x57E;
            slot.product = 3;
            rec->type = Config::PadType::SwitchPro;
        }
        if (slot.vendor == 0)
            slot.vendor = 1;
    }
    const SDL_PropertiesID props = SDL_GetJoystickProperties(rec->joy);
    slot.hasRumble = SDL_GetBooleanProperty(props, SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, false);
    slot.hasTriggerRumble = SDL_GetBooleanProperty(props, SDL_PROP_JOYSTICK_CAP_TRIGGER_RUMBLE_BOOLEAN, false);
    slot.hasLED = SDL_GetBooleanProperty(props, SDL_PROP_JOYSTICK_CAP_RGB_LED_BOOLEAN, false);
    slot.connected = true;
    slot.opened = true;
    slot.hasGyro = false;
    slot.hasTouchpad = false;
    if (rec->pad == nullptr) {
        const int buttons = SDL_GetNumJoystickButtons(rec->joy) + (rec->hats > 0 ? 4 : 0);
        slot.buttonCount = static_cast<uint32_t>(std::min(std::max(buttons, 0), 32));
        slot.axisCount = static_cast<uint32_t>(std::min(std::max(SDL_GetNumJoystickAxes(rec->joy), 0), 10));
    } else {
        slot.buttonCount = 21;
        slot.axisCount = 6;
        if (SDL_GamepadHasSensor(rec->pad, SDL_SENSOR_ACCEL) && SDL_GamepadHasSensor(rec->pad, SDL_SENSOR_GYRO)) {
            SDL_SetGamepadSensorEnabled(rec->pad, SDL_SENSOR_ACCEL, true);
            SDL_SetGamepadSensorEnabled(rec->pad, SDL_SENSOR_GYRO, true);
            rec->sensor = std::make_unique<SensorState>();
            rec->sensor->motion.SetCalibrationMode(GamepadMotionHelpers::Stillness |
                                                   GamepadMotionHelpers::SensorFusion);
            rec->sensor->calibUntil = NowNs() + 2 * kSecondNs;
            slot.hasGyro = true;
            slot.axisCount = 8;
        }
        if (SDL_GetNumGamepadTouchpads(rec->pad) > 0 && SDL_GetNumGamepadTouchpadFingers(rec->pad, 0) != 0) {
            rec->touch = std::make_unique<TouchState>();
            slot.hasTouchpad = true;
            slot.buttonCount = 23;
            slot.axisCount = 10;
        }
    }
    slot.power = PowerOf(rec->joy);
    Config::DeviceDesc &d = slot.desc;
    d.vendor = slot.vendor;
    d.product = slot.product;
    d.gamepad = rec->pad != nullptr;
    d.hats = rec->pad == nullptr && rec->hats > 0;
    d.touchpad = slot.hasTouchpad;
    d.type = rec->type;
    d.buttonCount = slot.buttonCount;
    d.axisCount = slot.axisCount;
    d.name = slot.name.c_str();
    return true;
}

void Release(Slot &slot) {
    Record *r = RecordOf(slot);
    if (r == nullptr)
        return;
    r->slot = -1;
    g_slotRecord[slot.id - 1] = nullptr;
}

bool Alive(const Slot &slot) {
    const Record *r = RecordOf(slot);
    return r != nullptr && r->instance != 0;
}

void SetRumble(const Slot &slot, int type, float level, uint64_t untilNs) {
    Record *r = RecordOf(slot);
    if (r == nullptr || type < 0 || type > 3)
        return;
    r->rumble[type] = static_cast<uint16_t>(static_cast<int>(level * 65534.0f));
    r->expiry[type] = untilNs;
}

void StopRumble(const Slot &slot, uint64_t now) {
    Record *r = RecordOf(slot);
    if (r == nullptr)
        return;
    std::memset(r->rumble, 0, sizeof r->rumble);
    for (uint64_t &e : r->expiry)
        e = now;
}

void SetLED(const Slot &slot, uint32_t rgb) {
    if (Record *r = RecordOf(slot))
        r->led = rgb;
}

int AddMapping(const char *mapping) { return SDL_AddGamepadMapping(mapping); }

} // namespace GamePad::Device
