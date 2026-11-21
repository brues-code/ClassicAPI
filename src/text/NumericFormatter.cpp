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

// `C_StringUtil.CreateNumericRuleFormatter()` and `Enum.NumericRuleFormatRounding`,
// as the 3.80.2 client does them (UIUtil; RTTI NumericFormatter).
//
// THE OBJECTS. A formatter is a reference-counted C++ object; each Lua value
// is a userdata holding one reference. Its class is the value's metatable:
//   - NumericRuleFormatter (CreateNumericRuleFormatter, Copy): AddBreakpoint,
//     SetBreakpoints, GetBreakpoints, ClearBreakpoints, Copy, FormatNumber.
//   - NumericFormatter, the base (Cooldown:GetCountdownFormatter pushes this
//     one for any formatter, 0x140C13520): FormatNumber only.
//   - the other C_StringUtil classes, each in its own file, built here with
//     CreateClass.
// Every class has __gc, __tostring ("<class>: %p" of the formatter), __eq
// (the same formatter, 0x140D56710), __metatable false, and fields:
// `f.foo = 1` stores into one table per formatter (__newindex 0x140D56550),
// shared by every value boxing it; __index reads the methods first. A method
// name or a metatable key is read-only: "Attempted to assign to read-only key
// %s". Copy (0x140D528C0) copies the breakpoints, not the fields.
//
// BREAKPOINTS (readers 0x140D53540 / 0x140D53330). Number fields (threshold,
// step, min, max; a component's div, mod, step) must be finite numbers: a
// wrong type or inf / NaN is "Current Field: [<path>], must be a finite
// number - Usage: ...", the path joined by commas ("components,div").
// threshold and format are required. rounding is read as an int (1.7 is 1)
// from 0 to 2; format as a C string (it ends at a NUL). SetBreakpoints
// (0x140D52FF0) replaces the list, all or nothing, sorted largest threshold
// first by std::sort over reverse iterators on `a.threshold < b.threshold`
// (0x141BA5950): this build's std::sort is the same introsort, so equal
// thresholds keep their order up to 32 breakpoints, as there. AddBreakpoint
// (0x140D521D0) inserts after every breakpoint with the same or a larger
// threshold. 1.12's C API reads a list up to its first nil; 3.80.2 reads it
// by its length and errors on a hole.
//
// FORMATNUMBER (0x141BA5B40 -> 0x141BA5260). The first breakpoint with
// threshold <= value; with none, the value as "%.14g". Its step snaps the
// value (Snap: 0x141BA51F0), then min and max clamp it. Without components
// the value is the one format argument; with them each component makes one:
// divided by div when div > 0, fmod by mod when mod > 0, snapped by its own
// step and rounding. The text is made by the client's own formatter
// (0x1405BA260, ported as FormatText), not string.format.
//
// Every error a method raises is cut to 199 characters, as 3.80.2's method
// wrappers copy it into char[200].

#include "text/NumericFormatter.h"

#include "Game.h"
#include "lua/Arg.h"
#include "lua/Boxed.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace Text::NumericFormatter {

namespace Lua = Game::Lua;

// Enum.NumericRuleFormatRounding.
enum Rounding : int {
    ROUNDING_NEAREST = 0,
    ROUNDING_UP = 1,
    ROUNDING_DOWN = 2,
};

struct Component {
    double div = 0.0;
    double mod = 0.0;
    double step = 0.0;
    int rounding = ROUNDING_NEAREST;
};

struct Breakpoint {
    double threshold = 0.0;
    double step = 0.0;
    int rounding = ROUNDING_NEAREST;
    bool hasMin = false;
    bool hasMax = false;
    double min = 0.0;
    double max = 0.0;
    std::string format;
    std::vector<Component> components;
};

namespace {

struct RuleFormatter : Formatter {
    std::vector<Breakpoint> breakpoints; // largest threshold first

    bool FormatValue(void *L, double value, std::string &out) const override;
};

// The classes' metatables, one each per Lua state.
constexpr const char *kRuleKey = "ClassicAPI.NumericRuleFormatter";
constexpr const char *kBaseKey = "ClassicAPI.NumericFormatter";
// Every formatter class CreateClass has built, so ToFormatter knows them all.
std::vector<const char *> g_classes;

constexpr std::size_t kErrorCap = 199;
constexpr std::size_t kFormatBuffer = 0x400; // 0x141BA5199: the formatter's output buffer

} // namespace

