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

// `ActionCam [option]` — the 3.80.2 Classic client's console command
// (CGCamera_ConsoleCommandActionCam 0x141bc7560, registered by the CGCamera
// constructor 0x141b576f0 in category game with no help line).
//
// The table is 3.80.2's (0x144592f80, 13 rows). Each row sets up to six
// cvars with a plain set; a null value leaves that cvar alone, and a cvar
// that does not exist is skipped. Row 0's values are built from the
// compiled defaults with "%i" / "%g", which all print "0". An option is
// matched case-insensitively (__ascii_stricmp). A match prints
// "ACTION Camera set to '<name>' - '<description>'"; anything else prints
// the syntax with the rows from last to first. No Lua event and no
// confirmation, as in 3.80.2. CameraKeepCharacterCentered is not touched,
// so the features stay off until it is 0.

#include "Game.h"
#include "cvar/Factory.h"

#include <cstring>

namespace Camera::ActionCam {

namespace {

const char *const kCVars[] = {
    "test_cameraDynamicPitch",
    "test_cameraOverShoulder",
    "test_cameraTargetFocusEnemyEnable",
    "test_cameraTargetFocusInteractEnable",
    "test_cameraHeadMovementStrength",
    "nameplateOtherAtBase",
};
constexpr int kCVarCount = sizeof(kCVars) / sizeof(kCVars[0]);

struct Row {
    const char *name;
    const char *description;
    const char *values[kCVarCount];
};

const Row kRows[] = {
    {"default", "Default ActionCam settings", {"0", "0", "0", "0", "0", "0"}},
    {"on", "ActionCam", {"1", "1", "0", "1", "1", "0"}},
    {"off", "Disable action cam features", {"0", "0", "0", "0", "0", "0"}},
    {"basic", "Basic ActionCam", {"1", "0", "0", "0", "0", "0"}},
    {"full", "All action cam features on", {"1", "1", "1", "1", "1", "0"}},
    {"focusOff", "disable target focusing", {nullptr, nullptr, "0", "0", nullptr, nullptr}},
    {"focusInteract",
     "enable interact target focusing",
     {nullptr, nullptr, "0", "1", nullptr, nullptr}},
    {"focusEnemy", "enable enemy target focusing", {nullptr, nullptr, "1", "0", nullptr, nullptr}},
    {"focusAll", "enable all target focusing", {nullptr, nullptr, "1", "1", nullptr, nullptr}},
    {"headMove", "enable head movement", {nullptr, nullptr, nullptr, nullptr, "1", nullptr}},
    {"lowHeadMove",
     "enable head movement only while moving",
     {nullptr, nullptr, nullptr, nullptr, "0.5", nullptr}},
    {"noHeadMove", "disable head movement", {nullptr, nullptr, nullptr, nullptr, "0", nullptr}},
    {"heavyHeadMove", "heavy head movement", {nullptr, nullptr, nullptr, nullptr, "2", nullptr}},
};
constexpr int kRowCount = sizeof(kRows) / sizeof(kRows[0]);

int __fastcall Console_ActionCam(const char * /*command*/, const char *args) {
    if (args != nullptr && *args != '\0') {
        for (const Row &row : kRows) {
            if (_stricmp(args, row.name) != 0)
                continue;
            for (int i = 0; i < kCVarCount; ++i) {
                CVar::Factory::Handle cvar = CVar::Factory::Find(kCVars[i]);
                if (cvar != nullptr && row.values[i] != nullptr)
                    CVar::Factory::SetString(cvar, row.values[i]);
            }
            Game::Console::Printf("ACTION Camera set to '%s' - '%s'", row.name,
                                  row.description);
            return 1;
        }
    }
    Game::Console::Printf("========================================");
    Game::Console::Printf("Syntax: ActionCam [option]");
    Game::Console::Printf("Action Camera [option] options:");
    for (int i = kRowCount - 1; i >= 0; --i)
        Game::Console::Printf("%12s - %s", kRows[i].name, kRows[i].description);
    return 1;
}

// Process-global, so the first glue boot covers the session; the registrar
// dedups the repeat on each logout.
void Register() {
    Game::Console::RegisterCommand("ActionCam", &Console_ActionCam,
                                   Game::Console::CATEGORY_GAME, nullptr);
}

const Game::GlueModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Camera::ActionCam
