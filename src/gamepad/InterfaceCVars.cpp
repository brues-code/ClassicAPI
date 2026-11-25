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

// The gamepad interface's settings: cvars retail's camelot client registers
// and only its gamepad Lua reads (Blizzard_Gamepad, _GamepadSmartNavigation,
// _GamepadSharedUtility read each one through GetCVar, CVarCallbackRegistry
// or SetTempCVar; no PC client has them). The engine only keeps them, so this
// registers them and nothing reads them here. Names, defaults, help and
// category (4, game) are the _forever_ 1.60.1 client's own, from
// ConsoleGetAllCommands and C_CVar.GetCVarDefault in game (build 16001,
// 2026-11-25).
//
// The gamepad cvars that drive engine behavior (CameraFollow*,
// autoLootDefault, autoRangedCombat, cameraZoomSpeed) are not here: each
// comes with the feature it controls.

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"

namespace GamePad::InterfaceCVars {

namespace {

struct Entry {
    const char *name;
    const char *value;
    const char *help;
};

constexpr Entry kCVars[] = {
    {"GamepadCompassCustomFilterBoss", "0", "Adds Boss icons to the compass's custom filter."},
    {"GamepadCompassCustomFilterCritter", "0", "Adds Critter icons to the compass's custom filter."},
    {"GamepadCompassCustomFilterEnemy", "0", "Adds Enemy icons to the compass's custom filter."},
    {"GamepadCompassCustomFilterFriendly", "0", "Adds Friendly icons to the compass's custom filter."},
    {"GamepadCompassCustomFilterParty", "0", "Adds Party icons to the compass's custom filter."},
    {"GamepadEnableReticleSwingTimer", "1", "Shows a swing timer on the gamepad reticle while attacking."},
    {"GamepadHudModifierUsesToggle", "0",
     "Allows the HUD Modifier state to be toggled on/off when tapped instead of when held/released."},
    {"GamepadRaidTargetingHoverMode", "0",
     "When raid targeting spells will target the indicated player without changing the player's current "
     "target."},
    {"GamepadShowPersistentInputLegend", "1",
     "Indicates if the list of gamepad actions should always be shown on screen."},
    {"GamepadSwapFriendlyTargetActions", "0",
     "Swaps the dpad and face button actions when holding the friendly target modifier."},
    {"GamepadSwapHostileTargetActions", "0",
     "Swaps the dpad and face button actions when holding the hostile target modifier."},
    {"GamepadSwapTargetModifiers", "0", "Swaps the friendly and hostile target modifiers."},
    {"GamepadTabFriendlyTargetAction", "1", "Tab target friendlies on target action release"},
    {"GamepadTabHostileTargetAction", "1", "Tab target hostiles on target action release"},
    {"GamepadTargetingModifierVisualDelay", "0.350000",
     "The duration that the Targeting Modifier can be held before updating the UI. Releasing within this "
     "window will tab target."},
    {"GamepadUsePartyTargeting", "1",
     "If enabled, target specific party members with the dpad rather than using directional targeting"},
    {"SmartNavigationScrollSpeed", "5000.000000", "How fast areas are scrolled using the right stick."},
};

void Register() {
    for (const Entry &e : kCVars)
        CVar::Factory::Register(e.name, e.help, Offsets::CVAR_CATEGORY_GAME, e.value);
}

const Game::GlueModuleAutoRegister _glue{&Register};

} // namespace

} // namespace GamePad::InterfaceCVars
