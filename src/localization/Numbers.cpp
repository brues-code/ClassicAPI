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

// Retail's number formatters (LocalizationDocumentation.lua):
// `BreakUpLargeNumbers`, `AbbreviateNumbers`, `AbbreviateLargeNumbers` and
// `C_StringUtil.GetDefaultAbbreviationBreakpoints`. Retail moved them from
// FrameXML into C (12.0.0) and that C is not available, so what follows is
// retail's behavior as probed in an enUS retail client (2026-10-30).
//
// BreakUpLargeNumbers(n [, natural])
//   - A whole number, or one 1000 or more in size, loses its fraction (toward
//     zero) and gets LARGE_NUMBER_SEPERATOR every three digits while the
//     breakUpLargeNumbers cvar is on: 1234567.891 -> "1,234,567",
//     1234567890 -> "1,234,567,890", -1234.5 -> "-1,234"; cvar off ->
//     "1234567".
//   - Any other number keeps one decimal, its tenths rounded half away from
//     zero, ungrouped: 999.96 -> "1000.0", 0.25 -> "0.3", -5.55 -> "-5.6",
//     5.04 -> "5.0", 123.456 -> "123.5".
//   - natural: a value of a million or more becomes the largest number word
//     that fits, with one decimal below 100, truncated, and ".0" dropped:
//     1290000 -> "1.2 million", 12345678 -> "12.3 million", 99990000 ->
//     "99.9 million", 123456789 -> "123 million", 2000000 -> "2 million",
//     1.5e13 -> "15 trillion". Anything smaller, and every negative, is the
//     whole number grouped: 0.5 -> "0", 999999 -> "999,999", -1234567 ->
//     "-1,234,567".
//     The Asian locales' words are 10^4-based (SECOND_NUMBER is 万), so their
//     scales are 1e4 / 1e8 / 1e12. That is read off the strings, not probed.
// AbbreviateNumbers(n [, options])
//   - The first breakpoint n reaches gives floor(n / significandDivisor) /
//     fractionDivisor, written as Lua writes a number, then the abbreviation:
//     1234 -> "1.2K", 12345 -> "12K", 1999999 -> "1.9M". Below every
//     breakpoint, so for every negative, n is written as is: 999.9 ->
//     "999.9", -12345 -> "-12345", 0 -> "0".
//   - The abbreviation is the global string it names unless
//     abbreviationIsGlobal is false (12.3h from {100, "h", 10, 10}); a name
//     with no global adds nothing. Retail's defaults name THIRD_ and
//     FOURTH_NUMBER_CAP_NO_SPACE, which its clients do not define, so
//     1.5e10 -> "15" and 1.5e12 -> "1.5".
//   - options.breakpointData replaces the defaults. options.locale picks the
//     defaults (the Asian table for koKR, zhCN and zhTW) and falls back to the
//     client's locale: 12345 with locale "zhCN" -> "1.2K".
// AbbreviateLargeNumbers(n [, options])
//   - The same walk over its own defaults, FrameXML's old thresholds
//     (123456 -> "123 K", 12345678 -> "12345 K", 123456789 -> "123 M",
//     1234567890 -> "1234 M"), and BreakUpLargeNumbers(n) below them: 1234.5
//     -> "1,234", -12345 -> "-12,345".
//   - Outside the Asian locales the value is rounded to a whole number: 123456
//     over {100, "h", 10, 10} -> "1235h". The Asian default keeps its
//     decimals: 123456789 with locale "zhCN" -> "1.23 M".
// options.config (CreateAbbreviateConfig's cached object) is not backported.

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>

