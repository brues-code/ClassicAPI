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

// String helpers that 1.12's Lua 5.0 is missing:
//
//   - `string.match(s, pattern [, init])` — first-match extraction (5.1),
//     also the global `strmatch`, as `string.find` is `strfind`.
//   - `string.gmatch(s, pattern)`         — match iterator (5.1).
//   - `string.gsub` TABLE replacement     — 5.1 form (see the shim below).
//   - `strsplit(sep, str [, pieces])`     — WoW global; split on any char.
//   - `strjoin(delimiter, ...)`           — WoW global; join varargs.
//   - `strtrim(str [, chars])`            — WoW global; trim a char set.
//   - `strreplace(str, find, replace)`    — plain-text replace (see below).
//   - `string.reverse(s)` / `strrev(s)`   — 5.1 string function, missing in 5.0.
//
// The two pattern `string.*` ones reuse machinery 5.0 already ships:
//   - `match` is `find` returning captures / the whole match instead of the
//     start/end indices — so we call the engine's `str_find` and transform.
//   - `gmatch` is exactly 5.0's `string.gfind` (renamed in 5.1); we register
//     it as a direct alias of the engine's `gfind` C function.
// `strsplit` is a hand-rolled port of 3.3.5's `strsplit` (see below).
//
// NOTE: both call forms work — `string.foo(s, ...)` AND the `("x"):foo(...)`
// method sugar. Method calls resolve through this same `string` table via
// LuaSyntax::StringMethods (the Lua 5.1 string-metatable backport), so
// everything registered here is automatically reachable as a method, on
// both states.

#include "Game.h"
#include "Offsets.h"

#include <cstddef>
#include <cstring>
#include <string>

namespace BaseLib::StringLib {

namespace {

// `string.match(s, pattern [, init])`. Calls the engine `str_find` and
// reshapes its result: no match → nil; match with captures → the captures;
// match with no captures → the whole matched substring.
int __fastcall Script_string_match(void *L) {
    // Normalize to exactly (s, pattern, init): pad missing args with nil so
    // str_find defaults `init` to 1, and drop any stray 4th value so it can't
    // be misread as find's `plain` flag (match has no plain form).
    Game::Lua::SetTop(L, 3);
    constexpr int kBase = 3;

    auto strFind = reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_LUA_STR_FIND);
    const int nres = strFind(L); // pushes start,end,caps.. above kBase, or a nil

    if (nres <= 1) {
        // No match. str_find pushed the nil (return 1); guard the degenerate
        // nres==0 case by pushing our own nil so the shape holds.
        if (nres < 1)
            Game::Lua::PushNil(L);
        return 1;
    }

    if (nres == 2) {
        // No captures → the whole matched substring s[start..end] (1-based,
        // inclusive). An empty match has end == start-1 → an empty string.
        const int start = static_cast<int>(Game::Lua::ToNumber(L, kBase + 1));
        const int end = static_cast<int>(Game::Lua::ToNumber(L, kBase + 2));
        // str_find already coerced arg 1 to a string, so this is safe.
        const char *s = Game::Lua::ToString(L, 1);
        Game::Lua::SetTop(L, kBase); // drop start, end
        const size_t len = (end >= start) ? static_cast<size_t>(end - start + 1) : 0;
        Game::Lua::PushLString(L, s + start - 1, static_cast<unsigned int>(len));
        return 1;
    }

    // Captures present → drop start & end, hand back only the captures.
    Game::Lua::Remove(L, kBase + 1); // remove start; end → kBase+1, caps → kBase+2..
    Game::Lua::Remove(L, kBase + 1); // remove end;   caps → kBase+1..
    return nres - 2;
}

// `string.gmatch` is 5.0's `string.gfind` verbatim — register the engine
// function pointer directly rather than wrapping it.
const auto Script_string_gmatch =
    reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_LUA_STR_GFIND);

