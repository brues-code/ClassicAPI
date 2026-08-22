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

// Lua 5.1 syntax backport (source-level transpile).
//
// Vanilla's Lua is 5.0. It lacks the 5.1 operators `#` (length) and `%`
// (modulo), `...` used as an expression, `0x` hexadecimal number literals, AND
// leveled long brackets (`[=[`…`]=]`), so modern addon ports that use them fail
// to COMPILE. We don't touch the 5.0 parser/VM (there is no OP_LEN / OP_MOD and
// no free opcode); instead we rewrite the SOURCE before it reaches the parser:
// co-hook `luaL_loadbuffer` (the one function every compile funnels through —
// file scripts, `loadstring`, XML `<OnLoad>`) and rewrite:
//     #operand      ->  __len(operand)
//     a % b         ->  __mod(a, b)
//     ...           ->  unpack(arg)      (the `...` EXPRESSION, not the decl)
//     0xHH...       ->  <decimal>        (5.0's lexer rejects hex literals)
//     [=[ … ]=]     ->  [[ … ]] or "…"   (5.0 has no leveled long brackets)
// `__len` / `__mod` are C globals we register (see below); `unpack` and the
// 5.0 `arg` table are already present. RunPasses runs the hex pass first, then
// the vararg pass, then the # / % precedence parser, over one shared
// tokenization (RewriteChunk and LoadBuffer_h both go through it).
//
// Why a real parser (not regex / one-term-each-side): `#` is prefix but `%`
// is binary infix, so its operands must be delimited by PRECEDENCE —
// `a * b % c` is `__mod(a*b, c)`, `a + b % c` is `a + __mod(b,c)`, and
// `a % b % c` chains left-associatively as `__mod(__mod(a,b), c)`. A
// precedence-climbing parse gets all of these right and composes `#` and `%`
// in a single pass (`#a % b` -> `__mod(__len(a), b)`).
//
// Why __mod is not math.mod: the engine's `math.mod` is C fmod (truncated
// toward zero), which disagrees with Lua `%` for negative operands
// (`-1 % 3` == 2, but fmod(-1,3) == -1). __mod computes the real 5.1
// definition `a - floor(a/b)*b`.
//
// Safety: the rewrite runs only when a chunk contains `#` or `%`, and either
// as an OPERATOR already fails to compile on 5.0 — so we cannot regress
// working code UNLESS we misread one inside a string/comment (`"%d"`,
// `"%a+"`, `--[[ # ]]`). The lexer's string/comment skipping is therefore
// the one safety-critical part, and it is exact. No newlines are inserted,
// so error line numbers are preserved.
//
// Prototype scope / limits (documented):
//   * Nested long strings/comments (`[[ a [[ b ]] c ]]`) are depth-matched to
//     mirror the 5.0 engine lexer (which nests), so string/comment boundaries
//     are exact.
//   * Leveled long brackets (`[=[`…`]=]`, `[==[`…`]==]`) — a 5.1 form this 5.0
//     engine has no lexer support for — are rewritten to a `[[…]]` or quoted
//     literal (long comments blanked to spaces), line-count-preserving. See the
//     RewriteLongBrackets pass below.
//   * `__len` on a table returns a border (bisection) = 5.1 `#`; it ignores
//     any `table.setn` count (5.1 has no setn — correct `#` semantics).
//   * Hex literals: only INTEGER `0x…` (up to 16 hex digits) are converted,
//     to the exact decimal the value represents — the same double 5.1 would
//     produce. Hex FLOATS (`0x1.8p3`) and > 64-bit literals are left as-is
//     (vanishingly rare in addons; they fail to compile exactly as today, so
//     no regression). The value is unsigned (`0xFFFFFFFF` -> 4294967295).
//   * Diagnostic `_classicapi_TranspileLength(src)` returns the full rewrite.
//   * Toggles via `_classicapi_SetTranspileOption(name, bool)` /
//     `_classicapi_GetTranspileOption(name)` (name = "Length" / "Modulo" /
//     "VarargExpansion" / "HexLiterals" / "LongBrackets").
//   * `...` expands to `unpack(arg)`, which is faithful in every position and
//     preserves embedded nils via `arg.n` (this build's `unpack` honors it).
//     Only the `...` in a function's parameter list is left intact.
//   * Addon-args: vanilla never passed `(addonName, addonTable)` to addon
//     chunks (1.12's main chunk has no `arg`), so the modern file-scope
//     `local name, ns = ...` idiom would resolve to `unpack(nil)`. For addon
//     files (chunkname `@Interface\AddOns\<Name>\...`) that use a real `...`,
//     we prepend a chunk-local `arg = {"<Name>", __addonns("<Name>"), n=2}` so
//     `...` yields the name + a per-addon shared table. `__addonns` returns the
//     same table for every file of an addon (registry-backed). The preamble is
//     newline-free (line numbers preserved).
//   * Non-addon vararg chunks (RunScript `/run`, XML <OnLoad>, loadstring) get a
//     `local arg = {n=0}` fallback: their main chunk is not vararg, so a
//     top-level `...` would otherwise be `unpack(nil)` and throw where 5.1
//     yields nothing. A chunk called with args (`loadstring(...)(a,b)`) can't
//     recover them — this build doesn't populate `arg` for called chunks.
//     The `__addonns` global is CONTEXT-GATED (see `Script_AddonNS`): it only
//     answers for the addon mid-load, for its own name — so it cannot be used
//     at runtime to read another addon's private table. Cross-addon access is
//     the separate, TOC-opt-in `C_AddOns.GetAddOnLocalTable`.

#include "Game.h"
#include "Offsets.h"
#include "luasyntax/AddonNamespace.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <intrin.h> // _ReturnAddress
#include <string>
#include <vector>

