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

#include "FullName.h"

#include "Game.h"
#include "settings/Paths.h"

#include <cstring>

namespace Player::FullName {

namespace {

// The realm as the engine writes it into a full name: spaces and tabs removed.
std::string Squeeze(const char *realm) {
    std::string out;
    for (const char *p = realm; p != nullptr && *p != '\0'; ++p)
        if (*p != ' ' && *p != '\t')
            out.push_back(*p);
    return out;
}

} // namespace

std::string Make(const char *name, const char *realm) {
    if (name == nullptr || *name == '\0')
        return {};
    std::string out = name;
    const std::string squeezed = Squeeze(realm);
    if (squeezed.empty() ||
        Game::StrCmpI(squeezed.c_str(), Squeeze(Settings::Paths::RealmName().c_str()).c_str()) == 0)
        return out;
    out += '-';
    out += squeezed;
    return out;
}

std::string Parse(const char *text) {
    if (text == nullptr)
        return {};
    const char *dash = std::strchr(text, '-');
    if (dash == nullptr)
        return Make(text, nullptr);
    return Make(std::string(text, dash).c_str(), dash + 1);
}

} // namespace Player::FullName
