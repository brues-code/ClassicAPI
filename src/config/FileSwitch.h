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

// Redirects the boot call that initializes the config, so it receives the
// `-config` value instead of "Config.wtf".
//
// MUST be called from DllMain. The engine loads the config during boot,
// several calls before the point where VanillaFixes invokes our `Load` export
// — see `Offsets::CALL_INIT_CONFIG_INITIALIZE`. DllMain is early enough because
// VanillaFixes injects every DLL while the game process is still suspended and
// resumes it only afterwards.
//
// This is a single rel32 store, so it costs none of what makes hooking from
// DllMain a problem: no trampoline, no thread suspension, and no loader-lock work.
void Install();

} // namespace Config::FileSwitch
