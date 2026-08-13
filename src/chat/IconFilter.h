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

namespace Chat::IconFilter {

// Strips player-injected inline-texture (`|T…|t`) escapes from a chat message.
// Returns `msg` unchanged when it contains no `|T` (the common case — no copy),
// otherwise a sanitized copy written into `buf`. Called by Chat::Dispatch on the
// message the engine's chat handler is about to display. See IconFilter.cpp for
// the anti-spoof rationale and why sanitizing here (not in the text emitter) is
// the only place player-typed and addon `|T` are distinguishable.
const char *Sanitize(const char *msg, char *buf, size_t bufSize);

} // namespace Chat::IconFilter
