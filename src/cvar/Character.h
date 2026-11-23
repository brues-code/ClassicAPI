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

#include "cvar/Factory.h"

// CVar::Character — cvars whose value belongs to the character, not to the
// install (3.3.5's CVAR_SCOPE_CHARACTER; retail's dump marks them
// "character"). The values live in 1.12's per-character account-data section
// (the character's `config-cache.wtf`), read and written through the engine's
// own section calls, as `SET name "value"` lines — 3.3.5's format for the
// same file. They stay out of Config.wtf.
//
// Every in-game registration pass applies the character's saved value, or
// the default when the file has none, so the next character never inherits
// the last one's. On /reload that happens inside the registration. At login
// the cvar holds its default until the server sync delivers the section, a
// little after VARIABLES_LOADED (as 1.12's chat settings arrive); listeners
// see the change as CVAR_UPDATE. Every accepted change rewrites the file at
// once, so the file always matches the live values.
namespace CVar::Character {

// CVar::Factory::Register for a character cvar; same arguments, same
// callback contract. Call it from a ModuleAutoRegister (in-game) pass: the
// file is only reachable with a character selected.
Factory::Handle Register(const char *name, const char *help, int category,
                         const char *defaultValue,
                         Factory::ChangeCallback callback = nullptr,
                         void *userData = nullptr);

// True for a cvar registered through Register above.
bool Is(Factory::Handle cvar);

} // namespace CVar::Character
