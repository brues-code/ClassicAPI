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

// The `|4singular:plural;` plural escape.
//
// 4.3.4's grammar pass (LanguageProcess, CSimpleRender.cpp) finds the first
// `|<digit>` code in the text, runs that rule over the whole text, and repeats
// until no code is left. Its rules are |1 Korean particles, |2 French
// articles, |3 Russian declension and |4 plurals. 1.12's pass
// (FUN_EXPAND_GRAMMAR_ESCAPES) is the same loop with rules 1 and 2 only, so
// `|4` reaches the screen as written. The replacement below is 4.3.4's loop:
// rules 1 and 2 stay 1.12's own functions, rule 4 is 4.3.4's
// LanguageRule4 with its locale plural index (FrameScript_GetPluralIndex,
// chosen by locale as Client.cpp does). Rule 3 needs the server's declension
// data and a ruRU client, so `|3` stays literal as before.
//
// The pass has 1.12's callers unchanged: SetText with expand (Lua SetText,
// button text, tooltip lines), chat lines, chat bubbles and name plates.
// It runs in rule order by position, so `|1` after a `|4` sees the chosen
// plural, as in 4.3.4.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace Text::Grammar {

namespace {

using Rule_t = char *(__fastcall *)(const char *text);

// 4.3.4's s_languageBuffer.
constexpr size_t kBufferSize = 0x2000;
char g_buffer[kBufferSize];

// FrameScript_GetPluralIndex. The 1.12 locale index has 4.3.4's order for
// 0..7 (enUS, koKR, frFR, deDE, zhCN, zhTW, esES, esMX); 8 is ruRU there.
uint32_t PluralIndex(int32_t count) {
    switch (Game::Read<int>(Offsets::VAR_LOCALE_INDEX)) {
    case 1:
    case 2:
    case 4:
    case 5:
        return count > 1;
    case 8:
        if (count % 100 >= 11 && count % 100 <= 14)
            return 2;
        if (count % 10 == 1)
            return 0;
        if (count % 10 >= 2 && count % 10 <= 4)
            return 1;
        return 2;
    default:
        return count != 1;
    }
}

bool IsDigit(char ch) { return ch >= '0' && ch <= '9'; }

// LanguageRule4: each `|4a:b[:c];` becomes the form the plural index picks
// for the last number before it. It writes into g_buffer, in place when the
// text is already there: the output never runs ahead of the input.
char *ExpandPlurals(const char *text) {
    bool wasDigit = false;
    int32_t number = 0;
    const char *src = text;
    char *dest = g_buffer;

    while (*src) {
        const char ch = *src;
        const bool isDigit = IsDigit(ch);
        if (isDigit)
            number = wasDigit ? number * 10 + ch - '0' : ch - '0';

        if (ch == '|') {
            src++;
            if (*src == '4' && !IsDigit(src[1])) {
                src++;
                const uint32_t index = PluralIndex(number);
                for (uint32_t i = 0; i < index && *src; i++) {
                    while (*src != ':' && *src != ';') {
                        src++;
                        if (!*src)
                            break;
                    }
                    if (!*src || *src == ';')
                        break;
                    src++;
                }
                while (*src == ' ')
                    src++;
                if (!*src)
                    break;
                while (*src != ':' && *src != ';') {
                    *dest++ = *src++;
                    if (!*src) {
                        *dest = 0;
                        return g_buffer;
                    }
                }
                while (*src != ';') {
                    src++;
                    if (!*src) {
                        *dest = 0;
                        return g_buffer;
                    }
                }
            } else {
                *dest++ = '|';
                // 4.3.4 copies the byte after a trailing `|` (the NUL) and
                // steps past it; stop there instead.
                if (!*src)
                    break;
                *dest++ = *src;
            }
        } else {
            *dest++ = ch;
        }

        src++;
        wasDigit = isDigit;
    }

    *dest = 0;
    return g_buffer;
}

// 4.3.4's dispatch: the first `|` + digit code that names a rule. 4.3.4 and
// 1.12 read the number with SStrToInt, but each rule consumes only its digit
// followed by a non-digit, so `|01` names rule 1, is never consumed, and the
// loop never ends. The code here is the digit alone, which is exactly what
// the rules consume. The scan is by byte; `|` never occurs inside a UTF-8
// sequence, so it finds what 4.3.4's sgetu8 walk finds.
int NextRule(const char *text) {
    for (const char *p = text; *p; ++p) {
        if (*p != '|')
            continue;
        ++p;
        if (!*p)
            return 0;
        if ((*p == '1' || *p == '2' || *p == '4') && !IsDigit(p[1]))
            return *p - '0';
    }
    return 0;
}

char *__fastcall ExpandGrammarEscapes_h(const char *text) {
    if (std::strlen(text) >= kBufferSize) {
        std::memcpy(g_buffer, text, kBufferSize);
        g_buffer[kBufferSize - 3] = 0;
        g_buffer[kBufferSize - 2] = 0;
        g_buffer[kBufferSize - 1] = 0;
        text = g_buffer;
    }
    for (;;) {
        switch (NextRule(text)) {
        case 1:
            text = reinterpret_cast<Rule_t>(Offsets::FUN_EXPAND_KOREAN_PARTICLES)(text);
            break;
        case 2:
            text = reinterpret_cast<Rule_t>(Offsets::FUN_EXPAND_FRENCH_ARTICLES)(text);
            break;
        case 4:
            text = ExpandPlurals(text);
            break;
        default:
            return const_cast<char *>(text);
        }
    }
}

// A full replacement: the original is never called.
const Game::HookAutoRegister _hook{Offsets::FUN_EXPAND_GRAMMAR_ESCAPES,
                                   reinterpret_cast<void *>(&ExpandGrammarEscapes_h), nullptr};

} // namespace

} // namespace Text::Grammar
