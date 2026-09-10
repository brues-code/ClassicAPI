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

// Owns the hook on `FUN_MACRO_PARSE_PRIMARY_SPELL` and dispatches to
// feature-module-registered `(prefix, extractor)` pairs. The engine's
// parser only recognizes `/cast SpellName` and `CastSpellByName(...)`
// — addons that define new cast wrappers (`CastSpellNoToggle`,
// `C_Spell.CastAtCursor`, etc.) need their macro slots tagged with
// the resolved spellID too, otherwise action-bar UIs lose
// auto-repeat highlight and cooldown overlay.
//
// Each feature module owns an extractor that knows how to read the
// arg shape for its specific function (quoted string, numeric
// literal, etc.). Patterns auto-register at static-init via
// `PatternAutoRegister`. The hook walks each macro line ONCE,
// checking every registered prefix at every position; on a prefix
// match it calls the extractor to resolve a spellID.
//
// Long-line overflow guard: the vanilla parser has a 256-byte
// per-line stack buffer that overflows on `/run` lines longer than
// that. We pre-check and skip the original entirely when any line
// exceeds the safe threshold — the original's tagging is lost in
// that case (no `/cast` on a 240+ byte line anyway) but our
// patterns still run since they bound-check their own extraction.

#include "MacroPrimarySpell.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace Spell::MacroPrimarySpell {

namespace {

PatternAutoRegister *g_patternHead = nullptr;
PostParseAutoRegister *g_postParseHead = nullptr;

using MacroParse_t = void(__fastcall *)(int macroEntry);
MacroParse_t MacroParse_o = nullptr;

void NotifyParsed(int macroEntry) {
    for (auto *node = g_postParseHead; node != nullptr; node = node->next)
        node->cb(macroEntry);
}

// Vanilla's parser uses a 256-byte stack buffer per line; bytes past
// the buffer clobber the saved entry pointer at `[EBP-4]` and the
// return address. 240 leaves a safety margin.
constexpr int kMaxSafeLineBytes = 240;

bool BodyHasLongLine(const char *body) {
    if (body == nullptr)
        return false;
    int lineLen = 0;
    for (; *body != '\0'; ++body) {
        if (*body == '\n') {
            lineLen = 0;
        } else if (++lineLen > kMaxSafeLineBytes) {
            return true;
        }
    }
    return false;
}

// Walks each line once. At every byte position, checks every
// registered prefix; on first match, invokes the extractor. First
// extractor that returns a positive spellID wins for the whole macro
// — matches the engine parser's "first hit, line-at-a-time" walk.
int ScanBody(const char *body) {
    if (body == nullptr)
        return 0;
    const char *line = body;
    while (*line != '\0') {
        const char *eol = line;
        while (*eol != '\0' && *eol != '\n')
            ++eol;

        for (const char *p = line; p < eol; ++p) {
            for (auto *node = g_patternHead; node != nullptr; node = node->next) {
                if (p + node->prefixLen > eol)
                    continue;
                if (std::memcmp(p, node->prefix, node->prefixLen) != 0)
                    continue;
                const int spellID = node->extract(p + node->prefixLen, eol);
                if (spellID > 0)
                    return spellID;
                // Prefix matched but extraction failed (malformed
                // args). Don't keep checking other prefixes at this
                // position — they couldn't possibly also match here.
                break;
            }
        }

        line = (*eol == '\n') ? eol + 1 : eol;
    }
    return 0;
}

void __fastcall MacroParse_h(int macroEntry) {
    const char *body = reinterpret_cast<const char *>(
        macroEntry + Offsets::OFF_MACRO_BODY);
    int *cacheField = reinterpret_cast<int *>(
        macroEntry + Offsets::OFF_MACRO_PRIMARY_SPELL);

    if (BodyHasLongLine(body)) {
        *cacheField = 0;
        const int spellID = ScanBody(body);
        if (spellID > 0)
            *cacheField = spellID;
        NotifyParsed(macroEntry);
        return;
    }

    MacroParse_o(macroEntry);

    if (*cacheField == 0) {
        const int spellID = ScanBody(body);
        if (spellID > 0)
            *cacheField = spellID;
    }
    NotifyParsed(macroEntry);
}

const Game::HookAutoRegister _hookreg{
    Offsets::FUN_MACRO_PARSE_PRIMARY_SPELL,
    reinterpret_cast<void *>(&MacroParse_h),
    reinterpret_cast<void **>(&MacroParse_o)};

} // namespace

PatternAutoRegister::PatternAutoRegister(const char *p, ArgExtractor e)
    : prefix(p),
      prefixLen(static_cast<unsigned>(std::strlen(p))),
      extract(e),
      next(g_patternHead) {
    g_patternHead = this;
}

PostParseAutoRegister::PostParseAutoRegister(PostParseCallback c)
    : cb(c),
      next(g_postParseHead) {
    g_postParseHead = this;
}

} // namespace Spell::MacroPrimarySpell
