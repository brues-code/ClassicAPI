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
//   - `string.gmatch(s, pattern)`         — match iterator (5.1), also `gmatch`.
//   - `strsplit(sep, str [, pieces])`     — WoW global; split on any char.
//   - `strjoin(delimiter, ...)`           — WoW global; join varargs.
//   - `strtrim(str [, chars])`            — WoW global; trim a char set.
//   - `strreplace(str, find, replace)`    — plain-text replace (see below).
//   - `string.reverse(s)` / `strrev(s)`   — 5.1 string function, missing in 5.0.
//   - `string.byte(s, i, j)` / `strbyte`  — 5.1's range form (5.0 reads only i).
//   - `strlenutf8(s)`                     — WoW global; characters, not bytes.
//   - `strcmputf8i(a, b)`                 — WoW global; case-insensitive UTF-8 order.
//   - `strsplittable(sep, str [, pieces])`— WoW global; `strsplit` into a table.
//   - `strconcat(...)` / `string.concat`  — WoW global; `..` over every argument.
//
// The two UTF-8 globals are 4.3.4's `Script_strlenutf8` and the newer
// clients' `strcmputf8i`, each a thin push over a Storm helper 1.12 does not
// have (`SStrLenUTF8` / `SStrCmpUTF8I`) — `Text::Utf8` mirrors those.
// `strconcat` is 4.3.4's: `lua_concat` over the whole stack.
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
#include "text/Utf8.h"

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

// The scan behind `strsplit` and `strsplittable`: splits `str` on ANY
// character in `sep` and hands each piece to `emit(start, length)` in order,
// the unsplit remainder last. `pieces > 0` caps the count, with the
// remainder (delimiters and all) as the final piece; `0` is unlimited.
// Only scans for separators while unlimited or the cap hasn't been reached;
// `pieces == 1` (or <= 0 but non-zero) yields the whole string as a single
// piece — matches 3.3.5's `strsplit` (FUN_00816a60).
template <typename Emit>
void SplitPieces(const char *sep, const char *str, int pieces, Emit emit) {
    const char *segStart = str;
    int count = 0;
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
            emit(segStart, static_cast<unsigned int>(p - segStart));
            segStart = p + 1;
            if (count == pieces - 1)
                break; // cap hit; remainder becomes the final piece
        }
    }
    emit(segStart, static_cast<unsigned int>(std::strlen(segStart)));
}

// `strsplit(sep, str [, pieces])` — the WoW global (also aliased as
// `string.split`): the pieces as multiple values. Ported from 3.3.5 with one
// deliberate change: we do NOT `lua_settop(L, 0)` up front. Popping the args
// would leave the source string unreferenced, so a GC step triggered by a
// `pushlstring` could free the very buffer we're still scanning. Keeping the
// args on the stack keeps the source GC-rooted; Lua returns the top N values
// (our pieces) regardless of the args sitting below them. Each push is
// guarded by `lua_checkstack` (this pushes an unbounded number of results),
// erroring like 3.3.5 on genuine stack exhaustion.
int __fastcall Script_strsplit(void *L) {
    const char *sep = Game::Lua::ToString(L, 1);
    const char *str = Game::Lua::ToString(L, 2);
    if (sep == nullptr || str == nullptr) {
        Game::Lua::Error(L, "Usage: strsplit(\"separators\", str [, pieces])");
        return 0; // unreachable
    }
    const int pieces =
        Game::Lua::IsNumber(L, 3) ? static_cast<int>(Game::Lua::ToNumber(L, 3)) : 0;

    int count = 0;
    SplitPieces(sep, str, pieces, [L, &count](const char *start, unsigned int len) {
        ++count;
        if (Game::Lua::CheckStack(L, count) == 0) {
            Game::Lua::Error(L, "strsplit(): Stack overflow");
            return; // unreachable
        }
        Game::Lua::PushLString(L, start, len);
    });
    return count;
}

