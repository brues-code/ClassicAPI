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

#include "turtle/Detect.h"

#include "Game.h"

#include <cstring>

namespace Turtle {

namespace {

// Latched once we've read the global as a string — Turtle never un-sets
// it, so a true result is permanent. A false read is NOT cached: FrameXML
// may not have run yet, so we re-probe on each call until it lands.
bool g_detected = false;
char g_version[32] = {0};

// Which Lua state is live right now: the glue screen (true) or the in-world
// FrameScript state (false). The `TURTLE_*` glue markers are set by Turtle's
// GlueXML and are only trustworthy on the glue state — no user addon runs
// there. In-world, ANY addon can define a global by one of those names (a
// Turtle-UI port shipping the strings, say), which must NOT latch Turtle
// detection. So the marker probe is gated on this. Driven by the engine's own
// per-state script-registration hooks below; starts true (the client boots to
// the glue screen).
bool g_onGlue = true;

void Probe() {
    if (g_detected && g_version[0] != '\0')
        return; // fully resolved (Turtle + version known)
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;

    const int top = Game::Lua::GetTop(L);

    // In-world: `TURTLE_WOW_VERSION` (an engine-set global — NOT defined in
    // Turtle's FrameXML Lua) both proves Turtle and gives the version string.
    // RawGet avoids any (nonexistent, but free-to-skip) globals metatable.
    if (g_version[0] == '\0') {
        Game::Lua::PushString(L, "TURTLE_WOW_VERSION");
        Game::Lua::RawGet(L, Game::Lua::GLOBALS_INDEX);
        if (Game::Lua::IsString(L, -1)) {
            const char *v = Game::Lua::ToString(L, -1);
            if (v != nullptr) {
                const size_t n = std::strlen(v);
                const size_t copy =
                    (n < sizeof(g_version) - 1) ? n : sizeof(g_version) - 1;
                std::memcpy(g_version, v, copy);
                g_version[copy] = '\0';
            }
            g_detected = true;
        }
        Game::Lua::SetTop(L, top);
    }

    // Glue screen: `TURTLE_WOW_VERSION` is not on the glue Lua state, but
    // Turtle's GlueXML sets a family of `TURTLE_*` string globals
    // (GlueStrings.lua) at glue boot — before the addon-registration scan
    // runs — so their presence marks a Turtle-lineage client. Clones keep the
    // NAMES (Octo rebrands only the URL values), so checking name presence is
    // clone-robust. Latch detected; the version stays empty until in-world.
    // This is what lets `Addons::FlavorToc` select `_Turtle.toc` consistently
    // at BOTH the glue registration scan and the in-world load pass.
    //
    // Only consult the markers while actually ON the glue state (`g_onGlue`).
    // In-world these names are forgeable by any loaded addon, so trusting them
    // there would let a stock client be mis-detected as Turtle for the session.
    if (!g_detected && g_onGlue) {
        static const char *const kGlueMarkers[] = {
            "AUTH_TURTLE_WEBSITE", "TURTLE_ARMORY", "TURTLE_KNOWLEDGE_DATABASE"};
        for (const char *marker : kGlueMarkers) {
            Game::Lua::PushString(L, marker);
            Game::Lua::RawGet(L, Game::Lua::GLOBALS_INDEX);
            const bool present = Game::Lua::IsString(L, -1);
            Game::Lua::SetTop(L, top);
            if (present) {
                g_detected = true;
                break;
            }
        }
    }
}

// Track the live Lua state via the engine's per-state script-registration
// hooks. The in-world registration (`ModuleAutoRegister`) fires on each world
// load BEFORE the addon load pass, so `g_onGlue` is already false by the time
// any addon's `FlavorToc` calls `Detected()`; the glue registration
// (`GlueModuleAutoRegister`) fires on every glue boot (initial launch + each
// world->glue logout). Neither reads a forgeable Lua global.
void OnInWorldRegister() { g_onGlue = false; }
void OnGlueRegister() { g_onGlue = true; }

const Game::ModuleAutoRegister _inWorldReg{&OnInWorldRegister};
const Game::GlueModuleAutoRegister _glueReg{&OnGlueRegister};

} // namespace

bool Detected() {
    Probe();
    return g_detected;
}

const char *Version() {
    Probe();
    // nullptr until the in-world `TURTLE_WOW_VERSION` fills g_version — a
    // glue-only detection latches Detected() with the version still empty, and
    // the header promises nullptr (not "") in that window.
    return (g_detected && g_version[0] != '\0') ? g_version : nullptr;
}

} // namespace Turtle
