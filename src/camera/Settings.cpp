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

// The ActionCam cvars, ported from the 3.80.2 Classic client
// (WowClassicGM-China.exe 3.80.2.69496, static initializers
// 0x1401a5080..0x1401a6640). Defaults, help and category are the retail
// ones (Ketho's classic CVars.lua, a dump of C_Console.GetAllCommands).
// The ranges are 3.80.2's validators: CVarValidateRange0To1 0x141b55880 and
// CVarValidateRange0To50 0x141b55580, which print "Value out of range
// (%f - %f)" and reject.
//
// Only the cvars a built feature reads are registered. The ActionCam
// command skips a cvar it cannot find, as 3.80.2's does, so its presets
// work before every feature exists.
// test_cameraDynamicPitchBaseFovPadFlying is not registered: 3.80.2 reads it
// only for a unit that can fly (movement flag 0x800000), which 1.12 has no
// counterpart for.

#include "camera/Settings.h"

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "storm/String.h"

namespace Camera::Settings {

namespace {

CVar::Factory::Handle g_keepCharacterCentered = nullptr;
CVar::Factory::Handle g_dynamicPitch = nullptr;
CVar::Factory::Handle g_baseFovPad = nullptr;
CVar::Factory::Handle g_baseFovPadDownScale = nullptr;
CVar::Factory::Handle g_smartPivotCutoffDist = nullptr;

bool InRange(const char *value, float min, float max) {
    const float v = Storm::ToFloat(value);
    if (v >= min && v <= max)
        return true;
    Game::Console::Printf("Value out of range (%f - %f)\n", static_cast<double>(min),
                          static_cast<double>(max));
    return false;
}

int __fastcall ValidateRange0To1(CVar::Factory::Handle, const char *, const char *value,
                                 void *) {
    return InRange(value, 0.0f, 1.0f);
}

int __fastcall ValidateRange0To50(CVar::Factory::Handle, const char *, const char *value,
                                  void *) {
    return InRange(value, 0.0f, 50.0f);
}

// Process-global, so the first glue boot covers the session.
void Register() {
    g_keepCharacterCentered = CVar::Factory::Register(
        "CameraKeepCharacterCentered",
        "Motion sickness control to keep character's head at center of screen to act as "
        "motion reference point. Can override other cvar settings.",
        Offsets::CVAR_CATEGORY_GAME, "1");
    g_dynamicPitch = CVar::Factory::Register(
        "test_cameraDynamicPitch",
        "Adjust camera pitch according to zoom distance for a more cinematic view",
        Offsets::CVAR_CATEGORY_GAME, "0.000000");
    g_baseFovPad = CVar::Factory::Register("test_cameraDynamicPitchBaseFovPad",
                                           "Fraction of screen height to keep feet below",
                                           Offsets::CVAR_CATEGORY_GAME, "0.400000",
                                           &ValidateRange0To1);
    g_baseFovPadDownScale = CVar::Factory::Register(
        "test_cameraDynamicPitchBaseFovPadDownScale",
        "Strength of dynamic pitch when looking down", Offsets::CVAR_CATEGORY_GAME,
        "0.250000", &ValidateRange0To1);
    g_smartPivotCutoffDist = CVar::Factory::Register(
        "test_cameraDynamicPitchSmartPivotCutoffDist",
        "Dynamic pitch disables Smart Pivot within this camera distance",
        Offsets::CVAR_CATEGORY_GAME, "10.000000", &ValidateRange0To50);
}

const Game::GlueModuleAutoRegister _autoreg{&Register};

} // namespace

bool KeepCharacterCentered() { return CVar::Factory::GetInt(g_keepCharacterCentered, 1) != 0; }

float DynamicPitch() { return CVar::Factory::GetFloat(g_dynamicPitch, 0.0f); }

float DynamicPitchBaseFovPad() { return CVar::Factory::GetFloat(g_baseFovPad, 0.4f); }

float DynamicPitchBaseFovPadDownScale() {
    return CVar::Factory::GetFloat(g_baseFovPadDownScale, 0.25f);
}

float DynamicPitchSmartPivotCutoffDist() {
    return CVar::Factory::GetFloat(g_smartPivotCutoffDist, 10.0f);
}

} // namespace Camera::Settings