// `strsplit(sep, str [, pieces])` — the WoW global (also aliased as
// `string.split`), split `str` on ANY character in `sep` and return the
// pieces as multiple values. `pieces > 0` caps the result count, with the
// unsplit remainder as the final piece; `0` / omitted = unlimited. Ported
// from 3.3.5's `strsplit` (FUN_00816a60) with one deliberate change: we do
// NOT `lua_settop(L, 0)` up front. Popping the args would leave the source
// string unreferenced, so a GC step triggered by a `pushlstring` could free
// the very buffer we're still scanning. Keeping the args on the stack keeps
// the source GC-rooted; Lua returns the top N values (our pieces) regardless
// of the args sitting below them. Each push is guarded by `lua_checkstack`
// (this pushes an unbounded number of results), erroring like 3.3.5 on
// genuine stack exhaustion.
int __fastcall Script_strsplit(void *L) {
    const char *sep = Game::Lua::ToString(L, 1);
    const char *str = Game::Lua::ToString(L, 2);
    if (sep == nullptr || str == nullptr) {
        Game::Lua::Error(L, "Usage: strsplit(\"separators\", str [, pieces])");
        return 0; // unreachable
    }
    const int pieces =
        Game::Lua::IsNumber(L, 3) ? static_cast<int>(Game::Lua::ToNumber(L, 3)) : 0;

    const char *segStart = str;
    int count = 0;
    // Only scan for separators while unlimited (pieces == 0) or the cap
    // hasn't been reached (pieces > 1). pieces == 1 (or <= 0 but non-zero)
    // yields the whole string as a single piece — matches 3.3.5.
    if (pieces == 0 || pieces > 1) {
        for (const char *p = str; *p != '\0'; ++p) {
            bool isSep = false;
            for (const char *s = sep; *s != '\0'; ++s) {
                if (*p == *s) {
                    isSep = true;
                    break;
                }
            }
            if (!isSep)
                continue;
            ++count;
            if (Game::Lua::CheckStack(L, count) == 0) {
                Game::Lua::Error(L, "strsplit(): Stack overflow");
                return 0; // unreachable
            }
            Game::Lua::PushLString(L, segStart,
                                   static_cast<unsigned int>(p - segStart));
            segStart = p + 1;
            if (count == pieces - 1)
                break; // cap hit; remainder becomes the final piece
        }
    }
    if (Game::Lua::CheckStack(L, count + 1) == 0) {
        Game::Lua::Error(L, "strsplit(): Stack overflow");
        return 0; // unreachable
    }
    Game::Lua::PushString(L, segStart); // final piece (remainder to end)
    return count + 1;
}

// `strjoin(delimiter, ...)` — the inverse of `strsplit`: concatenate the
// vararg pieces with `delimiter` between each. Standard WoW global (FrameXML
// defines it in Lua; there's no C original to mirror). A nil / non-coercible
// piece is treated as an empty string rather than erroring, so the delimiter
// placement stays predictable. `strjoin(",")` with no pieces returns "".
int __fastcall Script_strjoin(void *L) {
    const char *delim = Game::Lua::ToString(L, 1);
    if (delim == nullptr) {
        Game::Lua::Error(L, "Usage: strjoin(delimiter, ...)");
        return 0; // unreachable
    }
    const unsigned delimLen = Game::Lua::StrLen(L, 1);
    const int top = Game::Lua::GetTop(L);

    std::string out; // no Error() past this point — a longjmp would leak it
    for (int i = 2; i <= top; ++i) {
        if (i > 2)
            out.append(delim, delimLen);
        const char *piece = Game::Lua::ToString(L, i);
        if (piece != nullptr)
            out.append(piece, Game::Lua::StrLen(L, i));
    }
    Game::Lua::PushLString(L, out.data(), static_cast<unsigned int>(out.size()));
    return 1;
}

// `strtrim(str [, chars])` — remove any of the characters in `chars` from
// both ends of `str` (default: whitespace). `chars` is a literal set of
// characters, not a pattern. Standard WoW global. Two-ended scan — no
// allocation, embedded NULs preserved (bounds come from the Lua length).
int __fastcall Script_strtrim(void *L) {
    const char *str = Game::Lua::ToString(L, 1);
    if (str == nullptr) {
        Game::Lua::Error(L, "Usage: strtrim(str [, chars])");
        return 0; // unreachable
    }
    const unsigned len = Game::Lua::StrLen(L, 1);
    const char *chars = Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2)
                                                  : " \t\n\v\f\r";

    bool trim[256] = {false};
    for (const char *c = chars; *c != '\0'; ++c)
        trim[static_cast<unsigned char>(*c)] = true;

    unsigned start = 0, end = len;
    while (start < end && trim[static_cast<unsigned char>(str[start])])
        ++start;
    while (end > start && trim[static_cast<unsigned char>(str[end - 1])])
        --end;
    Game::Lua::PushLString(L, str + start, end - start);
    return 1;
}

