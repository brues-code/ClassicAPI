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
// rewritten copy in `buf`. Only ever applied to text that went through the `|T`
// anti-spoof sanitize, so a player can use the fixed, safe marker icons but
// still can't inject an arbitrary texture path. See RaidMarkers.cpp for the
// token table, the icon→texcoord mapping and the two consumers.
const char *Substitute(const char *msg, char *buf, size_t bufSize);

// Marks a server chat message being dispatched (Chat::Dispatch opens it around
// the engine's dispatch). Only its CHAT_MSG_* event gets the markers; client
// notices and text emotes fire outside one. Restores the prior state.
class EventScope {
public:
    EventScope();
    ~EventScope();
    EventScope(const EventScope &) = delete;
    EventScope &operator=(const EventScope &) = delete;

private:
    bool prev_;
};

// The CHAT_MSG_* event text: Substitute inside an EventScope, else `text`.
// Called from Chat::EventArgs' fire site.
const char *ForChatEvent(const char *text, char *buf, size_t bufSize);

} // namespace Chat::RaidMarkers