namespace LuaSyntax {

namespace {

constexpr size_t NPOS = static_cast<size_t>(-1);

// Runtime switches, default ON — a `#`/`%`/`...`/`0x`/`[=[`-bearing chunk does
// not compile on 5.0 today, so enabling by default cannot regress working addons.
bool g_lenEnabled = true;
bool g_modEnabled = true;
bool g_varargEnabled = true;
bool g_hexEnabled = true;
bool g_longBracketEnabled = true;

// lua_rawgeti(L, idx, n) — push table_at_idx[n] without metamethods. Not
// exposed via Game::Lua; used to probe table elements for the border search.
using RawGetI_t = void(__fastcall *)(void *L, int idx, int n);
const auto RawGetI = reinterpret_cast<RawGetI_t>(Offsets::LUA_RAWGETI);

// ============================================================================
// Lexer — enough of Lua 5.0 to skip strings/comments and delimit expressions.
// ============================================================================

enum TokKind { TK_NAME, TK_NUMBER, TK_STRING, TK_PUNCT };

struct Token {
    TokKind kind;
    size_t start;
    size_t end; // one past last byte
};

inline bool IsNameStart(unsigned char c) {
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
inline bool IsNameCont(unsigned char c) {
    return IsNameStart(c) || (c >= '0' && c <= '9');
}
inline bool IsDigit(unsigned char c) { return c >= '0' && c <= '9'; }

// If `src[pos]` opens a long bracket (`[`, N `=`, `[`), return N (>= 0), else -1.
int LongBracketLevel(const char *src, size_t len, size_t pos) {
    if (pos >= len || src[pos] != '[')
        return -1;
    size_t j = pos + 1;
    while (j < len && src[j] == '=')
        j++;
    if (j < len && src[j] == '[')
        return static_cast<int>(j - pos - 1);
    return -1;
}

// Skip a long string/comment from the opening `[` (`pos`). Returns offset past
// the close, or len if unterminated.
//
// Lua 5.0 (this client) NESTS level-0 `[[ ]]`: an inner `[[` raises the depth,
// a `]]` lowers it, and the string closes only at depth 0 — verified against
// the engine's own long-string reader FUN_00700010, which keeps exactly this
// counter. We must mirror it or we mis-identify where a nested long string ends
// and could rewrite `#`/`%` bytes the parser actually treats as string content.
// `[=[`-style levels are a 5.1 form this 5.0 client does not accept (its reader
// has no `=` handling), so they never nest — first matching close wins.
size_t SkipLongBracket(const char *src, size_t len, size_t pos, int level) {
    size_t i = pos + 2 + static_cast<size_t>(level);
    int depth = 0;
    while (i < len) {
        if (level == 0 && src[i] == '[' && i + 1 < len && src[i + 1] == '[') {
            depth++;
            i += 2;
            continue;
        }
        if (src[i] == ']') {
            size_t j = i + 1;
            int eq = 0;
            while (j < len && src[j] == '=') {
                j++;
                eq++;
            }
            if (eq == level && j < len && src[j] == ']') {
                if (depth == 0)
                    return j + 1;
                depth--;
                i = j + 1;
                continue;
            }
        }
        i++;
    }
    return len;
}

// Skip a short string from the opening quote. Returns offset past the close,
// or an unterminating newline / EOF offset.
size_t SkipShortString(const char *src, size_t len, size_t pos) {
    char q = src[pos];
    size_t i = pos + 1;
    while (i < len) {
        char c = src[i];
        if (c == '\\') {
            i += 2;
            continue;
        }
        if (c == q)
            return i + 1;
        if (c == '\n')
            return i;
        i++;
    }
    return len;
}

// Skip a numeric literal. Consumes one decimal point at most (so `1..2` is not
// merged) plus hex/exponent chars.
size_t SkipNumber(const char *src, size_t len, size_t pos) {
    size_t i = pos;
    const bool isHex = pos + 1 < len && src[pos] == '0' &&
                       (src[pos + 1] == 'x' || src[pos + 1] == 'X');
    bool seenDot = false;
    while (i < len) {
        char c = src[i];
        if (c == '.') {
            // A second dot, or the `..` concat operator, ends the number so
            // `0xFF.."x"` / `1..2` split cleanly (the concat is not consumed).
            if (seenDot || (i + 1 < len && src[i + 1] == '.'))
                break;
            seenDot = true;
            i++;
            continue;
        }
        if (IsNameCont(static_cast<unsigned char>(c))) {
            i++;
            continue;
        }
        if ((c == '+' || c == '-') && i > pos) {
            // Exponent sign continues the number. The marker is e/E for a
            // decimal literal but p/P for a hex float — in a hex literal e/E
            // are DIGITS, so `0xE-1` must break at `-` (not swallow it, which
            // would make `0xE-1 % 2` parse as `__mod(0xE-1,2)`, a wrong result).
            char p = src[i - 1];
            const bool expMarker =
                isHex ? (p == 'p' || p == 'P') : (p == 'e' || p == 'E');
            if (expMarker) {
                i++;
                continue;
            }
        }
        break;
    }
    return i;
}

void Tokenize(const char *src, size_t len, std::vector<Token> &out) {
    size_t i = 0;
    while (i < len) {
        unsigned char c = static_cast<unsigned char>(src[i]);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
            i++;
            continue;
        }
        if (c == '-' && i + 1 < len && src[i + 1] == '-') { // comment
            size_t j = i + 2;
            if (j < len && src[j] == '[') {
                int lvl = LongBracketLevel(src, len, j);
                if (lvl >= 0) {
                    i = SkipLongBracket(src, len, j, lvl);
                    continue;
                }
            }
            while (i < len && src[i] != '\n')
                i++;
            continue;
        }
        if (c == '[') { // long string?
            int lvl = LongBracketLevel(src, len, i);
            if (lvl >= 0) {
                size_t s = i;
                i = SkipLongBracket(src, len, i, lvl);
                out.push_back({TK_STRING, s, i});
                continue;
            }
        }
        if (c == '"' || c == '\'') {
            size_t s = i;
            i = SkipShortString(src, len, i);
            out.push_back({TK_STRING, s, i});
            continue;
        }
        if (IsNameStart(c)) {
            size_t s = i;
            i++;
            while (i < len && IsNameCont(static_cast<unsigned char>(src[i])))
                i++;
            out.push_back({TK_NAME, s, i});
            continue;
        }
        if (IsDigit(c) || (c == '.' && i + 1 < len && IsDigit(static_cast<unsigned char>(src[i + 1])))) {
            size_t s = i;
            i = SkipNumber(src, len, i);
            out.push_back({TK_NUMBER, s, i});
            continue;
        }
        if (c == '.') {
            // Dots tokenize greedily like Lua's own lexer: '.', '..', or '...'
            // (at most 3). A '.<digit>' was already taken as a number above, so
            // a '.' reaching here is punctuation. Emitting '...' as ONE token is
            // what lets the vararg pass tell it apart from an adjacent '..'
            // concat: `x .. ...` is `..` then `...`, never a five-dot run.
            size_t d = 1;
            while (d < 3 && i + d < len && src[i + d] == '.')
                d++;
            out.push_back({TK_PUNCT, i, i + d});
            i += d;
            continue;
        }
        out.push_back({TK_PUNCT, i, i + 1}); // single punctuation byte
        i++;
    }
}

// ============================================================================
// Parser — precedence climbing that records `#` and `%` rewrites.
// ============================================================================

struct Open {
    size_t off;
    const char *text;
    size_t span; // off..closeOff — larger span = outer (emitted first)
};
struct Close {
    size_t off;
    size_t span; // smaller span = inner (emitted first)
};
struct CharOp {
    size_t off;
    char repl; // 0 => delete the byte, else replace it
};

struct Ctx {
    const char *src;
    const std::vector<Token> *toks;
    std::vector<Open> opens;
    std::vector<Close> closes;
    std::vector<CharOp> charOps;
    bool lenOn;
    bool modOn;
    int depth = 0; // current parse-recursion depth (see DepthGuard)
};

// The `#`/`%` parser recurses per nested bracket / chained operator. Cap the
// depth so a pathological chunk (`{{{{…}}}}` thousands deep) can't overflow the
// C stack inside the hook — Lua's own parser rejects that with a clean "too
// many syntax levels" error at ~200 (LUAI_MAXCCALLS); we mirror the bound and
// simply stop rewriting the over-deep region (it then fails to compile on 5.0
// exactly as it would have without us — no crash, no regression).
constexpr int kMaxParseDepth = 200;
struct DepthGuard {
    int &d;
    explicit DepthGuard(int &dd) : d(dd) { ++d; }
    ~DepthGuard() { --d; }
};

inline const Token &Tk(const Ctx &c, size_t i) { return (*c.toks)[i]; }
inline size_t NT(const Ctx &c) { return c.toks->size(); }

inline bool SpanEq(const char *src, const Token &t, const char *w) {
    size_t n = t.end - t.start;
    return std::strlen(w) == n && std::memcmp(src + t.start, w, n) == 0;
}
inline bool PunctIs(const Ctx &c, size_t k, char ch) {
    return k < NT(c) && Tk(c, k).kind == TK_PUNCT && c.src[Tk(c, k).start] == ch;
}
inline bool NameIs(const Ctx &c, size_t k, const char *w) {
    return k < NT(c) && Tk(c, k).kind == TK_NAME && SpanEq(c.src, Tk(c, k), w);
}

bool IsKeyword(const char *src, const Token &t) {
    static const char *const kw[] = {
        "and",  "break",  "do",   "else",   "elseif", "end",    "false",
        "for",  "function", "if", "in",     "local",  "nil",    "not",
        "or",   "repeat", "return", "then",  "true",   "until",  "while"};
    for (const char *w : kw)
        if (SpanEq(src, t, w))
            return true;
    return false;
}
inline bool IsNameNonKw(const Ctx &c, size_t k) {
    return k < NT(c) && Tk(c, k).kind == TK_NAME && !IsKeyword(c.src, Tk(c, k));
}

// From an opening-bracket token at `k`, return the index just past the matching
// close, or NPOS if unbalanced.
size_t MatchBracket(const Ctx &c, size_t k, char open, char close) {
    int depth = 0;
    for (size_t j = k; j < NT(c); j++) {
        if (Tk(c, j).kind != TK_PUNCT)
            continue;
        char ch = c.src[Tk(c, j).start];
        if (ch == open)
            depth++;
        else if (ch == close) {
            depth--;
            if (depth == 0)
                return j + 1;
        }
    }
    return NPOS;
}

size_t ParseExpr(Ctx &c, size_t k, int minPrec);
void ScanRange(Ctx &c, size_t lo, size_t hi);

// Match a bracket at `k` AND scan its interior for nested `#`/`%`. Returns the
// index past the close, or NPOS if unbalanced.
size_t ScanBracket(Ctx &c, size_t k, char open, char close) {
    size_t e = MatchBracket(c, k, open, close);
    if (e == NPOS)
        return NPOS;
    ScanRange(c, k + 1, e - 1); // interior tokens (between the brackets)
    return e;
}

// Parse a primary expression at `k`; return index just past it, or NPOS.
size_t ParsePrimary(Ctx &c, size_t k) {
    if (k >= NT(c))
        return NPOS;
    const Token &tk = Tk(c, k);
    if (tk.kind == TK_PUNCT) {
        char ch = c.src[tk.start];
        if (ch == '(')
            return ScanBracket(c, k, '(', ')');
        if (ch == '{')
            return ScanBracket(c, k, '{', '}');
        return NPOS;
    }
    if (tk.kind == TK_STRING || tk.kind == TK_NUMBER)
        return k + 1;
    if (tk.kind == TK_NAME) {
        if (IsKeyword(c.src, tk)) {
            if (SpanEq(c.src, tk, "nil") || SpanEq(c.src, tk, "true") || SpanEq(c.src, tk, "false"))
                return k + 1;
            return NPOS; // function/if/... are not primaries we delimit
        }
        return k + 1;
    }
    return NPOS;
}

// Consume `.name` / `[expr]` / `(args)` / `:name args` / string-call /
// table-call suffixes after a primary. Scans bracketed interiors. Never fails.
size_t ParseSuffixes(Ctx &c, size_t k) {
    while (k < NT(c)) {
        const Token &tk = Tk(c, k);
        if (tk.kind == TK_STRING) { // f"str"
            k++;
            continue;
        }
        if (tk.kind != TK_PUNCT)
            break;
        char ch = c.src[tk.start];
        if (ch == '.' && (tk.end - tk.start) == 1) { // field access `.name` — not `..`/`...`
            if (IsNameNonKw(c, k + 1)) {
                k += 2;
                continue;
            }
            break;
        }
        if (ch == '[') {
            size_t e = ScanBracket(c, k, '[', ']');
            if (e == NPOS)
                break;
            k = e;
            continue;
        }
        if (ch == '(') {
            size_t e = ScanBracket(c, k, '(', ')');
            if (e == NPOS)
                break;
            k = e;
            continue;
        }
        if (ch == '{') { // f{table}
            size_t e = ScanBracket(c, k, '{', '}');
            if (e == NPOS)
                break;
            k = e;
            continue;
        }
        if (ch == ':') {
            if (!IsNameNonKw(c, k + 1))
                break;
            size_t m = k + 2;
            if (PunctIs(c, m, '(')) {
                size_t e = ScanBracket(c, m, '(', ')');
                if (e == NPOS)
                    break;
                k = e;
                continue;
            }
            if (m < NT(c) && Tk(c, m).kind == TK_STRING) {
                k = m + 1;
                continue;
            }
            if (PunctIs(c, m, '{')) {
                size_t e = ScanBracket(c, m, '{', '}');
                if (e == NPOS)
                    break;
                k = e;
                continue;
            }
            break;
        }
        break;
    }
    return k;
}

// Binary operator precedence (higher binds tighter). 0 = not a binary op we
// climb; the ScanRange driver reaches its operands anyway. `..` and `^` are
// right-associative. Comparisons written with `=`/`~` (`<=`,`==`,`~=`) tokenize
// as separate bytes and fall through — the driver still reaches their operands.
int BinPrec(const char *src, const Token &t, bool &rightAssoc) {
    rightAssoc = false;
    if (t.kind == TK_NAME) {
        if (SpanEq(src, t, "or"))
            return 1;
        if (SpanEq(src, t, "and"))
            return 2;
        return 0;
    }
    if (t.kind != TK_PUNCT)
        return 0;
    switch (src[t.start]) {
    case '<':
    case '>':
        return 3;
    case '+':
    case '-':
        return 5;
    case '*':
    case '/':
    case '%':
        return 6;
    case '^':
        rightAssoc = true;
        return 8;
    default:
        return 0;
    }
}

// Parse a full expression at `k` (precedence >= minPrec). Returns the index
// past the expression, or `k` if there is no primary (nothing parsed).
size_t ParseExpr(Ctx &c, size_t k, int minPrec) {
    const size_t start = k;
    DepthGuard guard(c.depth);
    if (c.depth > kMaxParseDepth)
        return start; // too deep — leave this region un-rewritten (see kMaxParseDepth)
    size_t cur;

    if (k < NT(c) && (PunctIs(c, k, '-') || PunctIs(c, k, '#') || NameIs(c, k, "not"))) {
        bool isHash = PunctIs(c, k, '#');
        size_t opTok = k;
        size_t operandEnd = ParseExpr(c, k + 1, 7); // unary binds at 7 (below ^)
        if (operandEnd == k + 1)
            return start; // no operand — leave as-is
        if (isHash && c.lenOn) {
            size_t openOff = Tk(c, opTok).start;
            size_t closeOff = Tk(c, operandEnd - 1).end;
            c.opens.push_back({openOff, "__len(", closeOff - openOff});
            c.closes.push_back({closeOff, closeOff - openOff});
            c.charOps.push_back({openOff, 0}); // drop the '#'
        }
        cur = operandEnd;
    } else {
        size_t p = ParsePrimary(c, k);
        if (p == NPOS)
            return start;
        cur = ParseSuffixes(c, p);
    }

    while (cur < NT(c)) {
        bool ra;
        int prec = BinPrec(c.src, Tk(c, cur), ra);
        if (prec == 0 || prec < minPrec)
            break;
        size_t opTok = cur;
        size_t rstart = cur + 1;
        size_t rend = ParseExpr(c, rstart, prec + (ra ? 0 : 1));
        if (rend == rstart)
            break; // right operand failed — stop (don't consume the op)
        if (c.src[Tk(c, opTok).start] == '%' && c.modOn) {
            size_t openOff = Tk(c, start).start; // left-assoc accumulated left operand
            size_t closeOff = Tk(c, rend - 1).end;
            c.opens.push_back({openOff, "__mod(", closeOff - openOff});
            c.closes.push_back({closeOff, closeOff - openOff});
            c.charOps.push_back({Tk(c, opTok).start, ','}); // '%' -> ','
        }
        cur = rend;
    }
    return cur;
}

// Drive ParseExpr across a token range: parse an expression, jump to its end;
// skip one token when none parses (keywords, `=`, `,`, `;`, ...). Every `#`/`%`
// lives inside some expression the driver reaches (top level or, recursively,
// a bracket interior via ScanBracket).
void ScanRange(Ctx &c, size_t lo, size_t hi) {
    size_t k = lo;
    while (k < hi) {
        size_t e = ParseExpr(c, k, 0);
        k = (e > k) ? e : k + 1;
    }
}

void BuildOutput(Ctx &c, const char *src, size_t len, std::string &out) {
    std::sort(c.opens.begin(), c.opens.end(), [](const Open &a, const Open &b) {
        return a.off != b.off ? a.off < b.off : a.span > b.span; // outer first
    });
    std::sort(c.closes.begin(), c.closes.end(), [](const Close &a, const Close &b) {
        return a.off != b.off ? a.off < b.off : a.span < b.span; // inner first
    });
    std::sort(c.charOps.begin(), c.charOps.end(),
              [](const CharOp &a, const CharOp &b) { return a.off < b.off; });

    out.clear();
    out.reserve(len + c.opens.size() * 7 + c.closes.size());
    size_t oi = 0, ci = 0, hi = 0;
    for (size_t p = 0; p < len; p++) {
        while (ci < c.closes.size() && c.closes[ci].off == p) {
            out.push_back(')');
            ci++;
        }
        while (oi < c.opens.size() && c.opens[oi].off == p) {
            out.append(c.opens[oi].text);
            oi++;
        }
        if (hi < c.charOps.size() && c.charOps[hi].off == p) {
            if (c.charOps[hi].repl)
                out.push_back(c.charOps[hi].repl);
            hi++; // repl == 0 -> delete the byte
        } else {
            out.push_back(src[p]);
        }
    }
    while (ci < c.closes.size() && c.closes[ci].off == len) {
        out.push_back(')');
        ci++;
    }
}

// Rewrite `src` -> `out` using the pre-tokenized `toks`. Returns true and fills
// `out` if anything changed.
bool RewriteAll(const char *src, size_t len, const std::vector<Token> &toks,
                std::string &out) {
    // Gate the (recursive) parse on an actual `#`/`%` OPERATOR token. A `%`
    // inside a format string (`"%d"`) is part of a TK_STRING token, not a punct,
    // so this skips the whole parse for the common format-string-only case
    // instead of parsing the chunk and discarding an empty edit list.
    bool wantLen = false, wantMod = false;
    for (const Token &t : toks) {
        if (t.kind != TK_PUNCT || t.end - t.start != 1)
            continue;
        const char ch = src[t.start];
        if (ch == '#') wantLen = true;
        else if (ch == '%') wantMod = true;
        if (wantLen && wantMod) break;
    }
    wantLen = wantLen && g_lenEnabled;
    wantMod = wantMod && g_modEnabled;
    if (!wantLen && !wantMod)
        return false;

    Ctx c;
    c.src = src;
    c.toks = &toks;
    c.lenOn = wantLen;
    c.modOn = wantMod;
    ScanRange(c, 0, toks.size());
    if (c.opens.empty())
        return false;
    BuildOutput(c, src, len, out);
    return true;
}

// ============================================================================
// Vararg pass: rewrite the `...` EXPRESSION into `unpack(arg)`.
//
// This build is pure Lua 5.0 varargs: `function(...)` works and populates the
// `arg` table (with `arg.n`), but `...` as an expression is a parse error.
// `unpack(arg)` is a faithful, position-independent substitute — it's itself a
// multi-value expression, so it spreads in tail position and truncates to one
// value elsewhere exactly as `...` does, and this build's `unpack(arg)`
// preserves embedded nils via `arg.n` (verified in-game). The ONLY `...` we must
// leave alone is the one in a function's PARAMETER LIST — that's the vararg
// declaration that creates `arg`. Nested vararg functions work for free: each
// `function(...)` makes its own `arg` and `unpack(arg)` binds to the nearest.
//
// Kept a separate pass (not folded into the # / % precedence parser) because it
// needs no precedence — it's a token substitution once the param-list `...` is
// excluded. Order vs RewriteAll is irrelevant (neither produces the other's
// trigger); RunPasses runs this first.
// ============================================================================

bool ContainsTripleDot(const char *src, size_t len) {
    for (size_t i = 0; i + 2 < len; i++)
        if (src[i] == '.' && src[i + 1] == '.' && src[i + 2] == '.')
            return true;
    return false;
}

// From a '(' token at `k`, return the index just past the matching ')', or the
// token count if unbalanced. Parens are always single-char puncts.
size_t MatchParenTok(const std::vector<Token> &t, const char *src, size_t k) {
    int depth = 0;
    for (size_t j = k; j < t.size(); j++) {
        if (t[j].kind != TK_PUNCT || (t[j].end - t[j].start) != 1)
            continue;
        char ch = src[t[j].start];
        if (ch == '(')
            depth++;
        else if (ch == ')') {
            depth--;
            if (depth == 0)
                return j + 1;
        }
    }
    return t.size();
}

bool RewriteVararg(const char *src, size_t len, const std::vector<Token> &toks,
                   std::string &out) {
    const size_t n = toks.size();

    // A `...` is a single 3-char punct token — Tokenize now emits '.', '..',
    // '...' greedily like Lua's lexer, so the vararg is trivially distinct from
    // `.` field access and `..` concat, even written adjacent as `x .. ...`
    // (which is `..` then `...`, not a five-dot run the old scan couldn't split).
    auto isVararg = [&](size_t i) {
        return i < n && toks[i].kind == TK_PUNCT && (toks[i].end - toks[i].start) == 3;
    };
    auto isPunct = [&](size_t i, char ch) {
        return i < n && toks[i].kind == TK_PUNCT &&
               (toks[i].end - toks[i].start) == 1 && src[toks[i].start] == ch;
    };

    // Mark the `...` inside each function's parameter list — the vararg
    // declaration, which must be left intact.
    std::vector<char> skip(n, 0);
    for (size_t i = 0; i < n; i++) {
        if (toks[i].kind != TK_NAME || !SpanEq(src, toks[i], "function"))
            continue;
        // Skip the name path (`foo`, `a.b`, `a.b:c`, or nothing) to the '('.
        size_t j = i + 1;
        while (j < n && !isPunct(j, '(') &&
               (toks[j].kind == TK_NAME || isPunct(j, '.') || isPunct(j, ':')))
            j++;
        if (!isPunct(j, '('))
            continue;
        size_t e = MatchParenTok(toks, src, j);
        for (size_t p = j; p < e; p++) {
            if (isVararg(p)) {
                skip[p] = 1; // a param list has at most one `...` (last param)
                break;
            }
        }
        i = e - 1; // resume at the function body
    }

    // Replace each remaining `...` with `unpack(arg)`.
    std::string result;
    result.reserve(len + 8);
    size_t prev = 0;
    bool any = false;
    for (size_t i = 0; i < n; ++i) {
        if (isVararg(i) && !skip[i]) {
            result.append(src + prev, toks[i].start - prev);
            result.append("unpack(arg)");
            prev = toks[i].end;
            any = true;
        }
    }
    if (!any)
        return false;
    result.append(src + prev, len - prev);
    out = std::move(result);
    return true;
}

// ============================================================================
// Hex-literal pass: rewrite 5.1 `0x…` integer literals to decimal.
//
// Lua 5.0's lexer reads only decimal digits + one `.` + `e` exponent, so a
// `0x…` literal fails to compile (which is why addons resort to
// `tonumber("0x…", 16)`). We convert each hex INTEGER token to the exact
// decimal it denotes — Lua then reads it as the same double 5.1 would. The
// value is unsigned. Hex floats and > 64-bit literals are left untouched (they
// fail to compile as they do today — no regression). Runs before the other
// passes; the tokenizer already skips strings/comments, so `"0xFF"` / `--0xFF`
// are safe, and it never inserts a newline (line numbers preserved).
// ============================================================================

inline bool IsHexDigit(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool ContainsHexPrefix(const char *src, size_t len) {
    for (size_t i = 0; i + 1 < len; i++)
        if (src[i] == '0' && (src[i + 1] == 'x' || src[i + 1] == 'X'))
            return true;
    return false;
}

bool RewriteHex(const char *src, size_t len, const std::vector<Token> &toks,
                std::string &out) {
    out.clear();
    out.reserve(len);
    size_t p = 0;
    bool any = false;
    for (const Token &t : toks) {
        if (t.kind != TK_NUMBER || t.end - t.start < 3)
            continue; // need "0x" + >= 1 digit
        if (src[t.start] != '0')
            continue;
        char x = src[t.start + 1];
        if (x != 'x' && x != 'X')
            continue;
        const size_t nDigits = t.end - (t.start + 2);
        if (nDigits == 0 || nDigits > 16)
            continue; // no digits, or wider than uint64 — leave as-is

        uint64_t val = 0;
        bool pure = true;
        for (size_t i = t.start + 2; i < t.end; i++) {
            unsigned char c = static_cast<unsigned char>(src[i]);
            if (!IsHexDigit(c)) { pure = false; break; } // hex float ('.'/'p') or junk
            val = val * 16 + ((c <= '9') ? (c - '0') : ((c | 0x20) - 'a' + 10));
        }
        if (!pure)
            continue;

        out.append(src + p, t.start - p);
        out.append(std::to_string(val));
        p = t.end;
        any = true;
    }
    if (!any)
        return false;
    out.append(src + p, len - p);
    return true;
}

// ============================================================================
// Long-bracket-level pass: rewrite 5.1 `[=[`…`]=]` / `[==[`…`]==]` (and their
// `--[=[` long-comment forms) into a shape the 5.0 engine accepts.
//
// Lua 5.1 added LEVELED long brackets (a run of `=` between the two `[`, matched
// by count on close). This 5.0 engine has NONE — its lexer reads one char after
// `[`: if it's `[` it's a level-0 long string, otherwise a bare `[` token
// (verified in the engine lexer FUN_006ff610; the `--[[` comment path is the
// same). So `[=[…]=]` fails to compile, and a modern addon that uses a leveled
// bracket (usually to hold text containing `]]`) can't load.
//
// We convert each leveled long bracket, PRESERVING LINE COUNT (no newline is
// added or removed, so error line numbers stay exact):
//   * Long STRING, body free of `[[`/`]]` and not ending in `]`  ->  `[[body]]`
//     (level 0). Byte-exact value, including the engine's leading-newline strip.
//     The `]`-ending / `[[`/`]]`-containing bodies would early-close or nest a
//     level-0 bracket, so they take the quoted path instead.
//   * Long STRING otherwise  ->  a quoted "…" literal: `"`->`\"`, `\`->`\\`,
//     each newline -> `\`+newline (a Lua line continuation: embeds `\n` AND
//     advances the physical line), `\r`->`\r`, NUL->`\0`, other bytes verbatim.
//   * Long COMMENT  ->  blanked to spaces (newlines kept); a comment is
//     whitespace, so this is semantically exact.
//
// Level 0 (`[[…]]`, `--[[…]]`) is left untouched — the engine handles it.
//
// Caveat: the quoted-string form does NOT reproduce 5.1's leading-newline strip
// (a level-≥1 long string that begins with a newline keeps that `\n` in its
// value). Only bodies that fall to the quoted path are affected (they contain
// `[[`/`]]` or end in `]`); the common multi-line case takes the byte-exact
// `[[body]]` path. Line numbers stay exact either way.
//
// Runs FIRST in RunPasses (it changes string/comment boundaries), before the
// shared tokenization the hex / vararg / operator passes use. A false-positive
// `[=` gate (inside a string, say) is harmless: the walker copies non-bracket
// bytes verbatim and returns "unchanged" when it finds no real leveled bracket.
// ============================================================================

// Cheap gate: a leveled long bracket must begin `[=` (`[`+`=`…). Necessary
// prefix for `[=[`, `[==[`, and the `--[=[` comment form.
bool MaybeLeveledBracket(const char *src, size_t len) {
    for (size_t i = 0; i + 1 < len; i++)
        if (src[i] == '[' && src[i + 1] == '=')
            return true;
    return false;
}

// True iff [start,end) is a properly-closed level-`level` long bracket, i.e.
// SkipLongBracket found a real `]`+`=`×level+`]` close (not end-of-buffer).
bool LongBracketClosed(const char *src, size_t start, size_t end, int level) {
    const size_t delim = 2 + static_cast<size_t>(level); // width of `[=…=[` == `]=…=]`
    if (end < start + 2 * delim)
        return false; // no room for both delimiters
    const size_t p = end - delim;
    if (src[p] != ']' || src[end - 1] != ']')
        return false;
    for (int k = 0; k < level; k++)
        if (src[p + 1 + k] != '=')
            return false;
    return true;
}

// Emit the leveled long STRING [start,end) (verified closed) as a 5.0 literal.
void EmitLeveledString(const char *src, size_t start, size_t end, int level,
                       std::string &out) {
    const size_t delim = 2 + static_cast<size_t>(level);
    const size_t cs = start + delim; // content start (past `[=…=[`)
    const size_t ce = end - delim;   // content end (before `]=…=]`)

    // Tier 1: `[[body]]` when body can't early-close or nest a level-0 bracket.
    bool hasDouble = false;
    for (size_t p = cs; p + 1 < ce; p++)
        if ((src[p] == '[' && src[p + 1] == '[') ||
            (src[p] == ']' && src[p + 1] == ']')) {
            hasDouble = true;
            break;
        }
    const bool endsInBracket = (ce > cs) && src[ce - 1] == ']'; // would merge with `]]`
    if (!hasDouble && !endsInBracket) {
        out.append("[[");
        out.append(src + cs, ce - cs);
        out.append("]]");
        return;
    }

    // Tier 2: quoted string — general, preserves line count and byte values.
    out.push_back('"');
    for (size_t p = cs; p < ce; p++) {
        const unsigned char b = static_cast<unsigned char>(src[p]);
        switch (b) {
        case '"':  out.append("\\\""); break;
        case '\\': out.append("\\\\"); break;
        case '\n': out.push_back('\\'); out.push_back('\n'); break; // line continuation
        case '\r': out.append("\\r"); break;
        case '\0': out.append("\\0"); break;
        default:   out.push_back(static_cast<char>(b)); break;
        }
    }
    out.push_back('"');
}

bool RewriteLongBrackets(const char *src, size_t len, std::string &out) {
    out.clear();
    out.reserve(len + 16);
    size_t i = 0;
    bool any = false;
    while (i < len) {
        const char c = src[i];
        if (c == '"' || c == '\'') { // short string — copy verbatim
            const size_t e = SkipShortString(src, len, i);
            out.append(src + i, e - i);
            i = e;
            continue;
        }
        if (c == '-' && i + 1 < len && src[i + 1] == '-') { // comment
            const size_t j = i + 2;
            const int lvl =
                (j < len && src[j] == '[') ? LongBracketLevel(src, len, j) : -1;
            if (lvl > 0) {
                const size_t e = SkipLongBracket(src, len, j, lvl);
                if (LongBracketClosed(src, j, e, lvl)) {
                    for (size_t p = i; p < e; p++) // blank to spaces, keep newlines
                        out.push_back((src[p] == '\n' || src[p] == '\r') ? src[p] : ' ');
                    any = true;
                } else {
                    out.append(src + i, e - i); // unterminated — leave for the engine
                }
                i = e;
                continue;
            }
            if (lvl == 0) { // level-0 long comment — engine handles it
                const size_t e = SkipLongBracket(src, len, j, 0);
                out.append(src + i, e - i);
                i = e;
                continue;
            }
            size_t e = j; // line comment — verbatim to end of line
            while (e < len && src[e] != '\n')
                e++;
            out.append(src + i, e - i);
            i = e;
            continue;
        }
        if (c == '[') { // long string?
            const int lvl = LongBracketLevel(src, len, i);
            if (lvl > 0) {
                const size_t e = SkipLongBracket(src, len, i, lvl);
                if (LongBracketClosed(src, i, e, lvl)) {
                    EmitLeveledString(src, i, e, lvl, out);
                    any = true;
                } else {
                    out.append(src + i, e - i); // unterminated — leave for the engine
                }
                i = e;
                continue;
            }
            if (lvl == 0) { // level-0 long string — engine handles it
                const size_t e = SkipLongBracket(src, len, i, 0);
                out.append(src + i, e - i);
                i = e;
                continue;
            }
            // else: a bare '[' — fall through to the byte copy
        }
        out.push_back(c);
        i++;
    }
    return any;
}

// Run every syntax rewrite over a chunk (hex first, then vararg, then # / %),
// tokenizing ONCE and re-lexing only after a pass actually rewrites the buffer
// (rare) — the passes share one token stream instead of re-lexing per pass.
// Returns true and fills `out` if anything changed; `*outVararg` / `*outOps`
// (optional) report whether the vararg / operator pass fired, for the load-time
// preamble in LoadBuffer_h.
bool RunPasses(const char *src, size_t len, std::string &out, bool *outVararg,
               bool *outOps) {
    if (outVararg) *outVararg = false;
    if (outOps) *outOps = false;
    if (src == nullptr || len == 0)
        return false;

    // Cheap byte gate — skip tokenizing entirely when no trigger char is present
    // (the common case for a chunk with none of `[=` / `0x` / `...` / `#` / `%`).
    const bool maybeLong = g_longBracketEnabled && MaybeLeveledBracket(src, len);
    const bool maybeHex = g_hexEnabled && ContainsHexPrefix(src, len);
    const bool maybeVararg = g_varargEnabled && ContainsTripleDot(src, len);
    const bool maybeOps = (g_lenEnabled && std::memchr(src, '#', len) != nullptr) ||
                          (g_modEnabled && std::memchr(src, '%', len) != nullptr);
    if (!maybeLong && !maybeHex && !maybeVararg && !maybeOps)
        return false;

    const char *cur = src;
    size_t curLen = len;
    std::string lb, hx, va, ops;
    int last = 0; // owns the final buffer: 1 long-bracket, 2 hex, 3 vararg, 4 ops

    // Leveled long brackets first — this rewrites string/comment boundaries, so
    // it must run before the shared tokenization the other passes consume.
    if (maybeLong && RewriteLongBrackets(cur, curLen, lb)) {
        cur = lb.data(); curLen = lb.size(); last = 1;
    }

    std::vector<Token> toks;
    Tokenize(cur, curLen, toks);

    if (maybeHex && RewriteHex(cur, curLen, toks, hx)) {
        cur = hx.data(); curLen = hx.size(); last = 2;
        Tokenize(cur, curLen, toks); // buffer changed — re-lex for the next pass
    }
    if (maybeVararg && RewriteVararg(cur, curLen, toks, va)) {
        cur = va.data(); curLen = va.size(); last = 3;
        if (outVararg) *outVararg = true;
        Tokenize(cur, curLen, toks);
    }
    if (maybeOps && RewriteAll(cur, curLen, toks, ops)) {
        last = 4;
        if (outOps) *outOps = true;
    }

    switch (last) {
    case 1: out = std::move(lb); return true;
    case 2: out = std::move(hx); return true;
    case 3: out = std::move(va); return true;
    case 4: out = std::move(ops); return true;
    default: return false;
    }
}

// Diagnostic entry: the full operator/vararg/hex rewrite of a chunk, without the
// load-time addon-args/helper preamble (that is LoadBuffer_h's concern).
bool RewriteChunk(const char *src, size_t len, std::string &out) {
    return RunPasses(src, len, out, nullptr, nullptr);
}

// ============================================================================
// Runtime helpers: __len (string length / table border) and __mod (5.1 `%`).
// ============================================================================

bool ElemNonNil(void *L, int n) {
    RawGetI(L, 1, n);
    bool nn = Game::Lua::Type(L, -1) != Game::Lua::TYPE_NIL;
    Game::Lua::SetTop(L, -2);
    return nn;
}

double TableBorder(void *L) {
    unsigned i = 0, j = 1;
    while (ElemNonNil(L, static_cast<int>(j))) {
        i = j;
        if (j > 0x7FFFFFFFu / 2) {
            i = 1;
            while (ElemNonNil(L, static_cast<int>(i)))
                i++;
            return static_cast<double>(i - 1);
        }
        j *= 2;
    }
    while (j - i > 1) {
        unsigned m = (i + j) / 2;
        if (ElemNonNil(L, static_cast<int>(m)))
            i = m;
        else
            j = m;
    }
    return static_cast<double>(i);
}

int __fastcall Script_Len(void *L) {
    int ty = Game::Lua::Type(L, 1);
    if (ty == Game::Lua::TYPE_STRING) {
        Game::Lua::PushNumber(L, static_cast<double>(Game::Lua::StrLen(L, 1)));
        return 1;
    }
    if (ty == Game::Lua::TYPE_TABLE) {
        Game::Lua::PushNumber(L, TableBorder(L));
        return 1;
    }
    Game::Lua::Error(L, "attempt to get length of a non-table, non-string value");
    return 0;
}

int __fastcall Script_Mod(void *L) {
    // Lua 5.1 `%`: a - floor(a/b)*b (result takes the sign of b). Distinct from
    // the engine's math.mod, which is C fmod (truncated toward zero).
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "attempt to perform arithmetic (modulo) on a non-number value");
        return 0;
    }
    double a = Game::Lua::ToNumber(L, 1);
    double b = Game::Lua::ToNumber(L, 2);
    Game::Lua::PushNumber(L, a - std::floor(a / b) * b);
    return 1;
}

// Name of the addon whose chunk is currently being loaded — set by
// `LoadBuffer_h` when it injects the addon-args preamble, and ONLY when the
// loadbuffer call came from the immediate-run file funnel (see the
// `_ReturnAddress` gate there). Consumed once by that chunk's `__addonns(...)`
// call — the first thing the chunk runs — so it is empty except during that
// single-chunk window. This is what makes `__addonns` safe to expose as a `_G`
// global: see `Script_AddonNS`.
char g_loadingAddon[128] = "";

// __addonns(name) -> the per-addon shared table (the modern second vararg),
// but ONLY for the addon currently mid-load, and only for its OWN name.
//
// The map itself lives in the Lua registry (unreachable from Lua — this build
// has no debug.getregistry / debug introspection), so `__addonns` is the sole
// Lua-visible door to it. Left ungated, any addon could read another addon's
// private table by name at runtime. So it is context-gated: it answers only
// while `g_loadingAddon` names the caller's own chunk and returns nil otherwise.
// The name arg must also match, so a mid-load chunk cannot ask for a different
// addon's table. The gate is one-shot (cleared on the answer) — the preamble
// calls this exactly once, and nothing else legitimately does.
//
// The grant is armed by `LoadBuffer_h` ONLY when the loadbuffer call comes from
// the immediate-run file funnel (its `_ReturnAddress` is `RET_LUA_FILE_COMPILE`,
// which pcalls the chunk right after compiling). That is the load-bearing
// defense: `loadstring` takes a caller-controlled chunkname (arg 2) and compiles
// WITHOUT running, so without the call-site gate a
// `loadstring("...","@Interface\\AddOns\\Victim\\x")` would arm the grant and
// leak Victim's table. loadstring's loadbuffer call has a different return
// address (it is NOT the file funnel), so it never arms — even when nested
// inside a RunScript body that itself runs through the file funnel. Cross-addon
// runtime access goes through the TOC-gated `C_AddOns.GetAddOnLocalTable`, which
// calls `PushAddonNamespace` directly (not this global) after its own checks.
int __fastcall Script_AddonNS(void *L) {
    const char *name = Game::Lua::ToString(L, 1);
    if (name == nullptr || g_loadingAddon[0] == '\0' || _stricmp(name, g_loadingAddon) != 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    g_loadingAddon[0] = '\0'; // one-shot: consume the load-time grant
    PushAddonNamespace(L, name); // LuaSyntax::PushAddonNamespace (defined below)
    return 1;
}

// ============================================================================
// luaL_loadbuffer co-hook — the universal compile chokepoint.
// ============================================================================

// Extract the addon folder name from a file chunkname like
// "@Interface\AddOns\<Name>\<file>". Returns true + fills `out` (NUL-terminated)
// only for an addon file with a name safe to embed in a "..." literal.
bool AddonNameFromChunk(const char *chunk, char *out, size_t outSize) {
    if (chunk == nullptr)
        return false;
    for (const char *p = chunk; *p; ++p) {
        // "addons" then a path separator (either slash). _strnicmp stops at a
        // NUL, so a short tail near end-of-string just mismatches.
        if (_strnicmp(p, "addons", 6) != 0)
            continue;
        const char sep = p[6];
        if (sep != '\\' && sep != '/') // match "addons\" OR "addons/"
            continue;
        const char *ns = p + 7;
        size_t k = 0;
        while (ns[k] != '\0' && ns[k] != '\\' && ns[k] != '/')
            ++k;
        if (k == 0 || k + 1 > outSize)
            return false;
        for (size_t j = 0; j < k; ++j) {
            char c = ns[j];
            if (c == '"' || c == '\\' || c == '\n' || c == '\r')
                return false; // would break the injected string literal
        }
        std::memcpy(out, ns, k);
        out[k] = '\0';
        return true;
    }
    return false;
}

using LoadBuffer_t = int(__fastcall *)(void *L, const char *buff, unsigned size,
                                       const char *name);
LoadBuffer_t g_origLoadBuffer = nullptr;

int __fastcall LoadBuffer_h(void *L, const char *buff, unsigned size, const char *name) {
    // Who called luaL_loadbuffer? The addon-args grant may be armed only for the
    // immediate-run file funnel (FUN_00704AE0 pcalls right after compiling), so
    // the preamble consumes the grant before anything else runs. `loadstring`
    // compiles WITHOUT running and takes a caller-forgeable chunkname — its call
    // has a different return address, so it can never arm the grant (even nested
    // inside a RunScript that itself runs through the file funnel).
    const bool fromFileFunnel =
        reinterpret_cast<uintptr_t>(_ReturnAddress()) == Offsets::RET_LUA_FILE_COMPILE;

    if (buff == nullptr || size == 0)
        return g_origLoadBuffer(L, buff, size, name);

    // Precompiled bytecode (Lua's ESC "Lua" chunk signature) is NOT source —
    // the rewrite passes would tokenize its bytes as text and corrupt it, and
    // the 5.0 undump is unhardened. Pass it straight through untouched.
    if (static_cast<unsigned char>(buff[0]) == 0x1B)
        return g_origLoadBuffer(L, buff, size, name);

    // Syntax transpile: hex literals first, then vararg, then # / % — one shared
    // tokenization (see RunPasses). `didVararg` / `didOps` drive the preamble.
    std::string transpiled;
    bool didVararg = false, didOps = false;
    const bool changed = RunPasses(buff, size, transpiled, &didVararg, &didOps);
    const char *body = changed ? transpiled.data() : buff;
    size_t bodyLen = changed ? transpiled.size() : size;

    // Assemble a newline-free preamble (line numbers preserved). Two parts:
    //  * Helper capture. `#`/`%`/`...` lower to `__len`/`__mod`/`unpack` CALLS;
    //    as bare globals those resolve nil under a `setfenv` sandbox with no
    //    `_G` fall-through, whereas real 5.1 operators are environment-
    //    independent. Capturing them at chunk top — default env, before any
    //    setfenv the chunk runs — into chunk-locals restores that: nested
    //    functions bind them as upvalues, unaffected by their own environment.
    //  * Addon-args. For an addon FILE chunk, hand it the modern (addonName,
    //    addonTable) varargs via a chunk-local `arg`, so file-scope
    //    `local name, ns = ...` (now `unpack(arg)`) resolves. Vanilla never
    //    passed these — 1.12's main chunk has no `arg`. Only when a real `...`
    //    is present (didVararg), the compile is the file funnel's own
    //    (fromFileFunnel — NOT loadstring, whose chunkname is caller-forgeable),
    //    and the chunkname is an addon path.
    std::string pre;
    if (didOps)
        pre += "local __len,__mod=__len,__mod;";
    if (didVararg)
        pre += "local unpack=unpack;";

    bool armed = false;
    char addon[128];
    if (didVararg) {
        if (fromFileFunnel && AddonNameFromChunk(name, addon, sizeof addon)) {
            pre += "local arg={\"";
            pre += addon;
            pre += "\",__addonns(\"";
            pre += addon;
            pre += "\"),n=2};";
            armed = true;
        } else {
            // Top-level `...` OUTSIDE the addon file funnel — RunScript (`/run`),
            // XML <OnLoad>, or loadstring. Vanilla's main / RunScript chunk is
            // NOT vararg, so `arg` is nil there and the `unpack(arg)` we emit
            // would throw where 5.1's `...` yields nothing. Bind a chunk-local
            // empty vararg table so those chunks match 5.1 (top-level `...`
            // yields nothing). A vararg function's own `arg` local shadows this
            // at its own scope, so nested `function(...)` bodies are unaffected.
            //
            // Limitation (verified in-game): a loadstring'd chunk CALLED with
            // args — `loadstring("return ...")(a,b)` — can't recover them. This
            // build does not populate `arg` for a called chunk, and `...` is not
            // a real 5.0 expression, so there is no source for the call args.
            // Not a regression: without the rewrite that chunk fails to compile
            // outright. This case is vanishingly rare in addons.
            pre += "local arg={n=0};";
        }
    }

    if (pre.empty()) {
        // No operator/vararg rewrite needing a preamble (hex-only, or nothing).
        if (body != buff)
            return g_origLoadBuffer(L, body, static_cast<unsigned>(bodyLen), name);
        return g_origLoadBuffer(L, buff, size, name);
    }

    const size_t bom = (bodyLen >= 3 && static_cast<unsigned char>(body[0]) == 0xEF &&
                        static_cast<unsigned char>(body[1]) == 0xBB &&
                        static_cast<unsigned char>(body[2]) == 0xBF)
                           ? 3
                           : 0;
    std::string full;
    full.reserve(bodyLen + pre.size() + 4);
    full.append(body, bom); // keep a leading BOM ahead of the preamble
    full += pre;
    full.append(body + bom, bodyLen - bom);

    // Grant this chunk (and only this chunk) the right to fetch its own
    // namespace via `__addonns`; the preamble consumes the grant as the chunk's
    // first act, so the window is a single uninterrupted chunk. Revoke on
    // compile failure so no stale grant leaks to a later runtime call. `addon`
    // is a validated name <= 127 chars, so it fits g_loadingAddon exactly.
    if (armed)
        std::memcpy(g_loadingAddon, addon, std::strlen(addon) + 1);
    const int rc =
        g_origLoadBuffer(L, full.data(), static_cast<unsigned>(full.size()), name);
    if (armed && rc != 0)
        g_loadingAddon[0] = '\0';
    return rc;
}

// ============================================================================
// Diagnostics / toggles.
// ============================================================================

int __fastcall Script_TranspileLength(void *L) {
    const char *s = Game::Lua::ToString(L, 1);
    if (s == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    unsigned n = Game::Lua::StrLen(L, 1);
    std::string out;
    if (RewriteChunk(s, n, out))
        Game::Lua::PushLString(L, out.data(), static_cast<unsigned>(out.size()));
    else
        Game::Lua::PushLString(L, s, n);
    return 1;
}

// The transpiler's runtime switches, table-driven. These are diagnostics /
// kill-switches: a `#`/`%`/`...`-bearing chunk can't compile on 5.0 unless
// rewritten, so disabling a switch only reverts affected chunks to the broken
// (un-transpiled) state — useful to answer "is ClassicAPI's rewrite breaking
// this addon?" without shipping a config surface. Lua sees one Set + one Get
// keyed by name (case-insensitive), instead of a Set/Get pair per switch.
struct Toggle { const char *name; bool *flag; };
const Toggle kToggles[] = {
    {"Length", &g_lenEnabled},
    {"Modulo", &g_modEnabled},
    {"VarargExpansion", &g_varargEnabled},
    {"HexLiterals", &g_hexEnabled},
    {"LongBrackets", &g_longBracketEnabled},
};
bool *FindToggle(const char *name) {
    if (name)
        for (const auto &t : kToggles)
            if (_stricmp(name, t.name) == 0)
                return t.flag;
    return nullptr;
}

// _classicapi_SetTranspileOption(name, bool) -> the new value, or nil if the
// name is unknown.
int __fastcall Script_SetTranspileOption(void *L) {
    bool *flag = FindToggle(Game::Lua::ToString(L, 1));
    if (flag == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    *flag = Game::Lua::ToBoolean(L, 2) != 0;
    Game::Lua::PushBool(L, *flag);
    return 1;
}
// _classicapi_GetTranspileOption(name) -> bool, or nil if the name is unknown.
int __fastcall Script_GetTranspileOption(void *L) {
    bool *flag = FindToggle(Game::Lua::ToString(L, 1));
    if (flag == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushBool(L, *flag);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("__len", &Script_Len);
    Game::Lua::RegisterGlobalFunction("__mod", &Script_Mod);
    Game::Lua::RegisterGlobalFunction("__addonns", &Script_AddonNS);
    Game::Lua::RegisterGlobalFunction("_classicapi_TranspileLength", &Script_TranspileLength);
    Game::Lua::RegisterGlobalFunction("_classicapi_SetTranspileOption", &Script_SetTranspileOption);
    Game::Lua::RegisterGlobalFunction("_classicapi_GetTranspileOption", &Script_GetTranspileOption);
}

// `__len` / `__mod` also on the glue state — the load hook is state-agnostic,
// so a glue chunk with `#`/`%` (none ship today) still resolves the helpers.
void RegisterGlueFunctions() {
    Game::Lua::RegisterGlueFunction("__len", &Script_Len);
    Game::Lua::RegisterGlueFunction("__mod", &Script_Mod);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::GlueModuleAutoRegister _glueAutoreg{&RegisterGlueFunctions};
const Game::HookAutoRegister _loadHook{
    Offsets::FUN_LUAL_LOADBUFFER, reinterpret_cast<void *>(&LoadBuffer_h),
    reinterpret_cast<void **>(&g_origLoadBuffer)};

} // namespace

// The one place the per-addon namespace map is read/created (see
// AddonNamespace.h). Registry key is local — nothing else may touch this map,
// so both callers (the `__addonns` preamble global and the gated
// `C_AddOns.GetAddOnLocalTable`) resolve to the same table per addon.
void PushAddonNamespace(void *L, const char *name) {
    static constexpr const char *kAddonNsRegKey = "__classicapi_addon_ns";

    // Case-fold the map key. Addon folder names are case-insensitive on
    // Windows, but the load-time producer keys by the folder's exact case
    // (from the chunkname) while `C_AddOns.GetAddOnLocalTable` passes the
    // caller's arbitrary case — its gates (by-name IsLoaded, TOC read) are both
    // case-insensitive too. Without folding, a case-mismatched request would
    // miss the real table and silently mint a fresh empty one. Fold both here,
    // the single choke point, so every caller lands on the same table.
    char key[128];
    size_t ki = 0;
    for (; name[ki] != '\0' && ki + 1 < sizeof key; ++ki)
        key[ki] = (name[ki] >= 'A' && name[ki] <= 'Z') ? static_cast<char>(name[ki] + 32)
                                                        : name[ki];
    key[ki] = '\0';

    // registry[kAddonNsRegKey] — the name->ns map, created on first use.
    Game::Lua::PushString(L, kAddonNsRegKey);
    Game::Lua::RawGet(L, Game::Lua::REGISTRY_INDEX);
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::SetTop(L, -2); // pop the non-table
        Game::Lua::NewTable(L);
        Game::Lua::PushString(L, kAddonNsRegKey);
        Game::Lua::PushValue(L, -2); // dup map
        Game::Lua::RawSet(L, Game::Lua::REGISTRY_INDEX);
    }
    // map on top; fetch map[key], creating a fresh ns if missing.
    Game::Lua::PushString(L, key);
    Game::Lua::RawGet(L, -2);
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::SetTop(L, -2); // pop nil
        Game::Lua::NewTable(L);
        Game::Lua::PushString(L, key);
        Game::Lua::PushValue(L, -2); // dup ns
        Game::Lua::RawSet(L, -4);    // map[key] = ns
    }
    // Drop the map beneath the ns so the net effect is +1 (ns on top).
    Game::Lua::Remove(L, -2);
}

} // namespace LuaSyntax
