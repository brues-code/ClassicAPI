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

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Our launch switches, as entries in the engine's own switch registry (Storm
// SCmd), the way 4.3.4 adds `-config` to its `s_wowArgList`.
//
// Registering is not optional. The engine's parser stops at the first switch
// it does not know, so an unregistered `-config x` would make the engine ignore
// every switch written after it (`-console`, `-windowed`, ...). Once registered,
// the engine parses our switches with the same tokenizer as its own, and the
// values are read back through SCmd. See `Offsets::CALL_INIT_REGISTER_CLIENT_SWITCHES`.

namespace Config::CommandLine {

enum class Switch : uint32_t {
    // Past the engine's own ids (0..38).
    Config = 0x100,
    GlueScript,
    GlueScriptFile,
    GameScript,
    GameScriptFile,
};

// Redirects the boot call that registers and parses the switches, so ours are
// registered first. MUST be called from DllMain: boot parses the command line
// before VanillaFixes calls our `Load` export.
void Install();

// The value of a string switch, through the engine. False when the switch is
// absent or empty. Values are at most 255 bytes (the engine's token buffer).
bool Value(Switch id, char *out, size_t outSize);

// The value of `-name`, read from the raw command line with no length bound.
// Only for inline Lua code (`-gluescript`, `-gamescript`), which can be longer
// than the engine keeps. It honors the same forms the engine accepts
// (`-name value`, `-name=value`, `-name:value`, `/name`) and skips quoted text,
// so a switch name inside a quoted script is not a match.
bool RawValue(const char *name, std::string &out);

} // namespace Config::CommandLine
