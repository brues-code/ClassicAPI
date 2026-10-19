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

// Inline-texture (`|T`, `|A`) chat anti-spoof.
//
// A player-typed escape never renders: the edit box stores a typed `|` as `||`,
// the escaped pipe, which every text path draws as a literal `|`. That alone
// keeps a typed `|Tpath|t` literal. The send-side escape check
// (SendChatMessage) also rejects a `|` followed by anything but `|` or `c` —
// but it skips from a `|c` to the next `|r` unchecked, so a modified client
// or an addon can still smuggle a real `|T` inside a colour run, and it would
// render as art in everyone's chat and in the bubble over the sender.
//
// So server-delivered chat is checked once at the source — Chat::Dispatch
// owns the dispatch hook and calls this on the message — and each real opener
// gets the engine's own neutralization: its `|` is doubled, the bytes a typed
// one has, so it shows as the literal text the sender typed. `||` pairs are
// already literal and are left alone, as is every other escape.
//
// ANY new marker the emitter learns MUST be added here in the same change, or it
// becomes a chat-spoof vector.

#include "IconFilter.h"

namespace Chat::IconFilter {

namespace {

// A real inline-texture opener at `s`: `|T` names a texture path and `|A` an
// atlas. Callers step over `||` pairs, so `s` is always at a token start.
bool IsIconOpener(const char *s) { return s[0] == '|' && (s[1] == 'T' || s[1] == 'A'); }

bool HasIconEscape(const char *s) {
    for (; *s != '\0'; ++s) {
        if (s[0] == '|' && s[1] == '|') {
            ++s; // escaped pipe
            continue;
        }
        if (IsIconOpener(s))
            return true;
    }
    return false;
}

} // namespace

const char *Sanitize(const char *msg, char *buf, size_t bufSize) {
    if (msg == nullptr || !HasIconEscape(msg))
        return msg; // common case: forward untouched, no copy

    // NEVER write `msg` in place: several of the chat handler's ~50 callers pass
    // read-only `.rdata` literals (system notifications), so an in-place edit
    // would fault. We copy only when an opener is actually present.
    //
    // Never leave a lone `|` before a non-escape character: the chat frame
    // tolerates it, but the speech-bubble builder ends the text there (it drops
    // a `|` with everything up to the next `[`). A doubled `||` is the valid
    // literal-pipe escape; when the buffer has no room for the extra byte, the
    // opener is blanked instead.
    size_t j = 0;
    for (size_t i = 0; msg[i] != '\0' && j + 1 < bufSize;) {
        if (msg[i] == '|' && msg[i + 1] == '|') {
            buf[j++] = msg[i++];
            if (j + 1 < bufSize)
                buf[j++] = msg[i++];
            continue;
        }
        if (IsIconOpener(msg + i)) {
            if (j + 3 < bufSize) {
                buf[j++] = '|';
                buf[j++] = '|';
                buf[j++] = msg[i + 1];
            } else {
                buf[j++] = ' ';
                if (j + 1 < bufSize)
                    buf[j++] = ' ';
            }
            i += 2;
            continue;
        }
        buf[j++] = msg[i++];
    }
    buf[j] = '\0';
    return buf;
}

} // namespace Chat::IconFilter