namespace Localization::Numbers {

namespace {

namespace Lua = Game::Lua;

struct Breakpoint {
    double breakpoint;
    const char *abbreviation;
    double significandDivisor;
    double fractionDivisor;
};

// C_StringUtil.GetDefaultAbbreviationBreakpoints(), as retail returns it.
constexpr Breakpoint kNumberBreakpoints[] = {
    {1e13, "FOURTH_NUMBER_CAP_NO_SPACE", 1e12, 1},
    {1e12, "FOURTH_NUMBER_CAP_NO_SPACE", 1e11, 10},
    {1e10, "THIRD_NUMBER_CAP_NO_SPACE", 1e9, 1},
    {1e9, "THIRD_NUMBER_CAP_NO_SPACE", 1e8, 10},
    {1e7, "SECOND_NUMBER_CAP_NO_SPACE", 1e6, 1},
    {1e6, "SECOND_NUMBER_CAP_NO_SPACE", 1e5, 10},
    {1e4, "FIRST_NUMBER_CAP_NO_SPACE", 1e3, 1},
    {1e3, "FIRST_NUMBER_CAP_NO_SPACE", 1e2, 10},
};
// C_StringUtil.GetDefaultAbbreviationBreakpoints("zhCN").
constexpr Breakpoint kNumberBreakpointsAsian[] = {
    {1e9, "SECOND_NUMBER_CAP_NO_SPACE", 1e8, 1},
    {1e8, "SECOND_NUMBER_CAP_NO_SPACE", 1e7, 10},
    {1e5, "FIRST_NUMBER_CAP_NO_SPACE", 1e4, 1},
    {1e4, "FIRST_NUMBER_CAP_NO_SPACE", 1e3, 10},
};
// AbbreviateLargeNumbers' defaults: FrameXML's digit-count thresholds
// (more than 8 digits -> millions, more than 5 -> thousands).
constexpr Breakpoint kLargeBreakpoints[] = {
    {1e8, "SECOND_NUMBER_CAP", 1e6, 1},
    {1e5, "FIRST_NUMBER_CAP", 1e3, 1},
};
// FrameXML's LocalizeNumberAbbreviation_Asian thresholds: 11+ digits ->
// 亿, 9+ -> 亿 with two decimals, 7+ -> 万.
constexpr Breakpoint kLargeBreakpointsAsian[] = {
    {1e10, "SECOND_NUMBER_CAP", 1e8, 1},
    {1e8, "SECOND_NUMBER_CAP", 1e6, 100},
    {1e6, "FIRST_NUMBER_CAP", 1e4, 1},
};

struct Scale {
    double value;
    const char *word;
};
constexpr Scale kScales[] = {
    {1e12, "FOURTH_NUMBER"},
    {1e9, "THIRD_NUMBER"},
    {1e6, "SECOND_NUMBER"},
};
constexpr Scale kScalesAsian[] = {
    {1e12, "FOURTH_NUMBER"},
    {1e8, "THIRD_NUMBER"},
    {1e4, "SECOND_NUMBER"},
};

CVar::Factory::Handle g_breakUpLargeNumbers = nullptr;

// VAR_LOCALE_INDEX order: enUS, koKR, frFR, deDE, zhCN, zhTW, esES, esMX, ruRU.
bool ClientIsAsian() {
    const int locale = Game::Read<int>(Offsets::VAR_LOCALE_INDEX);
    return locale == 1 || locale == 4 || locale == 5;
}

bool IsAsianLocale(const char *locale) {
    return std::strcmp(locale, "koKR") == 0 || std::strcmp(locale, "zhCN") == 0 ||
           std::strcmp(locale, "zhTW") == 0;
}

// `_G[name]` when it is a string, else "".
std::string GlobalString(void *L, const char *name) {
    Lua::PushString(L, name);
    Lua::GetTable(L, Lua::GLOBALS_INDEX);
    std::string s;
    if (Lua::Type(L, -1) == Lua::TYPE_STRING)
        s = Lua::ToString(L, -1);
    Lua::SetTop(L, -2);
    return s;
}

// A number as Lua's tostring writes it.
std::string LuaNumber(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.14g", v);
    return buf;
}

std::string Whole(double w) {
    char buf[400];
    std::snprintf(buf, sizeof buf, "%.0f", w);
    return buf;
}

// The whole number `w` (>= 0), grouped while the cvar is on.
std::string Grouped(void *L, double w) {
    const std::string digits = Whole(w);
    if (g_breakUpLargeNumbers != nullptr && CVar::Factory::GetInt(g_breakUpLargeNumbers, 1) == 0)
        return digits;
    const std::string sep = GlobalString(L, "LARGE_NUMBER_SEPERATOR");
    std::string out;
    const size_t n = digits.size();
    for (size_t i = 0; i < n; ++i) {
        if (i > 0 && (n - i) % 3 == 0)
            out += sep;
        out += digits[i];
    }
    return out;
}

// `v` truncated toward zero and grouped.
std::string SignedWhole(void *L, double v) {
    const double w = std::floor(std::fabs(v));
    return (v < 0 && w != 0 ? "-" : "") + Grouped(L, w);
}

std::string BreakUp(void *L, double v, bool natural) {
    if (natural) {
        const bool asian = ClientIsAsian();
        for (const Scale &s : asian ? kScalesAsian : kScales) {
            if (v < s.value)
                continue;
            const double tenths = std::floor(v / (s.value / 10));
            const double whole = std::floor(tenths / 10);
            std::string num = Whole(whole);
            const double digit = tenths - whole * 10;
            if (tenths < 1000 && digit != 0)
                num += GlobalString(L, "DECIMAL_SEPERATOR") + Whole(digit);
            return num + " " + GlobalString(L, s.word);
        }
        return SignedWhole(L, v);
    }
    const double a = std::fabs(v);
    if (a >= 1000 || a == std::floor(a))
        return SignedWhole(L, v);
    const double tenths = std::floor(a * 10 + 0.5);
    const double whole = std::floor(tenths / 10);
    return (v < 0 ? "-" : "") + Whole(whole) + GlobalString(L, "DECIMAL_SEPERATOR") +
           Whole(tenths - whole * 10);
}

// One breakpoint's result for `v`, which reached it.
std::string Abbreviate(double v, double significandDivisor, double fractionDivisor,
                       const std::string &label, bool roundWhole) {
    double q = std::floor(v / significandDivisor) / fractionDivisor;
    if (roundWhole)
        q = std::floor(q + 0.5);
    return LuaNumber(q) + label;
}

// The options table's fields this module reads.
struct Options {
    bool asian;
    int breakpointData; // stack index of options.breakpointData, 0 when absent
};

// Reads the options at stack index 2; may push options.breakpointData.
Options ReadOptions(void *L) {
    Options o{ClientIsAsian(), 0};
    if (Lua::Type(L, 2) != Lua::TYPE_TABLE)
        return o;
    Lua::PushString(L, "locale");
    Lua::GetTable(L, 2);
    if (Lua::Type(L, -1) == Lua::TYPE_STRING)
        o.asian = IsAsianLocale(Lua::ToString(L, -1));
    Lua::SetTop(L, -2);
    Lua::PushString(L, "breakpointData");
    Lua::GetTable(L, 2);
    if (Lua::Type(L, -1) == Lua::TYPE_TABLE)
        o.breakpointData = Lua::GetTop(L);
    else
        Lua::SetTop(L, -2);
    return o;
}

double FieldNumber(void *L, int table, const char *key, const char *fn) {
    Lua::PushString(L, key);
    Lua::GetTable(L, table);
    if (!Lua::IsNumber(L, -1))
        Lua::Error(L, "%s: breakpointData entry needs a number '%s'", fn, key);
    const double v = Lua::ToNumber(L, -1);
    Lua::SetTop(L, -2);
    return v;
}

// Walks options.breakpointData; true with `out` set at the first entry `v`
// reaches.
bool WalkData(void *L, int data, double v, bool roundWhole, const char *fn, std::string &out) {
    for (int i = 1;; ++i) {
        Lua::RawGetI(L, data, i);
        if (Lua::Type(L, -1) == Lua::TYPE_NIL) {
            Lua::SetTop(L, -2);
            return false;
        }
        if (Lua::Type(L, -1) != Lua::TYPE_TABLE)
            Lua::Error(L, "%s: breakpointData entry %d is not a table", fn, i);
        const int entry = Lua::GetTop(L);
        const double breakpoint = FieldNumber(L, entry, "breakpoint", fn);
        if (v >= breakpoint) {
            const double significandDivisor = FieldNumber(L, entry, "significandDivisor", fn);
            const double fractionDivisor = FieldNumber(L, entry, "fractionDivisor", fn);
            Lua::PushString(L, "abbreviation");
            Lua::GetTable(L, entry);
            if (Lua::Type(L, -1) != Lua::TYPE_STRING)
                Lua::Error(L, "%s: breakpointData entry needs a string 'abbreviation'", fn);
            const std::string abbreviation = Lua::ToString(L, -1);
            Lua::PushString(L, "abbreviationIsGlobal");
            Lua::GetTable(L, entry);
            const bool isGlobal = Lua::Type(L, -1) == Lua::TYPE_NIL || Lua::ToBoolean(L, -1);
            out = Abbreviate(v, significandDivisor, fractionDivisor,
                             isGlobal ? GlobalString(L, abbreviation.c_str()) : abbreviation,
                             roundWhole);
            Lua::SetTop(L, entry - 1);
            return true;
        }
        Lua::SetTop(L, entry - 1);
    }
}

template <size_t N>
bool WalkDefaults(void *L, const Breakpoint (&data)[N], double v, bool roundWhole,
                  std::string &out) {
    for (const Breakpoint &b : data) {
        if (v >= b.breakpoint) {
            out = Abbreviate(v, b.significandDivisor, b.fractionDivisor,
                             GlobalString(L, b.abbreviation), roundWhole);
            return true;
        }
    }
    return false;
}

double CheckNumber(void *L, const char *usage) {
    if (!Lua::IsNumber(L, 1))
        Lua::Error(L, "Usage: %s", usage);
    return Lua::ToNumber(L, 1);
}

int __fastcall Script_BreakUpLargeNumbers(void *L) {
    const double v = CheckNumber(L, "BreakUpLargeNumbers(largeNumber [, natural])");
    const std::string s = BreakUp(L, v, Lua::ToBoolean(L, 2) != 0);
    Lua::PushString(L, s.c_str());
    return 1;
}

int __fastcall Script_AbbreviateNumbers(void *L) {
    constexpr const char *kFn = "AbbreviateNumbers";
    const double v = CheckNumber(L, "AbbreviateNumbers(number [, options])");
    const Options o = ReadOptions(L);
    std::string s;
    const bool hit =
        o.breakpointData != 0
            ? WalkData(L, o.breakpointData, v, false, kFn, s)
            : (o.asian ? WalkDefaults(L, kNumberBreakpointsAsian, v, false, s)
                       : WalkDefaults(L, kNumberBreakpoints, v, false, s));
    if (!hit)
        s = LuaNumber(v);
    Lua::PushString(L, s.c_str());
    return 1;
}

int __fastcall Script_AbbreviateLargeNumbers(void *L) {
    constexpr const char *kFn = "AbbreviateLargeNumbers";
    const double v = CheckNumber(L, "AbbreviateLargeNumbers(number [, options])");
    const Options o = ReadOptions(L);
    const bool roundWhole = !o.asian;
    std::string s;
    const bool hit =
        o.breakpointData != 0
            ? WalkData(L, o.breakpointData, v, roundWhole, kFn, s)
            : (o.asian ? WalkDefaults(L, kLargeBreakpointsAsian, v, roundWhole, s)
                       : WalkDefaults(L, kLargeBreakpoints, v, roundWhole, s));
    if (!hit)
        s = BreakUp(L, v, false);
    Lua::PushString(L, s.c_str());
    return 1;
}

int __fastcall Script_GetDefaultAbbreviationBreakpoints(void *L) {
    const bool asian = Lua::Type(L, 1) == Lua::TYPE_STRING ? IsAsianLocale(Lua::ToString(L, 1))
                                                           : ClientIsAsian();
    const Breakpoint *data = asian ? kNumberBreakpointsAsian : kNumberBreakpoints;
    const size_t count = asian ? std::size(kNumberBreakpointsAsian) : std::size(kNumberBreakpoints);
    Lua::NewTable(L);
    for (size_t i = 0; i < count; ++i) {
        Lua::PushNumber(L, static_cast<double>(i + 1));
        Lua::NewTable(L);
        Lua::SetFieldNumber(L, "breakpoint", data[i].breakpoint);
        Lua::SetFieldString(L, "abbreviation", data[i].abbreviation);
        Lua::SetFieldNumber(L, "significandDivisor", data[i].significandDivisor);
        Lua::SetFieldNumber(L, "fractionDivisor", data[i].fractionDivisor);
        Lua::SetFieldBool(L, "abbreviationIsGlobal", true);
        Lua::SetTable(L, -3);
    }
    return 1;
}

const Game::Doc::Field kBreakpointFields[] = {
    Game::Doc::Req("breakpoint", "number", "The smallest value this entry applies to."),
    Game::Doc::Req("abbreviation", "string",
                   "The name of the global string to add, or the text itself."),
    Game::Doc::Req("significandDivisor", "number",
                   "The value is divided by this and rounded down."),
    Game::Doc::Req("fractionDivisor", "number",
                   "The rounded result is divided by this, which leaves its decimals."),
    Game::Doc::Opt("abbreviationIsGlobal", "bool", "true",
                   "False to add the abbreviation text as it is."),
};
const Game::Doc::Structure kBreakpointStructure{"NumberAbbreviationBreakpoint", "Localization",
                                                kBreakpointFields};

const Game::Doc::Field kOptionsFields[] = {
    Game::Doc::Opt("breakpointData", "table", nullptr,
                   "NumberAbbreviationBreakpoint entries, largest first. Replaces the defaults."),
    Game::Doc::Opt("locale", "string", nullptr,
                   "The locale whose defaults to use. koKR, zhCN and zhTW use the Asian ones."),
};
const Game::Doc::Structure kOptionsStructure{"NumberAbbrevOptions", "Localization",
                                             kOptionsFields};

const Game::Doc::Field kBreakUpArgs[] = {
    Game::Doc::Req("largeNumber", "number"),
    Game::Doc::Opt("natural", "bool", "false",
                   "True to write a million or more in words, such as \"1.2 million\"."),
};
const Game::Doc::Field kResultRets[] = {
    Game::Doc::Req("result", "string"),
};
const Game::Doc::Function kBreakUpLargeNumbers{
    "Formats a number with separators between groups of three digits.", kBreakUpArgs,
    kResultRets, "Localization"};

const Game::Doc::Field kAbbreviateArgs[] = {
    Game::Doc::Req("number", "number"),
    Game::Doc::Opt("options", "NumberAbbrevOptions"),
};
const Game::Doc::Function kAbbreviateNumbers{
    "Shortens a number with an abbreviation, such as \"1.2K\".", kAbbreviateArgs, kResultRets,
    "Localization"};
const Game::Doc::Function kAbbreviateLargeNumbers{
    "Shortens a large number to whole thousands or millions, such as \"123 K\".",
    kAbbreviateArgs, kResultRets, "Localization"};

const Game::Doc::Field kDefaultsArgs[] = {
    Game::Doc::Opt("locale", "string", nullptr, "The locale; the client's when not given."),
};
const Game::Doc::Field kDefaultsRets[] = {
    Game::Doc::Req("breakpoints", "table", "NumberAbbreviationBreakpoint entries, largest first."),
};
const Game::Doc::Function kGetDefaultAbbreviationBreakpoints{
    "Returns the breakpoints AbbreviateNumbers uses by default.", kDefaultsArgs, kDefaultsRets};

void RegisterLuaFunctions() {
    g_breakUpLargeNumbers =
        CVar::Factory::Register("breakUpLargeNumbers", "Toggles using commas in large numbers",
                                Offsets::CVAR_CATEGORY_GAME, "1");
    Lua::RegisterGlobalFunction("BreakUpLargeNumbers", &Script_BreakUpLargeNumbers,
                                &kBreakUpLargeNumbers);
    Lua::RegisterGlobalFunction("AbbreviateNumbers", &Script_AbbreviateNumbers,
                                &kAbbreviateNumbers);
    Lua::RegisterGlobalFunction("AbbreviateLargeNumbers", &Script_AbbreviateLargeNumbers,
                                &kAbbreviateLargeNumbers);
    Lua::RegisterTableFunction("C_StringUtil", "GetDefaultAbbreviationBreakpoints",
                               &Script_GetDefaultAbbreviationBreakpoints,
                               &kGetDefaultAbbreviationBreakpoints);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Localization::Numbers
