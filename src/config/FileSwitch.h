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

// `-config <name>` — makes the client read and write `WTF\<name>` in place of
// `WTF\Config.wtf`, so several settings profiles can live side by side.
namespace Config::FileSwitch {

// Reads the switch from the process command line and, when present, points the
// engine's config filename at it.
//
// MUST be called from DllMain, and is the one thing in this project that is.
// The engine loads the config during boot, several calls before the point where
// VanillaFixes invokes our `Load` export, so by the time the normal
// initialization path runs the file has already been read — see
// `Offsets::PATCH_CONFIG_FILENAME_PTR` for the full call ordering. DllMain is
// early enough because VanillaFixes injects every DLL while the game process is
// still suspended and resumes it only afterwards.
//
// This does not install a hook. It is a single 4-byte store into an immediate
// operand, so it costs none of what makes hooking from DllMain a problem: no
// trampoline, no thread suspension, no loader-lock work, and nothing for another
// DLL's hook to collide with.
void Apply();

} // namespace Config::FileSwitch