void RaiseCapped(void *L, const char *fmt, ...) {
    char text[kErrorCap + 1];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    Lua::Arg::RaiseMessage(L, text);
}

namespace {

// `x` snapped to a multiple of `step`, as 0x141BA51F0 does it: r = fmod(x,
// step); Nearest adds a step when r >= step / 2, Up when r > 0, Down never,
// and each takes r off. A negative value's remainder is negative, so Nearest
// and Down move it toward zero (-2.6 by 1 is -2). Up of 0.7 by 0.1 gives
// 0.7000000000000001, as the arithmetic does.
double Snap(double x, double step, int rounding) {
    if (!(step > 0.0))
        return x;
    const double r = std::fmod(x, step);
    switch (rounding) {
    case ROUNDING_NEAREST:
        return step * 0.5 <= r ? x - r + step : x - r;
    case ROUNDING_UP:
        return r > 0.0 ? x - r + step : x - r;
    case ROUNDING_DOWN:
        return x - r;
    default:
        return x;
    }
}

double Apply(const Component &c, double v) {
    if (c.div > 0.0)
        v /= c.div;
    if (c.mod > 0.0)
        v = std::fmod(v, c.mod);
    return Snap(v, c.step, c.rounding);
}

// --- the client's formatter (0x1405BA260) --------------------------------------

// Its arguments sit at stack slots as in the Lua state it runs in: the format
// at 2, the numbers from 3. "%N$" picks slot N + 2.
struct FormatArgs {
    const std::string &format;
    const std::vector<double> &numbers;
    std::string error;

    bool Missing(int slot) const { return slot != 2 && (slot < 3 || slot - 3 >= static_cast<int>(numbers.size())); }

    bool TypeError(int slot, const char *expected) {
        char text[96];
        std::snprintf(text, sizeof(text), "bad argument #%d to '?' (%s expected, got %s)", slot, expected,
                      Missing(slot) ? "no value" : "string");
        error = text;
        return false;
    }

    // luaL_checknumber.
    bool Number(int slot, double &out) {
        if (Missing(slot))
            return TypeError(slot, "number");
        if (slot != 2) {
            out = numbers[static_cast<size_t>(slot - 3)];
            return true;
        }
        // The format itself, read as Lua reads a numeric string.
        char *end = nullptr;
        out = std::strtod(format.c_str(), &end);
        while (end != nullptr && (*end == ' ' || (*end >= '\t' && *end <= '\r')))
            ++end;
        if (end == format.c_str() || end == nullptr || *end != '\0')
            return TypeError(slot, "number");
        return true;
    }

    // luaL_checkinteger: lua_tointeger's 64-bit truncation, of which the
    // formatter passes the low 32 bits.
    bool Integer(int slot, int &out) {
        double v;
        if (!Number(slot, v))
            return false;
        const int64_t whole = v > -9223372036854775808.0 && v < 9223372036854775808.0 ? static_cast<int64_t>(v)
                                                                                     : INT64_MIN;
        out = static_cast<int>(static_cast<uint32_t>(static_cast<uint64_t>(whole)));
        return true;
    }