// `strsplittable(sep, str [, pieces])` — the same split as one array
// table. Every piece goes to the next integer key of a fresh table, so the
// Lua stack never grows and no piece count is too many — the point of the
// table form. The args stay on the stack (GC-rooting the source, as above);
// the table sits above them at index 4.
int __fastcall Script_strsplittable(void *L) {
    const char *sep = Game::Lua::ToString(L, 1);
    const char *str = Game::Lua::ToString(L, 2);
    if (sep == nullptr || str == nullptr) {
        Game::Lua::Error(L, "Usage: strsplittable(\"separators\", str [, pieces])");
        return 0; // unreachable
    }
    const int pieces =
        Game::Lua::IsNumber(L, 3) ? static_cast<int>(Game::Lua::ToNumber(L, 3)) : 0;

    Game::Lua::SetTop(L, 3);
    Game::Lua::NewTable(L); // 4
    int count = 0;
    SplitPieces(sep, str, pieces, [L, &count](const char *start, unsigned int len) {
        Game::Lua::PushNumber(L, static_cast<double>(++count));
        Game::Lua::PushLString(L, start, len);
        Game::Lua::RawSet(L, 4);
    });
    return 1;
}

// `strlenutf8(s)` — 4.3.4's `Script_strlenutf8`: `SStrLenUTF8` on the
// string, the count of characters (code points) rather than bytes. Same
// usage text and `lua_isstring` gate as the original.
int __fastcall Script_strlenutf8(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: strlenutf8(string)");
        return 0; // unreachable
    }
    Game::Lua::PushNumber(
        L, static_cast<double>(Text::Utf8::Length(Game::Lua::ToString(L, 1))));
    return 1;
}

// `strcmputf8i(string1, string2)` — the newer clients' global (its usage
// text is in their binaries) over `SStrCmpUTF8I`: a case-insensitive sort
// order for UTF-8 text. The number is the first folded code-point
// difference, as Storm returns it; FrameXML only ever tests the sign (`< 0`
// in sort comparators, `== 0` for a name match).
int __fastcall Script_strcmputf8i(void *L) {
    if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: strcmputf8i(string1, string2)");
        return 0; // unreachable
    }
    const int result =
        Text::Utf8::CompareI(Game::Lua::ToString(L, 1), Game::Lua::ToString(L, 2));
    Game::Lua::PushNumber(L, static_cast<double>(result));
    return 1;
}