// `strreplace(str, find, replace)` — replace every occurrence of the literal
// substring `find` with `replace`; returns `(result, count)`. NOT a stock WoW
// global and NOT pattern-based (that's `gsub`) — a plain-text replace that
// needs no magic-character escaping. Empty `find` returns `str` unchanged
// (count 0) rather than looping forever.
int __fastcall Script_strreplace(void *L) {
    const char *str = Game::Lua::ToString(L, 1);
    const char *find = Game::Lua::ToString(L, 2);
    const char *repl = Game::Lua::ToString(L, 3);
    if (str == nullptr || find == nullptr || repl == nullptr) {
        Game::Lua::Error(L, "Usage: strreplace(str, find, replace)");
        return 0; // unreachable
    }
    const unsigned strLen = Game::Lua::StrLen(L, 1);
    const unsigned findLen = Game::Lua::StrLen(L, 2);
    const unsigned replLen = Game::Lua::StrLen(L, 3);

    if (findLen == 0) { // nothing to match — return input unchanged
        Game::Lua::PushLString(L, str, strLen);
        Game::Lua::PushNumber(L, 0.0);
        return 2;
    }

    std::string out; // no Error() past this point
    int count = 0;
    unsigned i = 0;
    while (i < strLen) {
        if (i + findLen <= strLen && std::memcmp(str + i, find, findLen) == 0) {
            out.append(repl, replLen);
            i += findLen;
            ++count;
        } else {
            out.push_back(str[i]);
            ++i;
        }
    }
    Game::Lua::PushLString(L, out.data(), static_cast<unsigned int>(out.size()));
    Game::Lua::PushNumber(L, static_cast<double>(count));
    return 2;
}

// `string.reverse(s)` / `strrev(s)` — return `s` with its bytes reversed.
// Lua 5.1 added `string.reverse`; 5.0's strlib lacks it. Byte-wise (not
// UTF-8 aware), matching stock Lua; embedded NULs preserved (bounds from the
// Lua length).
int __fastcall Script_string_reverse(void *L) {
    const char *s = Game::Lua::ToString(L, 1);
    if (s == nullptr) {
        Game::Lua::Error(L, "Usage: string.reverse(s)");
        return 0; // unreachable
    }
    const unsigned len = Game::Lua::StrLen(L, 1);
    std::string out(len, '\0'); // no Error() past this point
    for (unsigned i = 0; i < len; ++i)
        out[i] = s[len - 1 - i];
    Game::Lua::PushLString(L, out.data(), len);
    return 1;
}

// --- string.gsub table-replacement upgrade (Lua 5.1) ------------------------
//
// 5.1 allows a TABLE as gsub's replacement: each match looks up the first
// capture (the whole match when the pattern declares no captures) in the
// table; a nil/false value keeps the original match; string/number values
// substitute. This engine's gsub (FUN_LUA_STR_GSUB) is stock 5.0 —
// string/function only ("string or function expected" otherwise), and its
// add_value appends NOTHING when a replacement function returns a
// non-string (match → empty; verified by decode, see Offsets.h). So a
// plain table→closure adapter would EAT unmatched tokens where 5.1 keeps
// them, and the closure can't reconstruct the whole match from capture
// args alone.
//
// The shim therefore rewrites the pattern with one OUTER capture —
// `pat` → `(pat)`, keeping a leading `^` and an unescaped trailing `$`
// outside (both are anchors only at their positional ends; inside the
// parens they'd turn literal) — so the adapter closure always receives
// the whole match as arg 1, the pattern's own first capture as arg 2, and
// can hand arg 1 back to reproduce 5.1's keep-match for missing keys.
// Patterns containing a `%1`..`%9` back-reference can't be wrapped (the
// outer capture would renumber what `%N` refers to); those fall back to
// an unwrapped adapter whose missing-key behavior is the engine's own
// (match → empty) — the one documented divergence, and the combination
// (table replacement + pattern back-references) is essentially absent
// from real code. String/function replacements tail-call the engine gsub
// untouched.

// True when the pattern contains a `%1`..`%9` back-reference. Escape-aware:
// the char after any `%` is consumed, so `%%1` (literal percent, then '1')
// doesn't count.
bool PatternHasBackref(const char *p, unsigned len) {
    for (unsigned i = 0; i + 1 < len; ++i) {
        if (p[i] != '%')
            continue;
        const char c = p[i + 1];
        if (c >= '1' && c <= '9')
            return true;
        ++i; // skip the escaped char
    }
    return false;
}

// Adapter for the wrapped-pattern path: arg 1 = whole match, arg 2 = the
// pattern's own first capture (when it declared any). Upvalue 1 = the
// replacement table. Missing/false value → return the whole match (5.1's
// keep-match).
int __fastcall GsubTableLookupWrapped(void *L) {
    const int nargs = Game::Lua::GetTop(L);
    Game::Lua::PushValue(L, nargs >= 2 ? 2 : 1);        // the 5.1 lookup key
    Game::Lua::GetTable(L, Game::Lua::UpvalueIndex(1)); // tbl[key]
    const int t = Game::Lua::Type(L, -1);
    if (t == Game::Lua::TYPE_NIL ||
        (t == Game::Lua::TYPE_BOOLEAN && Game::Lua::ToBoolean(L, -1) == 0))
        Game::Lua::PushValue(L, 1); // keep the original match
    return 1;                       // top value is the result
}

