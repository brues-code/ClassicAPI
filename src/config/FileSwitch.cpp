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
// writing to disagree. See `Offsets::PATCH_CONFIG_FILENAME_PTR`.

#include "FileSwitch.h"

#include "Offsets.h"

#include <windows.h>

#include <cstddef>

namespace Config::FileSwitch {

namespace {

// The engine holds this pointer for the life of the process and reads through it
// again at exit to save, so the storage has to outlive everything — hence a
// file-static buffer rather than anything owned by a scope.
char g_configName[64];

bool IsSpace(char c) { return c == ' ' || c == '\t'; }

bool EqualsIgnoreCase(const char *a, const char *b, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z')
            ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb)
            return false;
    }
    return true;
}

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

// Copies the token at `p` into `out`, honoring double quotes so a name with a
// space still arrives whole. Returns false when the token is empty or too long.
bool ReadToken(const char *p, char *out, size_t outSize) {
    const bool quoted = (*p == '"');
    if (quoted)
        ++p;

    size_t n = 0;
    while (*p != '\0' && (quoted ? (*p != '"') : !IsSpace(*p))) {
        if (n + 1 >= outSize)
            return false; // longer than we will accept
        out[n++] = *p++;
    }
    out[n] = '\0';
    return n > 0;
}

// Finds `-config <name>` (or `-config=<name>`) on the command line and copies the
// name into `out`. Also accepts a `/` switch prefix, which Windows tools allow.
bool ParseSwitch(char *out, size_t outSize) {
    const char *cmdLine = ::GetCommandLineA();
    if (cmdLine == nullptr)
        return false;

    static const char kSwitch[] = "config";
    const size_t kSwitchLen = sizeof(kSwitch) - 1;

    for (const char *p = cmdLine; *p != '\0'; ++p) {
        if (*p != '-' && *p != '/')
            continue;
        // Only at a token boundary, so a path containing "-config" cannot match.
        if (p != cmdLine && !IsSpace(p[-1]) && p[-1] != '"')
            continue;

        const char *name = p + 1;
        if (!EqualsIgnoreCase(name, kSwitch, kSwitchLen))
            continue;

        const char *after = name + kSwitchLen;
        if (*after == '=') {
            ++after;
        } else if (IsSpace(*after)) {
            while (IsSpace(*after))
                ++after;
        } else {
            continue; // a longer switch that merely starts with "config"
        }
        return ReadToken(after, out, outSize);
    }
    return false;
}

} // namespace

void Apply() {
    if (!ParseSwitch(g_configName, sizeof g_configName))
        return;
    if (!IsPlainFilename(g_configName))
        return;

    void *target = reinterpret_cast<void *>(Offsets::PATCH_CONFIG_FILENAME_PTR);
    DWORD previous = 0;
    if (!::VirtualProtect(target, sizeof(const char *), PAGE_EXECUTE_READWRITE, &previous))
        return;
    *reinterpret_cast<const char **>(target) = g_configName;
    ::VirtualProtect(target, sizeof(const char *), previous, &previous);
}

} // namespace Config::FileSwitch
