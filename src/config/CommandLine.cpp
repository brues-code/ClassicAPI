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

#include "CommandLine.h"

#include "Common.h"
#include "Offsets.h"

#include <windows.h>

namespace Config::CommandLine {

namespace {

// Storm's `_ARGLIST` record.
struct ArgList {
    uint32_t flags;
    uint32_t id;
    const char *name;
    void *callback;
};

constexpr uint32_t SCMD_TYPE_STRING = 0x20000;

constexpr ArgList kSwitches[] = {
    {SCMD_TYPE_STRING, static_cast<uint32_t>(Switch::Config), "config", nullptr},
    {SCMD_TYPE_STRING, static_cast<uint32_t>(Switch::GlueScript), "gluescript", nullptr},
    {SCMD_TYPE_STRING, static_cast<uint32_t>(Switch::GlueScriptFile), "gluescriptFile", nullptr},
    {SCMD_TYPE_STRING, static_cast<uint32_t>(Switch::GameScript), "gamescript", nullptr},
    {SCMD_TYPE_STRING, static_cast<uint32_t>(Switch::GameScriptFile), "gamescriptFile", nullptr},
};

using RegisterArgList_t = int(__stdcall *)(const ArgList *list, uint32_t count);
using GetString_t = int(__stdcall *)(uint32_t id, char *buffer, uint32_t size);

// Replaces the boot call to the client-switch registrar, which also runs the
// parse. Ours go on the list first, then the engine does what it always did.
void __cdecl RegisterThenParse() {
    reinterpret_cast<RegisterArgList_t>(Offsets::FUN_SCMD_REGISTER_ARG_LIST)(
        kSwitches, static_cast<uint32_t>(sizeof kSwitches / sizeof kSwitches[0]));
    reinterpret_cast<void(__cdecl *)()>(Offsets::FUN_REGISTER_CLIENT_SWITCHES)();
}

bool IsSpace(char c) { return c == ' ' || c == '\t'; }

char Lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// Stops at `a`'s terminator rather than reading `len` bytes unconditionally, so
// a switch name at the very end of the command line cannot read past it.
bool EqualsIgnoreCase(const char *a, const char *b, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (a[i] == '\0' || Lower(a[i]) != Lower(b[i]))
            return false;
    }
    return true;
}

size_t Length(const char *s) {
    size_t n = 0;
    while (s[n] != '\0')
        ++n;
    return n;
}

// Start of the value token for `-name`, or null when the switch is absent.
const char *FindValue(const char *name) {
    const char *cmdLine = ::GetCommandLineA();
    if (cmdLine == nullptr || name == nullptr)
        return nullptr;
    const size_t nameLen = Length(name);
    if (nameLen == 0)
        return nullptr;

    bool quoted = false;
    for (const char *p = cmdLine; *p != '\0'; ++p) {
        if (*p == '"') {
            quoted = !quoted;
            continue;
        }
        if (quoted || (*p != '-' && *p != '/'))
            continue;
        // Only at a token boundary, so a path containing the name cannot match.
        // A closing quote is a boundary too: the engine's tokenizer ends a
        // token there.
        if (p != cmdLine && !IsSpace(p[-1]) && p[-1] != '"')
            continue;
        if (!EqualsIgnoreCase(p + 1, name, nameLen))
            continue;

        const char *after = p + 1 + nameLen;
        if (*after == '=' || *after == ':') {
            ++after;
        } else if (IsSpace(*after)) {
            while (IsSpace(*after))
                ++after;
        } else {
            continue; // a longer switch that merely starts with `name`
        }
        return after;
    }
    return nullptr;
}

} // namespace

void Install() {
    Common::RedirectCall(Offsets::CALL_INIT_REGISTER_CLIENT_SWITCHES,
                         Offsets::FUN_REGISTER_CLIENT_SWITCHES, &RegisterThenParse);
}

bool Value(Switch id, char *out, size_t outSize) {
    if (out == nullptr || outSize == 0)
        return false;
    out[0] = '\0';
    reinterpret_cast<GetString_t>(Offsets::FUN_SCMD_GET_STRING)(
        static_cast<uint32_t>(id), out, static_cast<uint32_t>(outSize));
    return out[0] != '\0';
}

bool RawValue(const char *name, std::string &out) {
    out.clear();
    const char *p = FindValue(name);
    if (p == nullptr)
        return false;
    // Honor double quotes so a value containing spaces arrives whole.
    const bool quoted = (*p == '"');
    if (quoted)
        ++p;
    while (*p != '\0' && (quoted ? (*p != '"') : !IsSpace(*p)))
        out.push_back(*p++);
    return !out.empty();
}

} // namespace Config::CommandLine
