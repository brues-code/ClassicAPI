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

#include "localization/Locale.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace Localization::Locale {

// Script_GetLocale (FUN_0048d8b0): VAR_LOCALE_NAME_TABLE[VAR_LOCALE_INDEX].
const char *Client() {
    const uint32_t index = Game::Read<uint32_t>(Offsets::VAR_LOCALE_INDEX);
    return reinterpret_cast<const char *const *>(
        static_cast<uintptr_t>(Offsets::VAR_LOCALE_NAME_TABLE))[index];
}

bool Is(const char *code) { return std::strcmp(Client(), code) == 0; }

} // namespace Localization::Locale