// Adapter for back-reference patterns (unwrapped): arg 1 is already the
// 5.1 key. A missing key returns nil → the engine appends nothing.
int __fastcall GsubTableLookupPlain(void *L) {
    Game::Lua::PushValue(L, 1);
    Game::Lua::GetTable(L, Game::Lua::UpvalueIndex(1));
    return 1;
}

int __fastcall Script_string_gsub(void *L) {
    const auto engineGsub =
        reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_LUA_STR_GSUB);
    if (Game::Lua::Type(L, 3) != Game::Lua::TYPE_TABLE)
        return engineGsub(L); // string/function/error paths untouched

    const char *pat = Game::Lua::ToString(L, 2);
    if (pat == nullptr)
        return engineGsub(L); // let the engine raise its own arg error
    const unsigned patLen = Game::Lua::StrLen(L, 2);

    Game::Lua::SetTop(L, 4); // normalize to (s, pat, tbl, n) — gsub reads ≤4

    if (!PatternHasBackref(pat, patLen)) {
        // Build `(body)` with the anchors outside the capture.
        unsigned b = 0, e = patLen;
        std::string wrapped;
        wrapped.reserve(patLen + 3);
        if (e > b && pat[0] == '^') {
            wrapped += '^';
            b = 1;
        }
        bool anchoredBack = false;
        if (e > b && pat[e - 1] == '$') {
            // `$` is an anchor only when not %-escaped: an even number of
            // `%` immediately before it means the `$` itself is unescaped.
            unsigned pc = 0;
            for (int idx = static_cast<int>(e) - 2;
                 idx >= static_cast<int>(b) && pat[idx] == '%'; --idx)
                ++pc;
            anchoredBack = (pc % 2) == 0;
            if (anchoredBack)
                --e;
        }
        wrapped += '(';
        wrapped.append(pat + b, e - b);
        wrapped += ')';
        if (anchoredBack)
            wrapped += '$';

        Game::Lua::PushLString(L, wrapped.data(),
                               static_cast<unsigned int>(wrapped.size())); // 5
        Game::Lua::PushValue(L, 3);                                        // 6: tbl
        Game::Lua::PushCClosure(L, &GsubTableLookupWrapped, 1);            // 6: closure
        Game::Lua::Insert(L, 2); // closure→2: s, closure, pat, tbl, n, wrapped
        Game::Lua::Insert(L, 2); // wrapped→2: s, wrapped, closure, pat, tbl, n
        Game::Lua::Remove(L, 4); // drop the original pattern
        Game::Lua::Remove(L, 4); // drop the table → (s, wrapped, closure, n)
    } else {
        Game::Lua::PushValue(L, 3);                           // 5: tbl
        Game::Lua::PushCClosure(L, &GsubTableLookupPlain, 1); // 5: closure
        Game::Lua::Insert(L, 3); // → s, pat, closure, tbl, n
        Game::Lua::Remove(L, 4); // → s, pat, closure, n
    }
    return engineGsub(L);
}

void RegisterFns() {
    Game::Lua::RegisterTableFunction("string", "match", &Script_string_match);
    Game::Lua::RegisterTableFunction("string", "gmatch", Script_string_gmatch);
    Game::Lua::RegisterTableFunction("string", "gsub", &Script_string_gsub);
    Game::Lua::RegisterTableFunction("string", "reverse", &Script_string_reverse);
    Game::Lua::RegisterTableFunction("string", "split", &Script_strsplit);
    // The bare `gsub` global (1.12 exposes the string functions as globals
    // too) must match `string.gsub`, so addons calling either form get the
    // table-replacement upgrade.
    Game::Lua::RegisterGlobalFunction("gsub", &Script_string_gsub);
    // 1.12's startup script makes globals of the 5.0 string functions
    // (`strfind = str.find`, `strsub = str.sub`, ...); 3.x clients also have
    // `strmatch`, which 3.3.5's FrameXML calls without defining.
    Game::Lua::RegisterGlobalFunction("strmatch", &Script_string_match);
    Game::Lua::RegisterGlobalFunction("strsplit", &Script_strsplit);
    Game::Lua::RegisterGlobalFunction("strjoin", &Script_strjoin);
    Game::Lua::RegisterGlobalFunction("strtrim", &Script_strtrim);
    Game::Lua::RegisterGlobalFunction("strreplace", &Script_strreplace);
    Game::Lua::RegisterGlobalFunction("strrev", &Script_string_reverse);
}

// Both states are Lua 5.0 and equally missing these; RegisterTableFunction
// targets whichever state is active in each registration hook.
const Game::ModuleAutoRegister _autoreg{&RegisterFns};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterFns};

} // namespace

} // namespace BaseLib::StringLib
