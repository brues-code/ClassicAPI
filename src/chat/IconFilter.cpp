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

// Inline-texture (`|T`) chat anti-spoof.
//
// Vanilla neutralizes player-*typed* escapes in chat (a typed `|cff…|r`,
// `|Hitem…|h` or `|T…|t` shows as raw text) — only *trusted* text renders them
// (addon `AddMessage`, shift-clicked links). Our Text::InlineTexture backport
// breaks that parity for `|T` alone: it detects the neutralized `||T` form and
// draws the icon anyway, so a player could `/say` `|Tpath|t` and have a real
// icon appear in everyone's chat AND in the speech bubble over their head.
//
// This restores vanilla's behavior for `|T` by defanging it in the chat path,
// while leaving `|c`/`|r`/`|H`/`|h` exactly as vanilla (already raw for typed
// input). It CANNOT be done in the text emitter: addon `|T` and player `|T`
// reach the emitter as byte-identical `||T` (vanilla doubles the unrecognized
// `|T` for both), so the only place they're distinguishable is the source —
// server-delivered chat flows through the chat dispatcher, addon `print` does
// not. Chat::Dispatch owns that hook and calls this on the message.
//
// Defang = replace the `|` of each `|T` with a space, breaking the pipe→T
// adjacency the inline-texture detector keys on (`|T` and `||T` both contain the
// `|T` substring, so this catches the doubled form too). The path text stays
// readable, the closer `|t` is inert without an opener, and every other escape
// is untouched.

#include "IconFilter.h"

namespace Chat::IconFilter {

namespace {

// True if `s` contains an inline-texture opener (`|T`) anywhere.
bool HasIconEscape(const char *s) {
    for (; *s != '\0'; ++s)
        if (s[0] == '|' && s[1] == 'T')
            return true;
    return false;
}

} // namespace

const char *Sanitize(const char *msg, char *buf, size_t bufSize) {
    if (msg == nullptr || !HasIconEscape(msg))
        return msg; // common case: forward untouched, no copy

    // NEVER write `msg` in place: several of the chat handler's ~50 callers pass
    // read-only `.rdata` literals (system notifications), so an in-place edit
    // would fault. We copy only when a `|T` is actually present.
    //
    // Defanging must not leave a LONE `|` followed by a non-escape char — that's
    // an invalid escape, which the chat frame tolerates but the speech-bubble
    // text setter chokes on, producing an empty bubble (verified in-game). By the
    // time chat reaches us the escape is usually the doubled `||T` (vanilla's own
    // neutralization of typed escapes), where `||` is a VALID literal-pipe escape.
    // So: keep the pipe(s) and blank the `T` when the pipe completes a `||`; blank
    // BOTH chars for a lone `|T`. Either way no dangling `|` remains, and no `|T`
    // opener survives for the inline-texture detector to fire on.
    size_t j = 0;
    for (size_t i = 0; msg[i] != '\0' && j + 1 < bufSize;) {
        if (msg[i] == '|' && msg[i + 1] == 'T') {
            const bool doubledPipe = (i > 0 && msg[i - 1] == '|');
            buf[j++] = doubledPipe ? '|' : ' '; // keep pipe only if it completes `||`
            if (j + 1 < bufSize)
                buf[j++] = ' '; // blank the `T`
            i += 2;             // consumed `|T`
        } else {
            buf[j++] = msg[i++];
        }
    }
    buf[j] = '\0';
    return buf;
}

} // namespace Chat::IconFilter
