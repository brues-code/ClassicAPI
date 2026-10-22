// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// `-config <name>` — a launch switch that swaps which settings file the client
// uses, so one install can hold several profiles.
//
// The engine already names the config file through a single pointer: boot passes
// `"Config.wtf"` to the config initializer, which keeps it and uses it to both
// read the file at startup and write it at exit. Supplying a different string is
// therefore the whole feature — no second code path, and no way for reading and
// writing to disagree. It is 4.3.4's `InitializeGlobal`, which passes
// `CmdLineGetString(CMD_CONFIG)` there when the switch is given. See
// `Offsets::CALL_INIT_CONFIG_INITIALIZE`.

#include "FileSwitch.h"

#include "CommandLine.h"
#include "Common.h"
#include "Offsets.h"

namespace Config::FileSwitch {

namespace {

using ConfigInitialize_t = void(__fastcall *)(const char *filename);

// The engine holds this pointer for the life of the process and reads through it
// again at exit to save, so the storage has to outlive everything — hence a
// file-static buffer rather than anything owned by a scope. 260 is 4.3.4's
// `CmdLineGetString` buffer.
char g_configName[260];

// A filename is required to be a plain name. The engine builds the save path as
// `WTF\` + this, and a subdirectory that does not exist makes the save fail
// silently, which would throw away a session's settings with nothing to show for
// it. Refusing the switch is the loud failure that a lost config file is not.
bool IsPlainFilename(const char *name) {
    if (*name == '\0')
        return false;
    for (const char *p = name; *p != '\0'; ++p) {
        if (*p == '\\' || *p == '/' || *p == ':')
            return false;
    }
    return true;
}

// Replaces the boot call. Runs after the command line is parsed, so the switch
// value is read through the engine.
void __fastcall InitializeConfig(const char *defaultName) {
    const char *name = defaultName;
    if (Config::CommandLine::Value(Config::CommandLine::Switch::Config, g_configName,
                                   sizeof g_configName) &&
        IsPlainFilename(g_configName))
        name = g_configName;
    reinterpret_cast<ConfigInitialize_t>(Offsets::FUN_CVAR_CONFIG_INITIALIZE)(name);
}

} // namespace

void Install() {
    Common::RedirectCall(Offsets::CALL_INIT_CONFIG_INITIALIZE,
                         Offsets::FUN_CVAR_CONFIG_INITIALIZE, &InitializeConfig);
}

} // namespace Config::FileSwitch
