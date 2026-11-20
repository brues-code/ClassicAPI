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

namespace AddOns::TocRewrite {

// Modern-client TOC directives, backported as the 3.80.2 client does them.
//
// A multi-flavor addon gates lines with conditions and expands path
// variables:
//
//   [Family]\Init.lua                         # -> Classic\Init.lua
//   Vanilla.lua      [AllowLoadGameType vanilla]
//   Mainline.lua     [AllowLoadGameType mainline]   # dropped on this client
//   Localization\[TextLocale].lua             # -> Localization\enUS.lua
//   deDE.lua         [AllowLoadTextLocale deDE]
//   ## SavedVariables: DB_[Game]              # -> DB_Vanilla
//
// Vanilla 1.12's TOC parsers never understood any of this. The scan parser
// (FUN_0051C9B0) reads only `##` metadata; the load pass (FUN_006edb90) hands
// each non-`#` line to the per-file loader verbatim, where the trailing
// ` [...]` breaks the extension check and the file silently never loads.
//
// 3.80.2's line processor (FUN_141bc1020, token table 0x1444fab60, handler
// FUN_141bc09c0) runs on every TOC file line, on every whole `##` line before
// its key is matched (FUN_141af3be0 @0x141af3edc), and on the value of every
// XML `<Include file>` / `<Script file>` (FUN_1405c9dd0). Its rules:
//
//   - Tokens are `[Name]` or `[Name args]`, names matched case-sensitively.
//     A token it does not know, and an unclosed `[`, stay in the line as
//     text. A failed condition drops the whole line; conditions AND. A
//     passed condition is removed. The result is trimmed at both ends.
//   - Lists split on space or comma; their names compare case-insensitively.
//   - [AllowLoadGameType ...] / [ExcludeLoadGameType ...]: the names map
//     through a fixed table (mainline, standard, plunderstorm, classic,
//     vanilla, tbc, wrath, cata, mists, wowhack); unknown names are ignored,
//     and a list with no known name passes either way. Allow passes when the
//     list shares a game type with the client, Exclude when it shares none.
//     This client is `classic` + `vanilla` (3.80.2 is `classic` + `wrath`),
//     plus `turtle` on a Turtle client: a ClassicAPI extension, like the
//     `_Turtle.toc` flavor, that no Blizzard client knows.
//   - [AllowLoadTextLocale ...]: passes when the list holds the client locale.
//   - [AllowLoad x]: one word; `game` and `both` pass, `glue` and anything
//     else fail (addon files and XML load in-game here).
//   - [AllowLoadEnvironment ...]: passes when the list holds `Global` and the
//     line is processed inside an addon load (`inAddonLoad`); fails outside
//     one, as 3.80.2's check does while its load flag (ctx+0xd) is clear.
//     This client has one Lua environment, so `Secure` never matches.
//   - [LoadIntoEnvironment ...]: passes; it only picks 3.80.2's environment.
//   - [Family] -> Classic, [Game] -> Vanilla, [TextLocale] -> the client
//     locale (GetLocale()).
//
// Returns true to keep the line, with `out` holding the processed text.
bool ProcessLine(const char *line, size_t len, bool inAddonLoad, std::string &out);

// The game types a `[AllowLoadGameType]` / `## AllowLoadGameType:` list
// names, as a bit mask (0 when it names none this table knows).
uint32_t GameTypeMask(const char *list, size_t len);

// The game types of this client (classic + vanilla, + turtle on Turtle).
uint32_t ClientGameTypes();

// Called from the FUN_FILE_READ hook after the read succeeds. Does nothing
// unless `path` is an addon `.toc` read (`...\AddOns\...\*.toc`). Rebuilds the
// TOC so the engine's own parsers see the processed lines:
//   - `##` lines at column 0 go through ProcessLine (no addon load: they are
//     read at registration). A dropped one becomes a `# ` comment, so no key
//     is set.
//   - `## Interface:` takes 3.80.2's value (FUN_141af3980): the client
//     version when the list (space or comma separated) holds it, else the
//     largest entry below it, else 0. Vanilla's parser is a plain atoi of the
//     first number, so the line is rewritten to that single value. Loading
//     still needs an exact match (FUN_0051e780).
//   - Other `#` lines and blank lines stay as they are.
//   - File lines go through ProcessLine (inside an addon load). A dropped one
//     becomes a `# ` comment, so the load loop skips it.
// For a base TOC (`...\AddOns\<Name>\<Name>.toc`), the `## AllowLoadGameType:`
// and `## AllowLoad:` values go to AddOns::LoadGate. When the rebuilt TOC
// differs, a fresh Storm buffer replaces `*outBuf` / `*outSize`; the caller
// frees it exactly as it would the engine's own read buffer.
void Transform(const char *path, void **outBuf, size_t *outSize);

} // namespace AddOns::TocRewrite