    // luaL_checklstring: a number reads as "%.14g".
    bool String(int slot, std::string &out) {
        if (Missing(slot))
            return TypeError(slot, "string");
        if (slot == 2) {
            out = format;
            return true;
        }
        char text[32];
        std::snprintf(text, sizeof(text), "%.14g", numbers[static_cast<size_t>(slot - 3)]);
        out = text;
        return true;
    }
};

// Appends what SStrPrintf writes into the `budget` bytes left: at most
// budget - 1 characters.
template <typename T> void Append(std::string &out, std::size_t &budget, const char *spec, T value) {
    const int needed = std::snprintf(nullptr, 0, spec, value);
    if (needed <= 0)
        return;
    std::vector<char> text(static_cast<size_t>(needed) + 1);
    std::snprintf(text.data(), text.size(), spec, value);
    const std::size_t n = std::min<std::size_t>(static_cast<size_t>(needed), budget - 1);
    out.append(text.data(), n);
    budget -= n;
}

// The client's formatter: %% %c %d %i %o %u %x %X %e %E %f %g %G %s, %t (as %f)
// and %F / %T (as %f, with the decimal point of the client's locale: always a
// point here), "%N$" for an argument by position, a flag / width / precision
// part of up to 125 characters, the output cut to 1023 characters and ended
// at its first NUL. False with `args.error` set.
bool FormatText(FormatArgs &args, std::string &out) {
    const std::string &format = args.format;
    out.clear();
    std::size_t budget = kFormatBuffer;
    int slot = 2;
    const char *p = format.c_str();
    const char *end = p + format.size();
    while (p < end && budget >= 2) {
        if (*p != '%') {
            out += *p++;
            --budget;
            continue;
        }
        const char *q = p + 1;
        if (*q == '%') {
            out += '%';
            --budget;
            p = q + 1;
            continue;
        }
        if (*q >= '0' && *q <= '9' && q[1] == '$') {
            slot = *q - '0' + 1;
            q += 2;
        }
        ++slot;
        const char *s = q;
        while (*s == '-' || *s == ' ' || *s == '#' || *s == '+' || *s == '0')
            ++s;
        while (*s >= '0' && *s <= '9')
            ++s;
        char c = *s;
        if (c == '.')
            c = *++s;
        if (c == '-')
            ++s;
        while (*s >= '0' && *s <= '9')
            ++s;
        const std::size_t length = static_cast<size_t>(s - q);
        if (length > 0x7D) {
            args.error = "invalid format (width or precision too long)";
            return false;
        }
        std::string spec = "%" + std::string(q, length + 1);
        const char conversion = *s;
        p = s + 1;
        switch (conversion) {
        case 'e':
        case 'E':
        case 'f':
        case 'g':
        case 'G':
        case 't':
        case 'F':
        case 'T': {
            std::replace(spec.begin(), spec.end(), 't', 'f');
            if (conversion == 'F' || conversion == 'T') {
                std::replace(spec.begin(), spec.end(), 'F', 'f');
                std::replace(spec.begin(), spec.end(), 'T', 'f');
            }
            double v;
            if (!args.Number(slot, v))
                return false;
            Append(out, budget, spec.c_str(), v);
            break;
        }
        case 'X':
        case 'o':
        case 'u':
        case 'x':
        case 'd':
        case 'i': {
            int v;
            if (!args.Integer(slot, v))
                return false;
            Append(out, budget, spec.c_str(), v);
            break;
        }
        case 'c': {
            int v;
            if (!args.Integer(slot, v))
                return false;
            out += static_cast<char>(v);
            --budget;
            break;
        }
        case 's': {
            std::string v;
            if (!args.String(slot, v))
                return false;
            Append(out, budget, spec.c_str(), v.c_str());
            break;
        }
        default:
            args.error = "invalid option in `format'";
            return false;
        }
    }
    const std::size_t nul = out.find('\0');
    if (nul != std::string::npos)
        out.resize(nul);
    return true;
}

// ---- reading breakpoints ------------------------------------------------------

// Where a breakpoint failed to read. The argument number counts one per level
// of nesting: a breakpoint's fields are #3 to AddBreakpoint and #4 inside
// SetBreakpoints' list.
struct ReadError {
    enum Kind { KIND_PLAIN, KIND_NOT_FINITE, KIND_OVERFLOW };
    int depth = 0;    // 0: the table itself, 1: one of its fields, ...
    std::string path; // empty when the table itself is wrong
    Kind kind = KIND_PLAIN;
    double value = 0.0; // the value that overflowed an int
};

bool Fail(ReadError &e, int depth, const char *field, ReadError::Kind kind = ReadError::KIND_PLAIN) {
    e.depth = depth;
    e.path = field != nullptr ? field : "";
    e.kind = kind;
    return false;
}

// t[key] as a finite number. A missing key leaves `present` false and `out`
// as it was; a wrong type or inf / NaN is an error. Strings that read as
// numbers count.
bool ReadNumber(void *L, int t, const char *key, double &out, bool &present) {
    Lua::PushString(L, key);
    Lua::GetTable(L, t);
    present = Lua::Type(L, -1) != Lua::TYPE_NIL;
    bool ok = true;
    if (present) {
        const double v = Lua::IsNumber(L, -1) ? Lua::ToNumber(L, -1) : NAN;
        ok = std::isfinite(v);
        if (ok)
            out = v;
    }
    Lua::SetTop(L, -2);
    return ok;
}

// An Enum.NumericRuleFormatRounding value (0x140BB8000): an int, truncated,
// from 0 to 2. Missing keeps the default.
bool ReadRounding(void *L, int t, int &out, ReadError &e, int depth, const char *path) {
    Lua::PushString(L, "rounding");
    Lua::GetTable(L, t);
    const int type = Lua::Type(L, -1);
    const bool isNumber = Lua::IsNumber(L, -1);
    const double v = isNumber ? Lua::ToNumber(L, -1) : 0.0;
    Lua::SetTop(L, -2);
    if (type == Lua::TYPE_NIL)
        return true;
    if (!isNumber)
        return Fail(e, depth, path);
    if (!(v > -2147483649.0 && v < 2147483648.0)) {
        e.value = v;
        return Fail(e, depth, path, ReadError::KIND_OVERFLOW);
    }
    const int whole = static_cast<int>(v);
    if (static_cast<unsigned>(whole) > ROUNDING_DOWN)
        return Fail(e, depth, path);
    out = whole;
    return true;
}

bool ReadComponent(void *L, int t, Component &c, ReadError &e) {
    if (Lua::Type(L, t) != Lua::TYPE_TABLE)
        return Fail(e, 0, nullptr);
    bool present = false;
    if (!ReadNumber(L, t, "div", c.div, present))
        return Fail(e, 1, "components,div", ReadError::KIND_NOT_FINITE);
    if (!ReadNumber(L, t, "mod", c.mod, present))
        return Fail(e, 1, "components,mod", ReadError::KIND_NOT_FINITE);
    if (!ReadNumber(L, t, "step", c.step, present))
        return Fail(e, 1, "components,step", ReadError::KIND_NOT_FINITE);
    return ReadRounding(L, t, c.rounding, e, 1, "components,rounding");
}

// The breakpoint table at `t`. Fields the type does not have are ignored.
bool ReadBreakpoint(void *L, int t, Breakpoint &bp, ReadError &e) {
    if (Lua::Type(L, t) != Lua::TYPE_TABLE)
        return Fail(e, 0, nullptr);
    bool present = false;
    if (!ReadNumber(L, t, "threshold", bp.threshold, present))
        return Fail(e, 1, "threshold", ReadError::KIND_NOT_FINITE);
    if (!present)
        return Fail(e, 1, "threshold");
    if (!ReadNumber(L, t, "step", bp.step, present))
        return Fail(e, 1, "step", ReadError::KIND_NOT_FINITE);
    if (!ReadRounding(L, t, bp.rounding, e, 1, "rounding"))
        return false;
    if (!ReadNumber(L, t, "min", bp.min, bp.hasMin))
        return Fail(e, 1, "min", ReadError::KIND_NOT_FINITE);
    if (!ReadNumber(L, t, "max", bp.max, bp.hasMax))
        return Fail(e, 1, "max", ReadError::KIND_NOT_FINITE);

    Lua::PushString(L, "format");
    Lua::GetTable(L, t);
    const bool isString = Lua::IsString(L, -1);
    if (isString)
        bp.format = Lua::ToString(L, -1); // a C string: it ends at a NUL
    Lua::SetTop(L, -2);
    if (!isString)
        return Fail(e, 1, "format");

    Lua::PushString(L, "components");
    Lua::GetTable(L, t);
    const int list = Lua::GetTop(L);
    const int type = Lua::Type(L, list);
    bool ok = type == Lua::TYPE_NIL || type == Lua::TYPE_TABLE;
    if (!ok)
        Fail(e, 1, "components");
    for (int i = 1; ok && type == Lua::TYPE_TABLE; ++i) {
        Lua::RawGetI(L, list, i);
        if (Lua::Type(L, -1) == Lua::TYPE_NIL)
            break;
        Component c;
        ok = ReadComponent(L, Lua::GetTop(L), c, e);
        Lua::SetTop(L, list);
        if (ok)
            bp.components.push_back(c);
        else
            e.depth += 2; // components (1), then the entry
    }
    Lua::SetTop(L, list - 1);
    return ok;
}

// After every breakpoint with the same or a larger threshold.
bool AddFrom(void *L, RuleFormatter &f, int t, ReadError &e) {
    Breakpoint bp;
    if (!ReadBreakpoint(L, t, bp, e))
        return false;
    const auto at = std::find_if(f.breakpoints.begin(), f.breakpoints.end(),
                                 [&](const Breakpoint &b) { return b.threshold < bp.threshold; });
    f.breakpoints.insert(at, std::move(bp));
    return true;
}

// All or nothing.
bool ReplaceFrom(void *L, RuleFormatter &f, int t, ReadError &e) {
    std::vector<Breakpoint> list;
    for (int i = 1;; ++i) {
        Lua::RawGetI(L, t, i);
        const int entry = Lua::GetTop(L);
        if (Lua::Type(L, entry) == Lua::TYPE_NIL) {
            Lua::SetTop(L, entry - 1);
            break;
        }
        Breakpoint bp;
        const bool ok = ReadBreakpoint(L, entry, bp, e);
        Lua::SetTop(L, entry - 1);
        if (!ok)
            return false;
        list.push_back(std::move(bp));
    }
    std::sort(list.rbegin(), list.rend(),
              [](const Breakpoint &a, const Breakpoint &b) { return a.threshold < b.threshold; });
    f.breakpoints = std::move(list);
    return true;
}

// Raised by the caller after the reader's locals are gone (lua_error does not
// unwind C++ frames).
void RaiseReadError(void *L, int arg, const ReadError &e, const char *usage) {
    const int n = arg + e.depth;
    if (e.kind == ReadError::KIND_OVERFLOW)
        RaiseCapped(L, "integer overflow attempting to store %f", e.value);
    else if (e.path.empty())
        RaiseCapped(L, "bad argument #%d to '?' (Usage: %s)", n, usage);
    else if (e.kind == ReadError::KIND_NOT_FINITE)
        RaiseCapped(L, "bad argument #%d to '?' (Current Field: [%s], must be a finite number - Usage: %s)", n,
                    e.path.c_str(), usage);
    else
        RaiseCapped(L, "bad argument #%d to '?' (Current Field: [%s], Usage: %s)", n, e.path.c_str(), usage);
}

// ---- the methods ----------------------------------------------------------------

RuleFormatter *Self(void *L, const char *usage) {
    auto *f = static_cast<RuleFormatter *>(ToClass(L, 1, kRuleKey));
    if (f == nullptr)
        RaiseCapped(L, "bad argument #1 to '?' (Usage: %s)", usage);
    return f;
}

int __fastcall Script_AddBreakpoint(void *L) {
    constexpr const char *kUsage = "self:AddBreakpoint(breakpoint)";
    RuleFormatter *f = Self(L, kUsage);
    if (f == nullptr)
        return 0;
    ReadError e;
    if (!AddFrom(L, *f, 2, e))
        RaiseReadError(L, 2, e, kUsage);
    return 0;
}

int __fastcall Script_SetBreakpoints(void *L) {
    constexpr const char *kUsage = "self:SetBreakpoints(breakpoints)";
    RuleFormatter *f = Self(L, kUsage);
    if (f == nullptr)
        return 0;
    if (Lua::Type(L, 2) != Lua::TYPE_TABLE) {
        RaiseCapped(L, "bad argument #2 to '?' (Usage: %s)", kUsage);
        return 0;
    }
    ReadError e;
    if (!ReplaceFrom(L, *f, 2, e))
        RaiseReadError(L, 3, e, kUsage);
    return 0;
}

int __fastcall Script_ClearBreakpoints(void *L) {
    RuleFormatter *f = Self(L, "self:ClearBreakpoints()");
    if (f != nullptr)
        f->breakpoints.clear();
    return 0;
}

int __fastcall Script_GetBreakpoints(void *L) {
    RuleFormatter *f = Self(L, "local breakpoints = self:GetBreakpoints()");
    if (f == nullptr)
        return 0;
    Lua::NewTable(L);
    int i = 0;
    for (const Breakpoint &bp : f->breakpoints) {
        Lua::PushNumber(L, ++i);
        Lua::NewTable(L);
        Lua::SetFieldNumber(L, "threshold", bp.threshold);
        Lua::SetFieldNumber(L, "step", bp.step);
        Lua::SetFieldNumber(L, "rounding", bp.rounding);
        if (bp.hasMin)
            Lua::SetFieldNumber(L, "min", bp.min);
        if (bp.hasMax)
            Lua::SetFieldNumber(L, "max", bp.max);
        Lua::PushString(L, "format");
        Lua::PushLString(L, bp.format.data(), static_cast<unsigned int>(bp.format.size()));
        Lua::SetTable(L, -3);
        Lua::PushString(L, "components");
        Lua::NewTable(L);
        int j = 0;
        for (const Component &c : bp.components) {
            Lua::PushNumber(L, ++j);
            Lua::NewTable(L);
            Lua::SetFieldNumber(L, "div", c.div);
            Lua::SetFieldNumber(L, "mod", c.mod);
            Lua::SetFieldNumber(L, "step", c.step);
            Lua::SetFieldNumber(L, "rounding", c.rounding);
            Lua::SetTable(L, -3);
        }
        Lua::SetTable(L, -3);
        Lua::SetTable(L, -3);
    }
    return 1;
}

int __fastcall Script_Copy(void *L) {
    RuleFormatter *f = Self(L, "local copy = self:Copy()");
    if (f == nullptr)
        return 0;
    auto *copy = new RuleFormatter;
    copy->breakpoints = f->breakpoints;
    Push(L, kRuleKey, copy);
    return 1;
}

// Any NumericFormatter (0x140C12C40 checks the class chain).
int __fastcall Script_FormatNumber(void *L) {
    constexpr const char *kUsage = "local formatted = self:FormatNumber(number)";
    Formatter *f = ToFormatter(L, 1);
    if (f == nullptr) {
        RaiseCapped(L, "bad argument #1 to '?' (Usage: %s)", kUsage);
        return 0;
    }
    if (Lua::Type(L, 2) <= Lua::TYPE_NIL) {
        RaiseCapped(L, "bad argument #2 to '?' (Usage: %s)", kUsage);
        return 0;
    }
    const double v = Lua::IsNumber(L, 2) ? Lua::ToNumber(L, 2) : NAN;
    if (!std::isfinite(v)) {
        RaiseCapped(L, "bad argument #2 to '?' (must be a finite number - Usage: %s)", kUsage);
        return 0;
    }
    if (Format(L, f, v) == nullptr) {
        const char *msg = Lua::ToString(L, -1);
        RaiseCapped(L, "NumericFormatter:FormatNumber(): %s", msg != nullptr ? msg : "?");
    }
    return 1;
}

// ---- metamethods ----------------------------------------------------------------

int __fastcall Script_CreateNumericRuleFormatter(void *L) {
    Push(L, kRuleKey, new RuleFormatter);
    return 1;
}

using Lua::Boxed::Method;
const Method kRuleMethods[] = {
    {"AddBreakpoint", &Script_AddBreakpoint},     {"SetBreakpoints", &Script_SetBreakpoints},
    {"GetBreakpoints", &Script_GetBreakpoints},   {"ClearBreakpoints", &Script_ClearBreakpoints},
    {"Copy", &Script_Copy},
};

const Lua::EnumIntegerEntry kRounding[] = {
    {"Nearest", ROUNDING_NEAREST},
    {"Up", ROUNDING_UP},
    {"Down", ROUNDING_DOWN},
};

const Game::Doc::Field kComponentFields[] = {
    Game::Doc::Opt("div", "number", nullptr, "Divides the value by this first, when it is above 0."),
    Game::Doc::Opt("mod", "number", nullptr,
                   "Then takes the remainder of a division by this, when it is above 0."),
    Game::Doc::Opt("step", "number", nullptr, "Then rounds the result to a multiple of this."),
    Game::Doc::Opt("rounding", "NumericRuleFormatRounding", "\"Nearest\"", "How step rounds."),
};
const Game::Doc::Structure kComponentStructure{"NumericRuleFormatComponent", "StringUtil",
                                               kComponentFields};

const Game::Doc::Field kBreakpointFields[] = {
    Game::Doc::Req("threshold", "number", "The smallest value this rule applies to."),
    Game::Doc::Opt("step", "number", nullptr, "Rounds the value to a multiple of this first."),
    Game::Doc::Opt("rounding", "NumericRuleFormatRounding", "\"Nearest\"", "How step rounds."),
    Game::Doc::Opt("min", "number", nullptr, "Raises a smaller rounded value to this."),
    Game::Doc::Opt("max", "number", nullptr, "Lowers a larger rounded value to this."),
    Game::Doc::Req("format", "string",
                   "A format: one specifier, or one for each component. It also takes %t, %F and "
                   "%T for %f, and %1$ to %9$ to pick an argument."),
    Game::Doc::Opt("components", "table", nullptr,
                   "NumericRuleFormatComponent entries, one format argument each."),
};
const Game::Doc::Structure kBreakpointStructure{"NumericRuleFormatBreakpoint", "StringUtil",
                                                kBreakpointFields};

const Game::Doc::Field kCreateRets[] = {
    Game::Doc::Req("formatter", "NumericRuleFormatter"),
};
const Game::Doc::Function kCreateNumericRuleFormatter{
    "Creates a formatter that turns numbers into text by rules for ranges of values.", {},
    kCreateRets};

void RegisterLuaFunctions() {
    void *L = Lua::State();
    CreateClass(L, kRuleKey, "NumericRuleFormatter", kRuleMethods);
    CreateClass(L, kBaseKey, "NumericFormatter", nullptr, 0);
    Lua::RegisterTableFunction("C_StringUtil", "CreateNumericRuleFormatter",
                               &Script_CreateNumericRuleFormatter, &kCreateNumericRuleFormatter);
    Lua::RegisterIntegerEnum("Enum", "NumericRuleFormatRounding", kRounding, "StringUtil");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterLuaFunctions};

// FORMATNUMBER: the first breakpoint with threshold <= value, else "%.14g".
bool RuleFormatter::FormatValue(void * /*L*/, double value, std::string &out) const {
    const Breakpoint *bp = nullptr;
    for (const Breakpoint &b : breakpoints) {
        if (b.threshold <= value) {
            bp = &b;
            break;
        }
    }
    if (bp == nullptr) {
        char text[32];
        std::snprintf(text, sizeof(text), "%.14g", value);
        out = text;
        return true;
    }
    double v = Snap(value, bp->step, bp->rounding);
    if (bp->hasMin && !(bp->min <= v))
        v = bp->min;
    if (bp->hasMax && !(v <= bp->max))
        v = bp->max;
    std::vector<double> numbers;
    if (bp->components.empty())
        numbers.push_back(v);
    for (const Component &c : bp->components)
        numbers.push_back(Apply(c, v));
    FormatArgs args{bp->format, numbers, {}};
    if (FormatText(args, out))
        return true;
    out = args.error;
    return false;
}

} // namespace

