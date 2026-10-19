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

// `pitchLimit <degrees>` — 4.3.4's console command (CGCamera::CCommand_PitchLimit)
// that sets how far the camera may pitch up and down.
//
// 4.3.4 writes `degrees` (and its negation) into the two globals every camera
// pitch clamp reads. 1.12 reads the very same pair, so this writes them and the
// engine's own clamps, smooth-pitch target and cameraPitch range check all
// follow. The only difference is that 1.12 keeps them in .rdata, hence the
// unprotected write.
//
// Like 4.3.4 there is no validation: a bare `pitchLimit` parses as 0 and locks
// the camera level, and values past 90 degrees are taken as given.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

namespace Camera::PitchLimit {

namespace {

using ToFloat_t = float(__stdcall *)(const char *);

constexpr float kDegreesToRadians = 0.017453292f; // 4.3.4's own constant

int __fastcall Console_PitchLimit(const char * /*command*/, const char *args) {
    const float limit =
        reinterpret_cast<ToFloat_t>(Offsets::FUN_SSTR_TO_FLOAT)(args) * kDegreesToRadians;
    const float max = limit;
    const float min = -limit;
    Common::PatchBytes(reinterpret_cast<void *>(Offsets::VAR_CAMERA_MAX_PITCH), &max,
                       sizeof(max));
    Common::PatchBytes(reinterpret_cast<void *>(Offsets::VAR_CAMERA_MIN_PITCH), &min,
                       sizeof(min));
    return 1;
}

// Process-global, so the first glue boot covers the session; the registrar
// dedups the repeat on each logout.
void Register() {
    Game::Console::RegisterCommand("pitchLimit", &Console_PitchLimit,
                                   Game::Console::CATEGORY_GAME, nullptr);
}

const Game::GlueModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Camera::PitchLimit
