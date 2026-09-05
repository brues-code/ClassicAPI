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

#include <string>

// `Settings::Paths` — the one place ClassicAPI builds a path to its own data
// files under `WTF\Account\`.
//
// Three modules used to resolve these independently, each re-reading the same
// account / realm / character globals and re-joining the same separators. The
// engine's own `WTF\Account\%s` format strings are deliberately NOT used: they
// belong to the engine's saved-variable layout, and a module mirroring THAT
// (see `AddOns::SavedVarsFirst`) has to match it byte for byte and so keeps its
// own copies. These functions describe ClassicAPI's files, which is a separate
// concern that happens to share a directory.
//
// Every function returns an empty string until the session globals it needs are
// populated, so a caller can treat "empty" as "not available yet" and never has
// to null-check the individual names.
namespace Settings::Paths {

// The logged-in account name, upper-cased by the engine. Empty before login.
std::string AccountName();

// `WTF\Account\<account>\<leaf>` — for data shared by every character on the
// account. Empty until the account is known.
std::string AccountFile(const char *leaf);

// `WTF\Account\<account>\<realm>\<character>\<leaf>` — for per-character data.
// Empty until all three are known, which is only true in-world: a
// logout to character select clears the character, so callers that re-resolve
// on each access pick up a new character without a reload.
std::string CharacterFile(const char *leaf);

} // namespace Settings::Paths