Formatter *ToClass(void *L, int idx, const char *key) {
    return static_cast<Formatter *>(Lua::FullUserdata::To(L, idx, key));
}

Formatter *ToFormatter(void *L, int idx) {
    for (const char *key : g_classes) {
        if (Formatter *f = ToClass(L, idx, key))
            return f;
    }
    return nullptr;
}

void Push(void *L, const char *key, Formatter *f) { Lua::FullUserdata::Push(L, key, f); }

void PushBase(void *L, Formatter *f) { Push(L, kBaseKey, f); }

void CreateClass(void *L, const char *key, const char *name, const Method *methods, std::size_t count) {
    if (std::find_if(g_classes.begin(), g_classes.end(),
                     [&](const char *k) { return std::strcmp(k, key) == 0; }) == g_classes.end())
        g_classes.push_back(key);
    std::vector<Method> all(methods, methods + count);
    all.push_back({"FormatNumber", &Script_FormatNumber});
    Lua::FullUserdata::CreateClass(L, key, name, all.data(), all.size());
}

const char *Format(void *L, Formatter *f, double value) {
    std::string text;
    const bool ok = f->FormatValue(L, value, text);
    Lua::PushLString(L, text.data(), static_cast<unsigned int>(text.size()));
    return ok ? Lua::ToString(L, -1) : nullptr;
}

} // namespace Text::NumericFormatter