// `strconcat(...)` / `string.concat(...)` — 4.3.4's `Script_strconcat`:
// `lua_concat` over everything on the stack, i.e. the `..` operator applied
// to every argument (numbers convert, anything else raises the concatenate
// error), `""` for no arguments, and a single argument handed back as it is.
int __fastcall Script_strconcat(void *L) {
    Game::Lua::Concat(L, Game::Lua::GetTop(L));
    return 1;
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

// `string.byte(s [, i [, j]])` / `strbyte` — the codes of bytes i..j, as
// 4.3.4's str_byte (j defaults to i). 1.12's (0x7FC210) is 5.0's: it reads
// only `i` and pushes one code. With `j` absent the two give the same
// result for every `i`. Positions truncate toward zero as the engine's
// str_byte and str_sub do. The engine's lua_checkstack (0x6F2F30) refuses
// more than 2048 stack slots, so the longest range is about 2000 bytes.

// 5.1's posrelat: a negative position counts from the end; before the start is 0.
long PosRelat(long pos, long len) {
    if (pos < 0)
        pos += len + 1;
    return pos >= 0 ? pos : 0;
}

int __fastcall Script_string_byte(void *L) {
    unsigned int l = 0;
    const char *s = Game::Lua::CheckLString(L, 1, &l);
    const long len = static_cast<long>(l);
    long posi = PosRelat(static_cast<long>(Game::Lua::OptNumber(L, 2, 1)), len);
    long pose =
        PosRelat(static_cast<long>(Game::Lua::OptNumber(L, 3, static_cast<double>(posi))), len);
    if (posi <= 0)
        posi = 1;
    if (pose > len)
        pose = len;
    if (posi > pose)
        return 0;
    const int n = static_cast<int>(pose - posi + 1);
    if (!Game::Lua::CheckStack(L, n))
        Game::Lua::Error(L, "stack overflow (%s)", "string slice too long");
    for (int i = 0; i < n; i++)
        Game::Lua::PushNumber(L, static_cast<unsigned char>(s[posi + i - 1]));
    return n;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kStrlenutf8Args[] = {
    Game::Doc::Req("string", "string", "UTF-8 text."),
};
const Game::Doc::Field kStrlenutf8Rets[] = {
    Game::Doc::Req("length", "number", "The number of characters; strlen counts bytes."),
};
const Game::Doc::Function kStrlenutf8{
    "The number of characters in a UTF-8 string, not the number of bytes.",
    kStrlenutf8Args, kStrlenutf8Rets, "LuaGlobals"};

const Game::Doc::Field kStrcmputf8iArgs[] = {
    Game::Doc::Req("string1", "string"),
    Game::Doc::Req("string2", "string"),
};
const Game::Doc::Field kStrcmputf8iRets[] = {
    Game::Doc::Req("result", "number",
                   "Negative when string1 sorts first, 0 when the two match ignoring "
                   "case, positive when string2 sorts first."),
};
const Game::Doc::Function kStrcmputf8i{
    "Compares two UTF-8 strings ignoring case, for sorting and matching.",
    kStrcmputf8iArgs, kStrcmputf8iRets, "LuaGlobals"};

const Game::Doc::Field kStrsplittableArgs[] = {
    Game::Doc::Req("delimiter", "string", "Each character is a delimiter."),
    Game::Doc::Req("str", "string"),
    Game::Doc::Opt("pieces", "number", "0",
                   "Maximum number of pieces; the last one holds the rest of the string. "
                   "0 is unlimited."),
};
const Game::Doc::Field kStrsplittableRets[] = {
    Game::Doc::Req("chunks", "table", "Array of the pieces, in order."),
};
const Game::Doc::Function kStrsplittable{
    "Splits a string on delimiter characters and returns the pieces as an array.",
    kStrsplittableArgs, kStrsplittableRets, "LuaGlobals"};

const Game::Doc::Field kStrconcatArgs[] = {
    Game::Doc::Vararg("string", "Strings or numbers."),
};
const Game::Doc::Field kStrconcatRets[] = {
    Game::Doc::Req("result", "string"),
};
const Game::Doc::Function kStrconcat{
    "Joins every argument into one string with nothing between them.",
    kStrconcatArgs, kStrconcatRets, "LuaGlobals"};

void RegisterFns() {
    Game::Lua::RegisterTableFunction("string", "match", &Script_string_match);
    Game::Lua::RegisterTableFunction("string", "gmatch", Script_string_gmatch);
    Game::Lua::RegisterTableFunction("string", "reverse", &Script_string_reverse);
    Game::Lua::RegisterTableFunction("string", "byte", &Script_string_byte);
    Game::Lua::RegisterTableFunction("string", "split", &Script_strsplit);
    Game::Lua::RegisterTableFunction("string", "trim", &Script_strtrim);
    Game::Lua::RegisterTableFunction("string", "join", &Script_strjoin);
    Game::Lua::RegisterTableFunction("string", "concat", &Script_strconcat, &kStrconcat);
    // 4.3.4's compat.lua binds each short global and its string-table twin
    // to ONE function by assignment (`gmatch = str.gmatch`, `strmatch =
    // str.match`, `strrev = str.reverse`, `str.trim = strtrim`, `str.split =
    // strsplit`, `str.join = strjoin`), so `strtrim == string.trim`. A second
    // Register* call would mint a second closure over the same C function, so
    // the globals alias the table entries by value instead. The string-table
    // half is what lets `(" x "):trim()` work and libraries capture it at
    // file scope (AceConfigCmd-3.0: `strtrim = string.trim`). 1.12's own
    // compat.lua set `strbyte` from the old string.byte, so it is re-aliased.
    Game::Lua::RegisterGlobalAlias("gmatch", "string", "gmatch");
    Game::Lua::RegisterGlobalAlias("strmatch", "string", "match");
    Game::Lua::RegisterGlobalAlias("strsplit", "string", "split");
    Game::Lua::RegisterGlobalAlias("strjoin", "string", "join");
    Game::Lua::RegisterGlobalAlias("strtrim", "string", "trim");
    Game::Lua::RegisterGlobalAlias("strrev", "string", "reverse");
    Game::Lua::RegisterGlobalAlias("strbyte", "string", "byte");
    Game::Lua::RegisterGlobalAlias("strconcat", "string", "concat", &kStrconcat);
    Game::Lua::RegisterGlobalFunction("strreplace", &Script_strreplace);
    Game::Lua::RegisterGlobalFunction("strlenutf8", &Script_strlenutf8, &kStrlenutf8);
    Game::Lua::RegisterGlobalFunction("strcmputf8i", &Script_strcmputf8i, &kStrcmputf8i);
    Game::Lua::RegisterGlobalFunction("strsplittable", &Script_strsplittable,
                                      &kStrsplittable);
}

// Both states are Lua 5.0 and equally missing these; RegisterTableFunction
// targets whichever state is active in each registration hook.
const Game::ModuleAutoRegister _autoreg{&RegisterFns};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterFns};

} // namespace

} // namespace BaseLib::StringLib
