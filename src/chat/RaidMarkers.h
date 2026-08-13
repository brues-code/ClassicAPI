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

namespace Chat::RaidMarkers {

// Expands raid-target marker tokens in a chat message into inline raid-target
// icon textures — the modern `{rt1}`..`{rt8}` / `{star}` / `{circle}` / `{skull}`
// (etc.) chat substitutions, backported onto the inline-texture renderer. Returns
// `msg` unchanged when it contains no `{` (the common case — no copy), otherwise a
// rewritten copy in `buf`. Called by Chat::Dispatch AFTER the `|T` anti-spoof
// sanitize, so a player can use the fixed, safe marker icons but still can't
// inject an arbitrary texture path. See RaidMarkers.cpp for the token table and
// the icon→texcoord mapping.
const char *Substitute(const char *msg, char *buf, size_t bufSize);

} // namespace Chat::RaidMarkers
