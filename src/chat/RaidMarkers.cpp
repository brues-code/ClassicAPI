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

// Raid-target marker chat substitutions ({rt1}..{rt8} / {star} / {skull} / ...).
//
// Modern WoW lets players type `{skull}` (etc.) in chat and everyone sees the
// raid-target icon. Vanilla 1.12 has no such substitution. We backport it onto
// the inline-texture (`|T…|t`) renderer: each recognized token is rewritten into
// a `|T` escape that crops the shared raid-target sprite sheet
// `Interface\TargetingFrame\UI-RaidTargetingIcons` to the matching 64×64 cell.
//
// Runs in Chat::Dispatch AFTER Chat::IconFilter::Sanitize. Order is deliberate:
// sanitize first defangs any player-typed raw `|T` spoof, then we add ONLY the
// fixed, known-safe marker escapes — so players get the markers without a path to
// inject arbitrary textures.
//
// Icon→texcoord mapping verified against the client's own UnitPopup.lua
// (RAID_TARGET_1..8): the sheet is 256×256 with 64-pixel cells, marker N at
// column (N-1)%4, row (N-1)/4. So RT1(star) is the top-left cell and RT8(skull)
// is row 1, column 3. The inline renderer's OpenGL v-flip already draws this sheet
// right-side-up (it's the sheet that flip was verified against), so top<bottom
// pixel coords render correctly.

#include "RaidMarkers.h"

#include <cstdio>
#include <cstring>

namespace Chat::RaidMarkers {

namespace {

// On-screen pixel size of an inline marker. The default chat font is ~14px, so 14
// keeps a marker about one line tall. Fixed (the substitution is a static string
// rewrite and can't see the target font); tunable here if it reads too small/large.
constexpr int kMarkerSizePx = 14;

struct Token {
    const char *name; // lowercase alias, no braces
    int marker;       // raid-target index 1..8
};

// Aliases per the modern client, all case-insensitive. `{coin}` is a colloquial
// alias for `{circle}` (RT2) and `{x}` for `{cross}` (RT7) — both shown in the
// in-game substitution help.
constexpr Token kTokens[] = {
    {"rt1", 1}, {"star", 1},
    {"rt2", 2}, {"circle", 2}, {"coin", 2},
    {"rt3", 3}, {"diamond", 3},
    {"rt4", 4}, {"triangle", 4},
    {"rt5", 5}, {"moon", 5},
    {"rt6", 6}, {"square", 6},
    {"rt7", 7}, {"cross", 7}, {"x", 7},
    {"rt8", 8}, {"skull", 8},
};

char LowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

// Case-insensitive match of the n-char token body [s, s+n) against a lowercase
// literal — true only when `lit` is exactly n chars long.
bool EqCI(const char *s, size_t n, const char *lit) {
    for (size_t i = 0; i < n; ++i)
        if (lit[i] == '\0' || LowerAscii(s[i]) != lit[i])
            return false;
    return lit[n] == '\0';
}

// Raid-target index (1..8) for the token body [name, name+n), or 0 if unknown.
int MatchMarker(const char *name, size_t n) {
    for (const Token &t : kTokens)
        if (EqCI(name, n, t.name))
            return t.marker;
    return 0;
}

bool HasBrace(const char *s) {
    for (; *s != '\0'; ++s)
        if (*s == '{')
            return true;
    return false;
}

} // namespace

const char *Substitute(const char *msg, char *buf, size_t bufSize) {
    if (msg == nullptr || !HasBrace(msg))
        return msg; // common case: forward untouched, no copy

    // A token inflates ~28x (a 3-char `{rtN}` -> ~85-byte `|T` markup), so a
    // marker-dense message can exceed `buf`. Never DROP message content to fit:
    // only expand a token when the expansion AND the whole remaining input (worst
    // case copied 1:1 as literal text) still fit. Past that point later tokens
    // stay literal `{rtN}`, so the full message text is always preserved — a DLL
    // user never sees a shorter line than a non-DLL user, just fewer icons.
    const size_t msgLen = std::strlen(msg);

    size_t j = 0;
    for (size_t i = 0; msg[i] != '\0' && j + 1 < bufSize;) {
        if (msg[i] == '{') {
            // Scan the token body to the closing brace (bounded — longest alias
            // is 8 chars, so a `{` with no nearby `}` is just literal text).
            size_t k = i + 1;
            while (msg[k] != '\0' && msg[k] != '}' && k - i <= 12)
                ++k;
            if (msg[k] == '}') {
                const int marker = MatchMarker(msg + i + 1, k - (i + 1));
                if (marker != 0) {
                    const int col = (marker - 1) % 4;
                    const int row = (marker - 1) / 4;
                    const int l = col * 64, r = l + 64, t = row * 64, b = t + 64;
                    // Emit the pipe-DOUBLED form `||T…||t`. The inline emitter
                    // renders both `|T` and `||T`, but the vanilla speech-bubble
                    // text builder (FUN_004b1600) TRUNCATES at a lone `|T` while
                    // PRESERVING `||` — so the doubled form is what survives into
                    // the bubble's FontString (which pfUI's bubble reskin copies
                    // and renders). Anti-spoof Sanitize runs before this, so these
                    // are the only `||T` openers a player can produce.
                    char markup[128];
                    const int m = std::snprintf(
                        markup, sizeof markup,
                        "||TInterface\\TargetingFrame\\UI-RaidTargetingIcons:%d:%d:0:0:256:256:"
                        "%d:%d:%d:%d||t",
                        kMarkerSizePx, kMarkerSizePx, l, r, t, b);
                    // Reserve room for the literal tail after this token
                    // (msg[k+1..]) so the rest of the message always fits.
                    const size_t tail = msgLen - (k + 1);
                    if (m > 0 && j + static_cast<size_t>(m) + tail + 1 <= bufSize) {
                        std::memcpy(buf + j, markup, static_cast<size_t>(m));
                        j += static_cast<size_t>(m);
                        i = k + 1; // consume through the closing `}`
                        continue;
                    }
                    // No room for the expansion — fall through and copy the
                    // literal `{` so the message is never left truncated mid-token.
                }
            }
        }
        buf[j++] = msg[i++];
    }
    buf[j] = '\0';
    return buf;
}

} // namespace Chat::RaidMarkers
