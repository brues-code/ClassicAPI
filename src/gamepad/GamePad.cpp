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

// The gamepad system's lifetime and per-frame work, after the 3.80.2 client.
//
// Lifetime. GamePadEnable's change callback (0x1412635B0) starts
// (GamePad::Start 0x141263430) and stops (0x141258FE0) it, so a saved
// GamePadEnable=1 starts it when the cvar registers. Ours registers in the
// first glue pass. A start resets the five slots and the config layers,
// starts SDL, builds the generic config, writes GamePadConfig_Default.json
// once a session, loads the user and addon config files and fires
// GAME_PAD_CONFIGS_CHANGED. A stop fires nothing.
//
// Logout (3.80.2's world shutdown 0x141692FD0 with quitting 0) saves the
// addon configs and reloads every layer (0x141263280), which fires
// GAME_PAD_CONFIGS_CHANGED; here that is the next glue pass. Exit saves them
// and stops, from the engine's exit handlers.
//
// Each frame (0x14125B710, then the LED / power half of 0x141264B20) the
// poll fills the raw states, takes one newly added device into the first
// empty slot, drops a slot whose device closed, maps every device, builds the
// combined one, then pushes the LED colour and fires GAME_PAD_POWER_CHANGED
// and UPDATE_BINDINGS for the active device.

#include "gamepad/GamePad.h"

#include "Game.h"
#include "Offsets.h"
#include "creature/Info.h"
#include "cvar/Factory.h"
#include "event/Custom.h"
#include "gamepad/Buttons.h"
#include "gamepad/Cursor.h"
#include "gamepad/Config.h"
#include "gamepad/Device.h"
#include "gamepad/Movement.h"
#include "gamepad/State.h"
#include "tick/FrameTick.h"
#include "unit/Identity.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace GamePad {

