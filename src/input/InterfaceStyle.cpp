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

// The interface style: keyboard and mouse, or gamepad. Retail's camelot
// client has it; no PC client does (the cvar is in none of Ketho's dumps),
// and its exe is encrypted on disk, so the contract comes from its generated
// API documentation, its Lua, and a probe in it (2026-11-25):
//
//   - The cvar `InputDeviceInterfaceStyle` holds an
//     Enum.InputDeviceInterfaceType (0 Mkb, 1 Gamepad). Its writers are Lua
//     SetCVar calls: the Settings "Enable Gamepad UI" checkbox
//     (Blizzard_SettingsDefinitions_Shared/Gamepad.lua, default
//     PlatformIsHandheld(), so 0 here) and the kiosk swap dialogs.
//     InputUtil.IsGamepadUISupported() is "the cvar exists".
//   - C_InputInterfaceStyle.GetCurrentStyle() (Environment All) is the style
//     the interface was built with. Probed: after SetCVar(1) returns it still
//     answers 0; after the reload, 1.
//   - A SetCVar that changes the value queues an interface reload: SetCVar
//     returns, the code after it runs, then the interface reloads. A SetCVar
//     to the current value reloads nothing.
//   - INPUT_DEVICE_INTERFACE_TRANSITION (documented, SynchronousEvent) never
//     fired in the probe: not inside SetCVar, not while the new interface
//     loaded through PLAYER_ENTERING_WORLD. It is reserved so RegisterEvent
//     takes it, and never fired.
//
// Each interface load (the world pass, the glue pass) reads the style from
// the cvar. A change in the world sets VAR_UI_RELOAD_REQUESTED, which is what
// ReloadUI does once its protected-action gate passes; the gate always passes
// for the client's own code, and the camelot reload is client code (it ran
// from a slash command and from the Settings checkbox alike). A change on the
// glue screens reloads nothing here: the world pass reads the new style when
// the world interface loads.

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "event/Custom.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace Input::InterfaceStyle {

namespace {

constexpr int kMkb = 0;
constexpr int kGamepad = 1;

CVar::Factory::Handle g_cvar = nullptr;
int g_style = kMkb;
bool g_inWorld = false;

const Game::Lua::EnumIntegerEntry kInterfaceType[] = {
    {"Mkb", kMkb},
    {"Gamepad", kGamepad},
};

const Game::Doc::Field kTransitionPayload[] = {
    Game::Doc::Req("newMode", "InputDeviceInterfaceType"),
    Game::Doc::Req("oldMode", "InputDeviceInterfaceType"),
};
const Game::Doc::Event kTransitionDoc{
    "Input", "Never fires: a change of InputDeviceInterfaceStyle reloads the interface instead.",
    kTransitionPayload};
const Event::Custom::AutoReserve _transition{"INPUT_DEVICE_INTERFACE_TRANSITION", &kTransitionDoc};

bool ParseStyle(const char *value, int &style) {
    if (value == nullptr)
        return false;
    char *end = nullptr;
    const long n = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || (n != kMkb && n != kGamepad))
        return false;
    style = static_cast<int>(n);
    return true;
}

int __fastcall OnStyle(CVar::Factory::Handle, const char *prev, const char *value, void *) {
    int style = kMkb;
    if (!ParseStyle(value, style))
        return 0;
    const bool changed = prev == nullptr || std::strcmp(prev, value) != 0;
    if (changed && g_inWorld)
        *reinterpret_cast<uint8_t *>(Offsets::VAR_UI_RELOAD_REQUESTED) = 1;
    return 1;
}

int __fastcall Script_GetCurrentStyle(void *L) {
    Game::Lua::PushNumber(L, g_style);
    return 1;
}

const Game::Doc::Field kStyleRet[] = {Game::Doc::Req("style", "InputDeviceInterfaceType")};
const Game::Doc::Function kGetCurrentStyle{
    "Returns the interface style the interface loaded with. A change of InputDeviceInterfaceStyle "
    "reloads the interface.",
    {}, kStyleRet};

void LoadInterface(bool inWorld) {
    g_inWorld = inWorld;
    if (!ParseStyle(CVar::Factory::GetString(g_cvar), g_style))
        g_style = kMkb;
    Game::Lua::RegisterIntegerEnum("Enum", "InputDeviceInterfaceType", kInterfaceType, "InputConstants");
    Game::Lua::RegisterTableFunction("C_InputInterfaceStyle", "GetCurrentStyle", &Script_GetCurrentStyle,
                                     &kGetCurrentStyle);
}

void RegisterGlue() {
    g_inWorld = false;
    // Every glue boot: a cvar that exists already only gets its callback
    // again, called with its current value, which changes nothing.
    g_cvar = CVar::Factory::Register("InputDeviceInterfaceStyle",
                                     "The interface style: 0 keyboard and mouse, 1 gamepad",
                                     Offsets::CVAR_CATEGORY_DEFAULT, "0", &OnStyle);
    LoadInterface(false);
}

void RegisterWorld() { LoadInterface(true); }

const Game::GlueModuleAutoRegister _glue{&RegisterGlue};
const Game::ModuleAutoRegister _world{&RegisterWorld};

} // namespace

} // namespace Input::InterfaceStyle
