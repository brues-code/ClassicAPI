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

// Lua from the launch command line:
//
//   -gluescript      <code>   run at the glue screens
//   -gluescriptFile  <path>   the file's contents, run at the glue screens
//   -gamescript      <code>   run once in-game
//   -gamescriptFile  <path>   the file's contents, run once in-game
//
// None of these exist in 1.12: the strings do not appear anywhere in the
// binary, so there is nothing to parse them and nothing to un-gate. The whole
// feature is therefore ours -- read the command line, and hand the source to
// the engine's own compile-and-run funnel at the right moment.
//
// WHEN. Registration time is too early for either state. The in-game module
// callback runs from FUN_LOAD_SCRIPT_FUNCTIONS, before FrameXML is loaded, and
// the glue callback runs from FUN_LOAD_GLUE_SCRIPT_FUNCTIONS, before GlueXML
// is. A script that ran there could not touch a frame, call a FrameXML
// function, or see an addon, which is most of what anyone would write one for.
// So each script is armed at registration and run from a tick, where the UI is
// demonstrably up:
//
//   glue -> Tick::FrameTick, whose hook is the UI render root and so fires at
//           the login and character-select screens. Armed by the glue module
//           callback, which runs once per glue boot -- initial launch and every
//           return from the world on logout -- so the glue script runs each
//           time the glue screens come up, matching what "run at the glue
//           screens" says. A script that logs in automatically therefore keeps
//           working after a logout, which is the point of it.
//   game -> Tick::WorldTick, which fires only in the world, so its first tick
//           is past FrameXML, past the addon load pass, and past enter-world
//           init. Latched for the life of the process, never re-armed on
//           /reload or on a second login, because "run once in-game" says once.
//
// HOW. FUN_LUA_RUN_STRING resolves the Lua state itself, so one call reaches
// whichever state is current and the two paths differ only in when they fire.
// It also installs the FrameScript error handler as the pcall's errfunc, so a
// faulty script reports itself the way any script error does instead of failing
// silently. Because every chunk goes through the shared compile chokepoint,
// these scripts get the same 5.1 syntax support the rest of the client's Lua
// has.
//
// A `*File` is read with Win32 rather than through the engine's file layer, on
// purpose. The engine resolves relative paths against a loose-file index built
// once at boot, so it cannot see a file created afterwards and cannot open an
// absolute path at all -- and an absolute path is exactly what someone puts in
// a shortcut. Reading it ourselves also means both switches share one execution
// path, so inline code and file contents cannot diverge in behaviour.

#include "CommandLine.h"

#include "Offsets.h"
#include "debug/Log.h"
#include "Game.h"
#include "tick/FrameTick.h"
#include "tick/WorldTick.h"

#include <windows.h>

#include <cstdint>
#include <string>

namespace Config::LaunchScript {

namespace {

using RunString_t = int(__fastcall *)(const char *source, uint32_t size, const char *chunkName,
                                      void *errCtx);

// A chunk name carries a leading `=` or `@` to tell Lua how to present it in an
// error. That marker is for Lua, not for a reader, so the log drops it.
const char *Displayed(const std::string &chunkName) {
    const char *s = chunkName.c_str();
    return (*s == '=' || *s == '@') ? s + 1 : s;
}

void Run(const std::string &source, const std::string &chunkName) {
    if (source.empty())
        return;
    const int ok = reinterpret_cast<RunString_t>(Offsets::FUN_LUA_RUN_STRING)(
        source.c_str(), static_cast<uint32_t>(source.size()), chunkName.c_str(), nullptr);
    // The engine's error handler has already reported the detail to the user by
    // now; this only records that the switch was the source of it.
    if (ok == 0)
        Debug::Log::Printf("[launchscript] %s did not run to completion", Displayed(chunkName));
}

// Whole-file read. Refuses a file larger than the compiler's own size parameter
// can describe, which is the engine's bound rather than one of ours.
bool ReadWholeFile(const char *path, std::string &out) {
    out.clear();
    const HANDLE file = ::CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    bool ok = false;
    LARGE_INTEGER size{};
    if (::GetFileSizeEx(file, &size) != 0 && size.QuadPart >= 0 &&
        size.QuadPart <= static_cast<long long>(0xFFFFFFFFu)) {
        if (size.QuadPart == 0) {
            ok = true; // an empty file is not a failure, just nothing to run
        } else {
            out.assign(static_cast<size_t>(size.QuadPart), '\0');
            DWORD read = 0;
            ok = ::ReadFile(file, &out[0], static_cast<DWORD>(out.size()), &read, nullptr) != 0 &&
                 read == out.size();
        }
    }
    ::CloseHandle(file);
    if (!ok)
        out.clear();
    return ok;
}

// Runs the inline switch and then the file switch, so a launch that passes both
// gets both, in the order they are written above.
void RunPair(const char *codeSwitch, const char *fileSwitch) {
    std::string code;
    if (Config::CommandLine::Value(codeSwitch, code))
        Run(code, std::string("=-") + codeSwitch); // `=` shows the switch, not the source

    std::string path;
    if (!Config::CommandLine::Value(fileSwitch, path))
        return;
    std::string contents;
    if (!ReadWholeFile(path.c_str(), contents)) {
        Debug::Log::Printf("[launchscript] -%s could not read '%s'", fileSwitch, path.c_str());
        return;
    }
    Run(contents, "@" + path); // `@` shows the path, so errors carry line numbers
}

bool g_glueArmed = false;
bool g_gameRan = false;

void OnFrameTick() {
    if (!g_glueArmed)
        return;
    g_glueArmed = false;
    RunPair("gluescript", "gluescriptFile");
}

void OnWorldTick() {
    if (g_gameRan)
        return;
    g_gameRan = true;
    RunPair("gamescript", "gamescriptFile");
}

void ArmGlue() { g_glueArmed = true; }

const Game::GlueModuleAutoRegister _glueAutoreg{&ArmGlue};
const Tick::FrameTick::AutoSubscribe _frameTick{&OnFrameTick};
const Tick::WorldTick::AutoSubscribe _worldTick{&OnWorldTick};

} // namespace

} // namespace Config::LaunchScript