namespace {

constexpr uint64_t kSecondNs = 1000000000ull;

// --- events -------------------------------------------------------------------

const Game::Doc::Event kConnectedDoc{"GamePad", "Fires when a gamepad connects.", {}};
const Game::Doc::Event kDisconnectedDoc{"GamePad", "Fires when a gamepad disconnects.", {}};
const Game::Doc::Event kConfigsChangedDoc{
    "GamePad", "Fires when the gamepad configs load: when gamepad support starts, on logout and when "
               "GamePadStickAxisButtons changes.",
    {}};
const Game::Doc::Field kPowerPayload[] = {
    Game::Doc::Req("powerLevel", "GamePadPowerLevel", "The active gamepad's battery level."),
};
const Game::Doc::Event kPowerChangedDoc{"GamePad", "Fires when the active gamepad's battery level changes.",
                                        kPowerPayload};

const Event::Custom::AutoReserve _connected{"GAME_PAD_CONNECTED", &kConnectedDoc};
const Event::Custom::AutoReserve _disconnected{"GAME_PAD_DISCONNECTED", &kDisconnectedDoc};
const Event::Custom::AutoReserve _configsChanged{"GAME_PAD_CONFIGS_CHANGED", &kConfigsChangedDoc};
const Event::Custom::AutoReserve _powerChanged{"GAME_PAD_POWER_CHANGED", &kPowerChangedDoc};

// --- state ----------------------------------------------------------------------

bool g_started = false;
bool g_everStarted = false;
bool g_registering = false;    // inside the cvar registration call
bool g_defaultWritten = false; // GamePadConfig_Default.json written this session (0x146AD5888)
uint32_t g_active = Device::kCombinedID;
float g_vibrationStrength = 1.0f;
bool g_ledOverride = false;
uint32_t g_ledOverrideColor = 0;
uint32_t g_lastColor = 0;
bool g_ledDirty = false; // a device connected: its record starts with the LED off
uint8_t g_lastPower = Device::kPowerUnknown;
bool g_activeWasConnected = false;
bool g_seenGlue = false;

CVar::Factory::Handle g_cvStickAxisButtons = nullptr;
CVar::Factory::Handle g_cvFactionColor = nullptr;

// 0x141262870: once a session from the start; on every connect, except while
// the GamePadEnable cvar is still registering.
void WriteDefaultFile(bool force) {
    if (force ? g_registering : g_defaultWritten)
        return;
    g_defaultWritten = true;
    if (g_started)
        Config::WriteDefaultFile();
}

// 0x141262730: the GAMEPAD_CONNECTED / GAMEPAD_DISCONNECTED line in chat, the
// device's config name or else its name.
void ChatPrint(const char *stringName, uint32_t id) {
    if (Unit::Identity::PlayerObject() == nullptr)
        return; // no chat outside the world
    const char *fmt = Game::Lua::GlobalText(stringName);
    if (fmt[0] == '\0')
        return;
    const uint32_t d = id != 0 ? id : g_active;
    const Device::Slot *s = Device::SlotByID(d);
    const char *name = "";
    if (s != nullptr && s->connected) {
        const std::string &cfgName = State::ConfigOf(d).name;
        name = !cfgName.empty() ? cfgName.c_str() : s->name.c_str();
    }
    char buf[1024];
    std::snprintf(buf, sizeof buf, fmt, static_cast<int>(id), name);
    using ChatDispatch_t = void(__fastcall *)(const char *, int, const char *, int, const char *, const char *,
                                              const char *, int, int, int, int, int);
    reinterpret_cast<ChatDispatch_t>(static_cast<uintptr_t>(Offsets::FUN_CHAT_DISPATCH))(
        buf, Offsets::CHAT_TYPE_SYSTEM, nullptr, 0, nullptr, nullptr, nullptr, 0, 0, 0, 0, 0);
}

void Connected(uint32_t id) {
    g_ledDirty = true;
    ChatPrint("GAMEPAD_CONNECTED", id);
    WriteDefaultFile(true);
    Event::Custom::Fire(_connected.Slot(), "");
}

// 0x141259650: the disconnect callback runs first, while the slot still has
// its name.
void RemoveSlot(uint32_t id) {
    Device::Slot &s = *Device::SlotByID(id);
    ChatPrint("GAMEPAD_DISCONNECTED", id);
    Event::Custom::Fire(_disconnected.Slot(), "");
    if (s.opened)
        Device::Release(s);
    State::ResetSlot(id);
    State::InvalidateCombined();
}

void ApplyAll() {
    for (uint32_t id = 1; id <= Device::kSlotCount; ++id)
        if (Device::SlotByID(id)->connected)
            State::Resolve(id);
}

// 0x1412596E0 and the map / combine after it.
void Poll() {
    const uint64_t now = Device::NowNs();
    const bool active = Device::AppActive();
    bool rescan = active && Device::TakePendingAdd();
    Device::Frame(now);
    for (uint32_t id = 1; id <= 4; ++id) {
        Device::Slot &s = *Device::SlotByID(id);
        Device::Raw &raw = Device::RawOf(static_cast<int>(id) - 1);
        if (!s.connected) {
            if (!rescan)
                continue;
            rescan = false; // one empty slot a frame
            if (!Device::Fill(s))
                continue;
            State::Resolve(id);
            Connected(id);
            State::RawReset(raw, State::ConfigOf(id));
            raw.stamp = now;
        } else if (now - raw.stamp > kSecondNs && raw.lastInput != 0) {
            // The device closed. (One that never sent input is kept, as in
            // 3.80.2.)
            RemoveSlot(id);
        } else if (!active) {
            const uint64_t stamp = raw.stamp, lastInput = raw.lastInput;
            State::RawReset(raw, State::ConfigOf(id));
            raw.stamp = stamp;
            raw.lastInput = lastInput;
        }
    }
    State::MapAll();
    // The next frame starts from this one, with its edges cleared.
    for (uint32_t id = 1; id <= 4; ++id) {
        if (!Device::SlotByID(id)->connected)
            continue;
        Device::Raw &raw = Device::RawOf(static_cast<int>(id) - 1);
        raw.pressed = raw.released = 0;
        raw.touchDelta[0] = raw.touchDelta[1] = 0.0f;
        for (int a = 0; a < Names::kAxisCount; ++a)
            raw.min[a] = raw.max[a] = raw.cur[a];
    }
}

// The faction for the LED: the player's, else the character screen's
// selected character's, from its race; -1 when there is neither (0x141460500
// / 0x141460590). 0 Horde, 1 Alliance, 2 any other group.
int Faction(bool &havePlayer) {
    int race = 0;
    const uint8_t *fields = Unit::Identity::PlayerDescriptor();
    havePlayer = fields != nullptr;
    if (havePlayer) {
        race = Unit::Identity::Race(fields);
    } else {
        if (Game::Read<int>(Offsets::VAR_GLUE_SUSPENDED) != 0)
            return -1; // in the world, the roster is gone
        const int count = Game::Read<int>(Offsets::VAR_GLUE_ROSTER_COUNT);
        const int index = Game::Read<int>(Offsets::VAR_GLUE_ROSTER_SELECTED_INDEX);
        const uint8_t *records = Game::Read<const uint8_t *>(Offsets::VAR_GLUE_ROSTER_RECORDS);
        if (records == nullptr || index < 0 || index >= count)
            return -1;
        race = records[index * Offsets::GLUE_ROSTER_STRIDE + Offsets::OFF_GLUE_CHAR_RACE];
    }
    const char *english = nullptr;
    const char *localized = nullptr;
    if (!Creature::Info::FactionGroupForRace(race, &english, &localized) || english == nullptr)
        return -1;
    if (std::strcmp(english, "Horde") == 0)
        return 0;
    if (std::strcmp(english, "Alliance") == 0)
        return 1;
    return 2;
}

template <typename Fn> void ForTargets(Fn fn) {
    for (uint32_t id = 1; id <= 4; ++id) {
        if (g_active != Device::kCombinedID && id != g_active)
            continue;
        const Device::Slot &s = *Device::SlotByID(id);
        if (s.connected && s.opened)
            fn(s);
    }
}

// The LED and power half of 0x141264B20, plus its UPDATE_BINDINGS.
void UpdateActive() {
    const Device::Slot &active = *Device::SlotByID(g_active);
    if (active.connected != g_activeWasConnected) {
        g_activeWasConnected = active.connected;
        Event::Custom::Fire(Event::Custom::LookupByName("UPDATE_BINDINGS"), "");
    }
    if (!active.connected)
        return;
    bool havePlayer = false;
    uint32_t color;
    if (g_ledOverride) {
        color = g_ledOverrideColor;
        Faction(havePlayer);
        if (!havePlayer)
            g_ledOverride = false; // the override lasts while there is a player
    } else if (CVar::Factory::GetInt(g_cvFactionColor) != 0) {
        const int f = Faction(havePlayer);
        color = f == -1 ? 0x404040 : f == 0 ? 0x800000 : f == 1 ? 0x000080 : 0x606000;
    } else {
        color = 0;
    }
    if (color != g_lastColor || g_ledDirty) {
        g_lastColor = color;
        g_ledDirty = false;
        ForTargets([&](const Device::Slot &s) { Device::SetLED(s, color); });
    }
    if (active.power != g_lastPower) {
        g_lastPower = active.power;
        Event::Custom::Fire(_powerChanged.Slot(), "%d", static_cast<int>(g_lastPower));
    }
}

void Tick() {
    if (!g_started)
        return;
    Poll();
    Buttons::Dispatch();
    Movement::Update();
    UpdateActive();
}

const Tick::FrameTick::AutoSubscribe _tick{&Tick};

// --- start / stop / reload --------------------------------------------------

void ResetSlots() {
    for (uint32_t id = 1; id <= Device::kSlotCount; ++id)
        State::ResetSlot(id);
}

bool Start() {
    ResetSlots();
    Config::ClearAll();
    if (!Device::Init()) {
        Game::Console::Write("GamePad support could not be started.");
        return false;
    }
    Config::BuildGeneric(false); // 3.80.2 adds the stick buttons only on a reload
    ApplyAll();
    g_started = true;
    g_everStarted = true;
    Poll();
    WriteDefaultFile(false);
    const bool files = Config::LoadFiles();
    const bool addons = Config::LoadAddons();
    if (files || addons)
        ApplyAll();
    Config::ResetStagingSeed();
    Event::Custom::Fire(_configsChanged.Slot(), "");
    return true;
}

void Stop() {
    if (!g_started)
        return;
    g_started = false;
    Buttons::Reset();
    Movement::Reset();
    Cursor::Reset();
    Device::Quit();
    Config::ClearAll();
    ResetSlots();
    g_activeWasConnected = false;
}

// 0x141263280. `stickAxisButtons` is GamePadStickAxisButtons (passed in, as
// its change callback runs before the cvar holds the new value).
void Reload(bool stickAxisButtons) {
    if (!g_started)
        return;
    Config::ClearAll();
    Config::BuildGeneric(stickAxisButtons);
    Config::LoadFiles();
    Config::LoadAddons();
    ApplyAll();
    Config::ResetStagingSeed();
    Event::Custom::Fire(_configsChanged.Slot(), "");
}

// --- cvars --------------------------------------------------------------------

int __fastcall OnEnable(CVar::Factory::Handle, const char *, const char *value, void *) {
    const bool want = value != nullptr && (std::atoi(value) != 0 || _stricmp(value, "true") == 0);
    int result = 1;
    if (want != g_started) {
        if (!want)
            Stop();
        else
            result = Start() ? 1 : 0;
    }
    if (!g_registering)
        Game::Console::Write(g_started ? "GamePad input enabled." : "GamePad input disabled.");
    return result;
}

int __fastcall OnSingleActiveID(CVar::Factory::Handle, const char *, const char *value, void *) {
    const int n = value != nullptr ? std::atoi(value) : 0;
    if (n < 0 || n > static_cast<int>(Device::kCombinedID))
        return 0;
    g_active = n == 0 ? Device::kCombinedID : static_cast<uint32_t>(n);
    return 1;
}

int __fastcall OnStickAxisButtons(CVar::Factory::Handle, const char *, const char *value, void *) {
    if (!g_registering)
        Reload(value != nullptr && std::atoi(value) != 0);
    return 1;
}

int __fastcall OnVibrationStrength(CVar::Factory::Handle, const char *, const char *value, void *) {
    float v = value != nullptr ? static_cast<float>(std::strtod(value, nullptr)) : 0.0f;
    g_vibrationStrength = v < 0.0f ? 0.0f : (v > 1000.0f ? 1000.0f : v);
    return 1;
}

int __fastcall OnTouchMoveThreshold(CVar::Factory::Handle, const char *, const char *value, void *) {
    Device::Touch().moveThreshold = value != nullptr ? static_cast<float>(std::strtod(value, nullptr)) : 0.0f;
    return 1;
}

int __fastcall OnTouchTapMaxMs(CVar::Factory::Handle, const char *, const char *value, void *) {
    const int v = value != nullptr ? std::atoi(value) : 0;
    Device::Touch().tapMaxMs = v > 0 ? static_cast<uint32_t>(v) : 0;
    return 1;
}

template <bool Device::TouchSettings::*Field>
int __fastcall OnTouchFlag(CVar::Factory::Handle, const char *, const char *value, void *) {
    Device::Touch().*Field = value != nullptr && std::atoi(value) != 0;
    return 1;
}

template <float Device::TouchSettings::*Field>
int __fastcall OnTouchFloat(CVar::Factory::Handle, const char *, const char *value, void *) {
    Device::Touch().*Field = value != nullptr ? static_cast<float>(std::strtod(value, nullptr)) : 0.0f;
    return 1;
}

// --- console --------------------------------------------------------------------

// GamePadListDevices (0x141263D40): the physical slots from 4 down to 1,
// numbered by a counter from 0.
int __fastcall Console_ListDevices(const char *, const char *) {
    uint32_t count = 0;
    for (uint32_t id = 4; id >= 1; --id) {
        const Device::Slot &s = *Device::SlotByID(id);
        if (!s.connected)
            continue;
        const std::string &cfgName = State::ConfigOf(id).name;
        Game::Console::Printf("GamePad %u - VendorID: %i  ProductID: %i  '%s'", count,
                              static_cast<int>(s.vendor), static_cast<int>(s.product),
                              !cfgName.empty() ? cfgName.c_str() : s.name.c_str());
        ++count;
    }
    Game::Console::Printf("%u GamePad devices connected", count);
    return 1;
}

// --- lifecycle hooks --------------------------------------------------------------

// The game's own exit runs this after it tore its world, interface and
// graphics device down, so nothing the gamepad holds in them is released
// here (the cursor's show would call into the gone device): the addon
// configs are saved and SDL closes.
void __cdecl OnProcessExit() {
    if (!g_started)
        return;
    Config::SaveAddons();
    g_started = false;
    Device::Quit();
}

void RegisterGlue() {
    if (g_seenGlue) {
        // Back from the world: 3.80.2's logout saves the addon configs and
        // reloads every layer.
        if (g_started) {
            Config::SaveAddons();
            Reload(CVar::Factory::GetInt(g_cvStickAxisButtons) != 0);
        }
        return;
    }
    g_seenGlue = true;
    reinterpret_cast<int(__cdecl *)(void(__cdecl *)())>(static_cast<uintptr_t>(Offsets::FUN_CRT_ATEXIT))(
        &OnProcessExit);
    Game::Console::RegisterCommand("GamePadListDevices", &Console_ListDevices, Game::Console::CATEGORY_GAME,
                                   "List all connected GamePad devices in the console");
    using CVar::Factory::Register;
    constexpr int kCategory = Offsets::CVAR_CATEGORY_DEFAULT;
    g_registering = true;
    Register("GamePadSingleActiveID", "ID of single GamePad device to use. 0 = Use all devices' combined input",
             kCategory, "0", &OnSingleActiveID);
    g_cvStickAxisButtons = Register("GamePadStickAxisButtons",
                                    "Enables virtual buttons for the GamePad stick cardinal directions", kCategory,
                                    "0", &OnStickAxisButtons);
    Register("GamePadVibrationStrength", "GamePad vibration effect strength", kCategory, "1", &OnVibrationStrength);
    g_cvFactionColor = Register("GamePadFactionColor", "Enable setting GamePad's led color to match current faction",
                                kCategory, "1");
    Register("GamePadTouchCursorEnable", "Enable cursor control with GamePad's touch pad", kCategory, "1",
             &OnTouchFlag<&Device::TouchSettings::cursorEnable>);
    Register("GamePadTouchCursorMoveThreshold", "Change before touchpad moves the cursor (larger for easier taps)",
             kCategory, "0.042", &OnTouchMoveThreshold);
    Register("GamePadTouchCursorAccel", "Cursor acceleration for touchpad control", kCategory, "1.0",
             &OnTouchFloat<&Device::TouchSettings::accel>);
    Register("GamePadTouchCursorSpeed", "Cursor speed for touchpad control", kCategory, "1.0",
             &OnTouchFloat<&Device::TouchSettings::speed>);
    Register("GamePadTouchTapMaxMs", "Max time in ms for a touch to register a tap/click", kCategory, "200",
             &OnTouchTapMaxMs);
    Register("GamePadTouchTapButtons", "Enable touch tap to press TouchPad buttons", kCategory, "0",
             &OnTouchFlag<&Device::TouchSettings::tapButtons>);
    Register("GamePadTouchTapOnlyClick", "Only use taps for cursor clicks, do not use tap presses", kCategory, "1",
             &OnTouchFlag<&Device::TouchSettings::tapOnlyClick>);
    Register("GamePadTouchTapRightClick", "Taps for cursor clicks are right clicks instead of left clicks",
             kCategory, "0", &OnTouchFlag<&Device::TouchSettings::tapRightClick>);
    // Last, as in 3.80.2: a saved GamePadEnable=1 starts the gamepad here.
    Register("GamePadEnable", "Whether GamePad input should be enabled", kCategory, "0", &OnEnable);
    g_registering = false;
}

const Game::GlueModuleAutoRegister _glue{&RegisterGlue};

// 3.80.2's input-control reset (0x14125E6B0) on entering the world: the LED
// is pushed again and the power level reported again.
void RegisterWorld() {
    g_lastColor = 0;
    g_lastPower = Device::kPowerUnknown;
}

const Game::ModuleAutoRegister _world{&RegisterWorld};

} // namespace

bool Started() { return g_started; }

bool EverStarted() { return g_everStarted; }

uint32_t ActiveID() { return g_active; }

void ApplyConfigs() {
    Config::CopyLayer(Config::kStaging, Config::kAddons);
    ApplyAll();
}

void SetVibration(int type, float intensity) {
    float v = g_vibrationStrength * intensity;
    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    const uint64_t until = Device::NowNs() + kSecondNs / 2;
    ForTargets([&](const Device::Slot &s) { Device::SetRumble(s, type, v, until); });
}

void StopVibration() {
    const uint64_t now = Device::NowNs();
    ForTargets([&](const Device::Slot &s) { Device::StopRumble(s, now); });
}

uint32_t LedColor() { return g_ledOverride ? g_ledOverrideColor : g_lastColor; }

void SetLedOverride(uint32_t rgb) {
    g_ledOverrideColor = rgb;
    g_ledOverride = true;
}

void ClearLedOverride() { g_ledOverride = false; }

} // namespace GamePad
