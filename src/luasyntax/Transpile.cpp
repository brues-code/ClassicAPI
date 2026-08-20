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

// Lua 5.1 syntax backport (source-level transpile) — PROTOTYPE.
//
// Vanilla's Lua is 5.0. It lacks the 5.1 operators `#` (length) and `%`
// (modulo), so modern addon ports that use them fail to COMPILE. We don't
// touch the 5.0 parser/VM (there is no OP_LEN / OP_MOD and no free opcode);
// instead we rewrite the SOURCE before it reaches the parser: co-hook
// `luaL_loadbuffer` (the one function every compile funnels through — file
// scripts, `loadstring`, XML `<OnLoad>`), parse the chunk with a small
// Lua-aware precedence-climbing parser, and rewrite:
//     #operand      ->  __len(operand)
//     a % b         ->  __mod(a, b)
// `__len` / `__mod` are C globals we register (see below).
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
//   * Nested long strings/comments (`[[ a [[ b ]] c ]]`, a 5.0-only quirk)
//     use first-close matching, not depth. Real addons ~never nest these.
//   * `__len` on a table returns a border (bisection) = 5.1 `#`; it ignores
//     any `table.setn` count (5.1 has no setn — correct `#` semantics).
//   * Diagnostic `_classicapi_TranspileLength(src)` returns the rewrite.
//   * Toggles `_classicapi_SetLengthOperator(bool)` / `SetModuloOperator`.
//   * `...` is a separate, larger phase (not here).

#include "Game.h"
#include "Offsets.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace LuaSyntax {

namespace {

constexpr size_t NPOS = static_cast<size_t>(-1);

// Runtime switches, default ON — a `#`/`%`-bearing chunk does not compile on
// 5.0 today, so enabling by default cannot regress working addons.
bool g_lenEnabled = true;
bool g_modEnabled = true;

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

// Skip a long string/comment from the opening `[` (`pos`). First close match
// only — no nesting (prototype limit). Returns offset past the close, or len.
size_t SkipLongBracket(const char *src, size_t len, size_t pos, int level) {
    size_t i = pos + 2 + static_cast<size_t>(level);
    while (i < len) {
        if (src[i] == ']') {
            size_t j = i + 1;
            int eq = 0;
            while (j < len && src[j] == '=') {
                j++;
                eq++;
            }
            if (eq == level && j < len && src[j] == ']')
                return j + 1;
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
    bool seenDot = false;
    while (i < len) {
        char c = src[i];
        if (c == '.') {
            if (seenDot)
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
            char p = src[i - 1];
            if (p == 'e' || p == 'E' || p == 'p' || p == 'P') {
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
        if (ch == '.') {
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

// Rewrite `src` -> `out`. Returns true and fills `out` if anything changed.
bool RewriteAll(const char *src, size_t len, std::string &out) {
    if (src == nullptr || len == 0)
        return false;
    bool wantLen = g_lenEnabled && std::memchr(src, '#', len) != nullptr;
    bool wantMod = g_modEnabled && std::memchr(src, '%', len) != nullptr;
    if (!wantLen && !wantMod)
        return false;

    std::vector<Token> toks;
    Tokenize(src, len, toks);

    Ctx c;
    c.src = src;
    c.toks = &toks;
    c.lenOn = wantLen;
    c.modOn = wantMod;
    ScanRange(c, 0, toks.size());
    if (c.opens.empty())
        return false; // e.g. `%` only inside format strings
    BuildOutput(c, src, len, out);
    return true;
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

// ============================================================================
// luaL_loadbuffer co-hook — the universal compile chokepoint.
// ============================================================================

using LoadBuffer_t = int(__fastcall *)(void *L, const char *buff, unsigned size,
                                       const char *name);
LoadBuffer_t g_origLoadBuffer = nullptr;

int __fastcall LoadBuffer_h(void *L, const char *buff, unsigned size, const char *name) {
    if (buff != nullptr && size != 0) {
        std::string out;
        if (RewriteAll(buff, size, out))
            return g_origLoadBuffer(L, out.data(), static_cast<unsigned>(out.size()), name);
    }
    return g_origLoadBuffer(L, buff, size, name);
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
    if (RewriteAll(s, n, out))
        Game::Lua::PushLString(L, out.data(), static_cast<unsigned>(out.size()));
    else
        Game::Lua::PushLString(L, s, n);
    return 1;
}

int __fastcall Script_SetLengthOperator(void *L) {
    g_lenEnabled = Game::Lua::ToBoolean(L, 1) != 0;
    Game::Lua::PushBool(L, g_lenEnabled);
    return 1;
}
int __fastcall Script_GetLengthOperator(void *L) {
    Game::Lua::PushBool(L, g_lenEnabled);
    return 1;
}
int __fastcall Script_SetModuloOperator(void *L) {
    g_modEnabled = Game::Lua::ToBoolean(L, 1) != 0;
    Game::Lua::PushBool(L, g_modEnabled);
    return 1;
}
int __fastcall Script_GetModuloOperator(void *L) {
    Game::Lua::PushBool(L, g_modEnabled);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("__len", &Script_Len);
    Game::Lua::RegisterGlobalFunction("__mod", &Script_Mod);
    Game::Lua::RegisterGlobalFunction("_classicapi_TranspileLength", &Script_TranspileLength);
    Game::Lua::RegisterGlobalFunction("_classicapi_SetLengthOperator", &Script_SetLengthOperator);
    Game::Lua::RegisterGlobalFunction("_classicapi_GetLengthOperator", &Script_GetLengthOperator);
    Game::Lua::RegisterGlobalFunction("_classicapi_SetModuloOperator", &Script_SetModuloOperator);
    Game::Lua::RegisterGlobalFunction("_classicapi_GetModuloOperator", &Script_GetModuloOperator);
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

} // namespace LuaSyntax
