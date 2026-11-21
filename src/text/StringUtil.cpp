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

// C_StringUtil's text and number functions, as the 3.80.2 client does them
// (registered by 0x140DE62E0; each Script_ there pcalls an inner function that
// reads its arguments with the generated readers, lua/Arg.h). The formatter
// factories live with their classes.
//
// Arguments typed `string` (EscapeLuaFormatString, EscapeQuotedCodes,
// StripHyperlinks, StripTextureMarkupForLooseFiles) are read up to their first
// NUL; `stringView` ones keep it. Every result is pushed with its length.

#include "Game.h"
#include "lua/Arg.h"
#include "texture/Load.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace Text::StringUtil {

namespace {

namespace Lua = Game::Lua;
namespace Arg = Game::Lua::Arg;

void PushText(void *L, const std::string &s) {
    Lua::PushLString(L, s.data(), static_cast<unsigned int>(s.size()));
}

// ---- the number functions ---------------------------------------------------------

constexpr double kTwo63 = 9223372036854775808.0;

// A double cut to int64 as the x64 client's cvttsd2si does: INT64_MIN when it
// does not fit.
int64_t Trunc64(double d) { return d >= -kTwo63 && d < kTwo63 ? static_cast<int64_t>(d) : INT64_MIN; }

// The x64 client's double -> uint64 conversion: past 2^63 it converts the
// excess and adds the top bit back.
uint64_t TruncUnsigned64(double d) {
    uint64_t high = 0;
    if (d >= kTwo63) {
        d -= kTwo63;
        if (d < kTwo63)
            high = 0x8000000000000000ull;
    }
    return static_cast<uint64_t>(Trunc64(d)) + high;
}

std::string FormatInteger(int64_t v) {
    char text[32];
    std::snprintf(text, sizeof text, "%lli", static_cast<long long>(v));
    return text;
}

// FloorToNearestString (0x140DE4E90): "%lli" of the number cut toward zero,
// so -1.5 is "-1".
int __fastcall Script_FloorToNearestString(void *L) {
    double v = 0.0;
    Arg::FiniteNumber(L, 1, "Usage: local text = C_StringUtil.FloorToNearestString(number)", v);
    PushText(L, FormatInteger(Trunc64(v)));
    return 1;
}

// RoundToNearestString (0x140DE53E0): half away from zero, through the
// unsigned conversion, so a value past 2^63 wraps.
int __fastcall Script_RoundToNearestString(void *L) {
    double v = 0.0;
    Arg::FiniteNumber(L, 1, "Usage: local text = C_StringUtil.RoundToNearestString(number)", v);
    const uint64_t magnitude = v > 0.0 ? TruncUnsigned64(v + 0.5) : TruncUnsigned64(0.5 - v);
    const uint64_t bits = v > 0.0 ? magnitude : 0 - magnitude;
    PushText(L, FormatInteger(static_cast<int64_t>(bits)));
    return 1;
}

// TruncateWhenZero (0x140DE5EA0): "" when |number| < 1e-6, else the number cut
// toward zero (so 0.5 is "0").
int __fastcall Script_TruncateWhenZero(void *L) {
    double v = 0.0;
    Arg::FiniteNumber(L, 1, "Usage: local text = C_StringUtil.TruncateWhenZero(number)", v);
    PushText(L, std::fabs(v) >= 1e-6 ? FormatInteger(Trunc64(v)) : std::string());
    return 1;
}

// ---- escaping -----------------------------------------------------------------------

// The client's UTF-8 decoder (0x1429A9CA0). `len` counts the bytes it read.
// A NUL is 0xFFFFFFFF with len 0. A bad sequence is 0x80000000: a stray or
// unknown lead byte, a byte that is not a continuation (len stops before it),
// or a whole sequence that is 5 or 6 bytes, overlong, a surrogate or past
// U+10FFFF. It reads up to the first NUL at most, as a Lua string ends in one.
uint32_t DecodeUtf8(const unsigned char *p, int &len) {
    constexpr uint32_t kBad = 0x80000000u;
    len = 0;
    const unsigned lead = *p++;
    if (lead == 0)
        return 0xFFFFFFFFu;
    len = 1;
    uint32_t cp = lead;
    int extra = 0;
    if ((lead & 0xFE) == 0xFC) {
        cp = lead & 0x01;
        extra = 5;
    } else if ((lead & 0xFC) == 0xF8) {
        cp = lead & 0x03;
        extra = 4;
    } else if ((lead & 0xF8) == 0xF0) {
        cp = lead & 0x07;
        extra = 3;
    } else if ((lead & 0xF0) == 0xE0) {
        cp = lead & 0x0F;
        extra = 2;
    } else if ((lead & 0xE0) == 0xC0) {
        cp = lead & 0x1F;
        extra = 1;
    } else if ((lead & 0x80) != 0) {
        return kBad;
    }
    for (int i = 0; i < extra; ++i) {
        const unsigned c = *p++;
        if ((c & 0xC0) != 0x80)
            return kBad;
        ++len;
        cp = (c & 0x3F) | (cp << 6);
    }
    if (cp < 0x110000 && len < 5 && cp - 0xD800 > 0x7FF) {
        if (cp > 0xFFFF)
            return cp;
        const int shortest = cp < 0x80 ? 1 : cp < 0x800 ? 2 : 3;
        if (len <= shortest)
            return cp;
    }
    return kBad;
}

// EscapeDecimalNonPrintables (0x141BAB840): each byte of characters 0 to 8, 11,
// 12 and 14 to 31, of a NUL and of a bad UTF-8 sequence becomes "\DDD".
// Tab, line feed, carriage return and everything from 0x20 up stay, 0x7F too.
int __fastcall Script_EscapeDecimalNonPrintables(void *L) {
    Arg::Text text;
    Arg::StringView(L, 1, "Usage: local escapedText = C_StringUtil.EscapeDecimalNonPrintables(text)", text);
    std::string out;
    const auto *p = reinterpret_cast<const unsigned char *>(text.data);
    const auto *end = p + text.size;
    while (p < end) {
        int len = 0;
        const uint32_t cp = DecodeUtf8(p, len);
        if (cp == 0xFFFFFFFFu)
            len = 1;
        const bool escape = cp == 0xFFFFFFFFu || static_cast<int32_t>(cp) < 9 ||
                            (cp < 0x20 && ((0xFFFFD800u >> cp) & 1) != 0);
        for (int i = 0; i < len; ++i) {
            if (!escape) {
                out += static_cast<char>(p[i]);
                continue;
            }
            const unsigned b = p[i];
            out += '\\';
            out += static_cast<char>('0' + b / 100);
            out += static_cast<char>('0' + b / 10 % 10);
            out += static_cast<char>('0' + b % 10);
        }
        p += len;
    }
    PushText(L, out);
    return 1;
}

std::string ReplaceAll(const Arg::Text &text, char find, const char *with) {
    std::string out;
    for (std::size_t i = 0; i < text.size; ++i) {
        if (text.data[i] == find)
            out += with;
        else
            out += text.data[i];
    }
    return out;
}

// EscapeLuaFormatString (0x140DE45E0): every '%' doubled.
int __fastcall Script_EscapeLuaFormatString(void *L) {
    Arg::Text text;
    Arg::String(L, 1, "Usage: local escapedText = C_StringUtil.EscapeLuaFormatString(text)", text);
    PushText(L, ReplaceAll(text, '%', "%%"));
    return 1;
}

// EscapeQuotedCodes (0x140DE4C20): every '|' doubled.
int __fastcall Script_EscapeQuotedCodes(void *L) {
    Arg::Text text;
    Arg::String(L, 1, "Usage: local escaped = C_StringUtil.EscapeQuotedCodes(text)", text);
    PushText(L, ReplaceAll(text, '|', "||"));
    return 1;
}

// EscapeLuaPatterns (0x140DE48A0): '%' before each of % ( ) * + - . ? [ ],
// before '^' only as the first character and '$' only as the last.
int __fastcall Script_EscapeLuaPatterns(void *L) {
    Arg::Text text;
    Arg::StringView(L, 1, "Usage: local escapedText = C_StringUtil.EscapeLuaPatterns(text)", text);
    std::string out;
    for (std::size_t i = 0; i < text.size; ++i) {
        const char c = text.data[i];
        bool escape = std::strchr("%()*+-.?[]", c) != nullptr && c != '\0';
        if (c == '^')
            escape = i == 0;
        else if (c == '$')
            escape = i + 1 == text.size;
        if (escape)
            out += '%';
        out += c;
    }
    PushText(L, out);
    return 1;
}

// ---- trimming and joining -----------------------------------------------------------

// RemoveContiguousSpaces (0x140DE5100): a run of ' ' keeps its first
// maxAllowedSpaces. Only ' ' counts.
int __fastcall Script_RemoveContiguousSpaces(void *L) {
    constexpr const char *kUsage =
        "Usage: local trimmedText = C_StringUtil.RemoveContiguousSpaces(text, maxAllowedSpaces)";
    Arg::Text text;
    uint32_t maxSpaces = 0;
    Arg::StringView(L, 1, kUsage, text);
    Arg::UInt32(L, 2, kUsage, maxSpaces);
    std::string out;
    uint32_t run = 0;
    for (std::size_t i = 0; i < text.size; ++i) {
        const char c = text.data[i];
        if (c != ' ') {
            out += c;
            run = 0;
        } else if (run < maxSpaces) {
            out += ' ';
            ++run;
        }
    }
    PushText(L, out);
    return 1;
}

// WrapString (0x140DE6140): prefix .. infix .. suffix, or "" when infix is
// empty.
int __fastcall Script_WrapString(void *L) {
    constexpr const char *kUsage = "Usage: local text = C_StringUtil.WrapString(infix [, prefix, suffix])";
    Arg::Text infix, prefix, suffix;
    Arg::StringView(L, 1, kUsage, infix);
    Arg::StringView(L, 2, kUsage, prefix, true);
    Arg::StringView(L, 3, kUsage, suffix, true);
    std::string out;
    if (infix.size != 0) {
        out.append(prefix.data != nullptr ? prefix.data : "", prefix.size);
        out.append(infix.data, infix.size);
        out.append(suffix.data != nullptr ? suffix.data : "", suffix.size);
    }
    PushText(L, out);
    return 1;
}

// ---- stripping ----------------------------------------------------------------------

struct StripOptions {
    bool maintainColor = false;
    bool maintainBrackets = false;
    bool stripNewlines = false;
    bool maintainAtlases = false;
    bool maintainTextures = false;
};

// Past the `|<close>` that ends a block starting at `p` (the code letter), or
// at the NUL when there is none.
const char *SkipBlock(const char *p, char close) {
    char c = *p;
    while (c != '\0') {
        const char *next = p + 1;
        if (c == '|' && *next == close)
            return p + 2;
        c = *next;
        p = next;
    }
    return p;
}

// The client's strip (0x142B87090).
std::string StripHyperlinks(const char *s, const StripOptions &o) {
    const std::size_t length = std::strlen(s);
    std::string out;
    bool linkText = false; // between a link's |H...|h and its closing |h
    const char *p = s;
    const char *end = s + length;
    while (p < end && out.size() < length) {
        if (*p != '|') {
            if (!linkText || (*p != '[' && *p != ']'))
                out += *p;
            ++p;
            continue;
        }
        // A kept code writes its '|' and lets the next pass copy the rest.
        bool keep = false;
        switch (p[1]) {
        case 'A':
            keep = o.maintainAtlases;
            if (!keep)
                p = SkipBlock(p + 1, 'a');
            break;
        case 'T':
            keep = o.maintainTextures;
            if (!keep)
                p = SkipBlock(p + 1, 't');
            break;
        case 'W':
            p = SkipBlock(p + 1, 'w');
            break;
        case 'H':
            p = SkipBlock(p + 1, 'h');
            linkText = !o.maintainBrackets;
            break;
        case 'h':
            p += 2;
            linkText = false;
            break;
        case 'C':
        case 'c':
            keep = o.maintainColor;
            if (keep)
                break;
            p += 2;
            if (*p == 'n' || *p == 'N') {
                if (const char *colon = std::strchr(p, ':'))
                    p = colon + 1;
            } else {
                for (int i = 0; i < 8 && *p != '\0'; ++i)
                    ++p;
            }
            break;
        case 'n':
            keep = !o.stripNewlines;
            if (!keep)
                p += 2;
            break;
        case 'r':
            keep = o.maintainColor;
            if (!keep)
                p += 2;
            break;
        case '|':
            out += "||";
            p += 2;
            break;
        default:
            keep = true;
            break;
        }
        if (keep) {
            out += '|';
            ++p;
        }
    }
    return out;
}

// StripHyperlinks (0x140DE56A0): five optional bools, false by default.
int __fastcall Script_StripHyperlinks(void *L) {
    constexpr const char *kUsage = "Usage: local stripped = C_StringUtil.StripHyperlinks(text, options)";
    Arg::Text text;
    StripOptions o;
    Arg::String(L, 1, kUsage, text);
    Arg::Bool(L, 2, kUsage, o.maintainColor, true);
    Arg::Bool(L, 3, kUsage, o.maintainBrackets, true);
    Arg::Bool(L, 4, kUsage, o.stripNewlines, true);
    Arg::Bool(L, 5, kUsage, o.maintainAtlases, true);
    Arg::Bool(L, 6, kUsage, o.maintainTextures, true);
    const std::string copy(text.data, text.size);
    PushText(L, StripHyperlinks(copy.c_str(), o));
    return 1;
}

// Whether a |T texture stays (0x141B73BDC to 0x141B73E52). A path that starts
// with a nonzero integer (after blanks, an optional sign, decimal digits; an
// int32 that clamps when it overflows) is a file ID, kept when it is not
// negative: 3.80.2's loose files are its negative IDs. Any other path stays
// unless it names a loose file, which 3.80.2 asks its file-ID table
// (0x142A20150, an unknown path is 0 and stays) and this client asks Storm.
bool KeepTexture(const std::string &path) {
    std::size_t i = 0;
    while (i < path.size() && (path[i] == ' ' || static_cast<unsigned char>(path[i] - 9) <= 4))
        ++i;
    if (i < path.size()) {
        const char sign = path[i];
        const bool negative = sign == '-';
        std::size_t digits = i + ((sign == '+' || sign == '-') ? 1 : 0);
        const std::size_t first = digits;
        uint32_t value = 0;
        bool overflow = false;
        for (; digits < path.size() && path[digits] >= '0' && path[digits] <= '9'; ++digits) {
            const uint32_t d = static_cast<uint32_t>(path[digits] - '0');
            if (value < 0xCCCCCCCu || (value == 0xCCCCCCCu && d <= 7u + (negative ? 1u : 0u)))
                value = value * 10 + d;
            else
                overflow = true;
        }
        if (digits != first) {
            if (overflow)
                value = negative ? 0x80000000u : 0x7FFFFFFFu;
            else if (negative)
                value = 0 - value;
            if (value != 0)
                return static_cast<int32_t>(value) >= 0;
        }
    }
    return !Texture::IsLooseFile(path.c_str());
}

// The client's strip (0x141B73680). Only |T...|t is looked at; a texture whose
// part before the first ':' is a loose file goes whole. Like the client, a
// |T with no |t after it copies the text before its '|' a second time.
std::string StripLooseTextures(const std::string &s) {
    const std::size_t n = s.size();
    std::string out;
    std::size_t start = 0;
    for (;;) {
        std::size_t bar = start;
        while (bar < n && s[bar] != '|')
            ++bar;
        if (bar >= n || bar + 1 >= n)
            break;
        out.append(s, start, bar - start);
        if (s[bar + 1] != 'T') {
            out += '|';
            start = bar + 1;
            continue;
        }
        std::size_t close = bar + 2;
        while (close + 2 <= n && !(s[close] == '|' && s[close + 1] == 't'))
            ++close;
        if (close + 2 > n)
            break; // start is still before the '|'
        const std::string contents = s.substr(bar + 2, close - bar - 2);
        if (KeepTexture(contents.substr(0, contents.find(':'))))
            out.append(s, bar, close + 2 - bar);
        start = close + 2;
        if (start >= n)
            return out;
    }
    if (start < n)
        out.append(s, start, n - start);
    return out;
}

// StripTextureMarkupForLooseFiles (0x140DE5A90).
int __fastcall Script_StripTextureMarkupForLooseFiles(void *L) {
    Arg::Text text;
    Arg::String(L, 1, "Usage: local stripped = C_StringUtil.StripTextureMarkupForLooseFiles(text)", text);
    PushText(L, StripLooseTextures(std::string(text.data, text.size)));
    return 1;
}

// ---- documentation ------------------------------------------------------------------

using Game::Doc::Opt;
using Game::Doc::Req;

const Game::Doc::Field kTextArg[] = {Req("text", "string")};
const Game::Doc::Field kNumberArg[] = {Req("number", "number")};
const Game::Doc::Field kEscapedTextRet[] = {Req("escapedText", "string")};
const Game::Doc::Field kEscapedRet[] = {Req("escaped", "string")};
const Game::Doc::Field kTextRet[] = {Req("text", "string")};
const Game::Doc::Field kStrippedRet[] = {Req("stripped", "string")};

const Game::Doc::Function kEscapeDecimalNonPrintables{
    "Returns the text with control characters (except tab, line feed and carriage return) and "
    "bytes that are not valid UTF-8 written as decimal escapes, such as \\001.",
    kTextArg, kEscapedTextRet};
const Game::Doc::Function kEscapeLuaFormatString{"Returns the text with every '%' doubled, for use as a format.",
                                                 kTextArg, kEscapedTextRet};
const Game::Doc::Function kEscapeLuaPatterns{
    "Returns the text with every Lua pattern character escaped, for use as a pattern.", kTextArg,
    kEscapedTextRet};
const Game::Doc::Function kEscapeQuotedCodes{"Returns the text with every '|' doubled, so its codes show as text.",
                                             kTextArg, kEscapedRet};
const Game::Doc::Function kFloorToNearestString{
    "Returns the number as an integer string, cut toward zero.", kNumberArg, kTextRet};
const Game::Doc::Function kRoundToNearestString{
    "Returns the number as an integer string, rounded half away from zero.", kNumberArg, kTextRet};
const Game::Doc::Function kTruncateWhenZero{
    "Returns the number as an integer string cut toward zero, or an empty string when it is almost zero.",
    kNumberArg, kTextRet};

const Game::Doc::Field kRemoveContiguousSpacesArgs[] = {
    Req("text", "string"),
    Req("maxAllowedSpaces", "number", "The most spaces kept in a row."),
};
const Game::Doc::Field kTrimmedTextRet[] = {Req("trimmedText", "string")};
const Game::Doc::Function kRemoveContiguousSpaces{
    "Returns the text with each run of spaces cut to at most maxAllowedSpaces.", kRemoveContiguousSpacesArgs,
    kTrimmedTextRet};

const Game::Doc::Field kStripHyperlinksArgs[] = {
    Req("text", "string"),
    Opt("maintainColor", "bool", "false", "Keep |c and |r color codes."),
    Opt("maintainBrackets", "bool", "false", "Keep the [ and ] around link text."),
    Opt("stripNewlines", "bool", "false", "Remove |n line breaks."),
    Opt("maintainAtlases", "bool", "false", "Keep |A...|a atlas markup."),
    Opt("maintainTextures", "bool", "false", "Keep |T...|t texture markup."),
};
const Game::Doc::Function kStripHyperlinks{"Returns the text with hyperlinks and other display codes removed.",
                                           kStripHyperlinksArgs, kStrippedRet};
const Game::Doc::Function kStripTextureMarkupForLooseFiles{
    "Returns the text without the |T...|t textures that come from loose files on disk.", kTextArg,
    kStrippedRet};

const Game::Doc::Field kWrapStringArgs[] = {
    Req("infix", "string"),
    Opt("prefix", "string"),
    Opt("suffix", "string"),
};
const Game::Doc::Function kWrapString{
    "Returns prefix, infix and suffix joined, or an empty string when infix is empty.", kWrapStringArgs,
    kTextRet};

void RegisterLuaFunctions() {
    struct Entry {
        const char *name;
        Lua::CFunction fn;
        const Game::Doc::Function *doc;
    };
    static const Entry kFunctions[] = {
        {"EscapeDecimalNonPrintables", &Script_EscapeDecimalNonPrintables, &kEscapeDecimalNonPrintables},
        {"EscapeLuaFormatString", &Script_EscapeLuaFormatString, &kEscapeLuaFormatString},
        {"EscapeLuaPatterns", &Script_EscapeLuaPatterns, &kEscapeLuaPatterns},
        {"EscapeQuotedCodes", &Script_EscapeQuotedCodes, &kEscapeQuotedCodes},
        {"FloorToNearestString", &Script_FloorToNearestString, &kFloorToNearestString},
        {"RemoveContiguousSpaces", &Script_RemoveContiguousSpaces, &kRemoveContiguousSpaces},
        {"RoundToNearestString", &Script_RoundToNearestString, &kRoundToNearestString},
        {"StripHyperlinks", &Script_StripHyperlinks, &kStripHyperlinks},
        {"StripTextureMarkupForLooseFiles", &Script_StripTextureMarkupForLooseFiles,
         &kStripTextureMarkupForLooseFiles},
        {"TruncateWhenZero", &Script_TruncateWhenZero, &kTruncateWhenZero},
        {"WrapString", &Script_WrapString, &kWrapString},
    };
    for (const Entry &e : kFunctions)
        Lua::RegisterTableFunction("C_StringUtil", e.name, e.fn, e.doc);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterLuaFunctions};

} // namespace

} // namespace Text::StringUtil
