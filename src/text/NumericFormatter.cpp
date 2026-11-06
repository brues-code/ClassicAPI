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

// `C_StringUtil.CreateNumericRuleFormatter()` and `Enum.NumericRuleFormatRounding`
// (StringUtilDocumentation.lua, NumericRuleFormatterAPIDocumentation.lua). The
// formatter is C in retail and no Blizzard Lua uses it, so what follows is
// retail's behavior as probed in a 12.1.5 client (2026-10-31):
//
//   - The object is a userdata, tostring "NumericRuleFormatter: <address>",
//     getmetatable false. Methods: AddBreakpoint, SetBreakpoints,
//     GetBreakpoints, ClearBreakpoints, Copy, FormatNumber.
//   - Breakpoints are kept largest threshold first, equal thresholds in the
//     order added. FormatNumber(v) uses the first one with v >= threshold;
//     below every threshold (or with none) v is written as Lua writes a
//     number: 9.99 -> "9.99", -1 -> "-1".
//   - The breakpoint's step snaps v (Snap below), then min and max clamp it.
//     A step of 0 or less snaps nothing. Without components, v is the one
//     format argument; with them, each component makes one argument from the
//     clamped v: divided by div, then the remainder by mod (C fmod, so -65
//     mod 60 is -5), then snapped by its own step. A div or mod of 0 does
//     nothing. Rounding is decided per breakpoint, never re-decided: 59.5 over
//     {0, step 1, Up} and {60, "%dm"} gives "60", not "1m".
//   - The format runs as string.format does: "%d" truncates (4.7 -> "4"),
//     "%s" writes the number as Lua does, "|c" codes pass through, and an
//     argument the format asks for that is not there is format's error,
//     prefixed "NumericFormatter:FormatNumber(): ". The text goes through
//     1.12's own str_format, called directly so a replaced string.format
//     cannot change it. Retail rejects %q and errors on %d past the int range
//     where str_format quotes and wraps.
//   - GetBreakpoints returns new tables with every field filled in (step 0,
//     rounding 0, components {}, a component's div/mod/step 0), min and max
//     only when set.
//
// Field assignment on the object (retail keeps `f.foo = 1`) is not ported: a
// formatter here has its methods and nothing else.

#include "text/NumericFormatter.h"

#include "Game.h"
#include "Offsets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

struct Formatter {
    int refs = 0;
    std::vector<Breakpoint> breakpoints; // largest threshold first
};

void Retain(Formatter *f) { ++f->refs; }

void Release(Formatter *f) {
    if (f != nullptr && --f->refs == 0)
        delete f;
}

namespace {

// The metatable every formatter object shares, one per Lua state.
constexpr const char *kMetatableKey = "ClassicAPI.NumericRuleFormatter";
// str_format's closure, made once per Lua state: FormatNumber runs from addon
// timers and the countdown every frame, and lua_pushcclosure allocates a new
// closure on each push.
constexpr const char *kFormatKey = "ClassicAPI.NumericRuleFormatter.format";

void PushMetatable(void *L) {
    Lua::PushString(L, kMetatableKey);
    Lua::RawGet(L, Lua::REGISTRY_INDEX);
}

void PushFormat(void *L) {
    Lua::PushString(L, kFormatKey);
    Lua::RawGet(L, Lua::REGISTRY_INDEX);
}

// `x` snapped to a multiple of `step`. The multiple toward zero is
// x - fmod(x, step) and the next one out is a step further; that arithmetic
// is what matches retail to the last bit: Up of 0.7 by 0.1 gives
// 0.7000000000000001 and of 0.6 gives 0.6, where ceil(x / step) * step gives
// 0.6000000000000001 for 0.6. Nearest takes a half up, toward +inf (2.5 -> 3,
// -2.5 -> -2, 7.5 by 5 -> 10).
double Snap(double x, double step, int rounding) {
    if (!(step > 0.0))
        return x;
    const double r = std::fmod(x, step);
    if (r == 0.0)
        return x;
    const double inward = x - r;
    const double outward = r > 0.0 ? inward + step : inward - step;
    const double up = r > 0.0 ? outward : inward;
    const double down = r > 0.0 ? inward : outward;
    switch (rounding) {
    case ROUNDING_UP:
        return up;
    case ROUNDING_DOWN:
        return down;
    default:
        return (r > 0.0 ? 2.0 * r >= step : -2.0 * r <= step) ? up : down;
    }
}

// A div or mod of 0 does nothing (probe). Negative ones are not probed and are
// skipped too, as a step of 0 or less is.
double Apply(const Component &c, double v) {
    if (c.div > 0.0)
        v /= c.div;
    if (c.mod > 0.0)
        v = std::fmod(v, c.mod);
    return Snap(v, c.step, c.rounding);
}

int AbsIndex(void *L, int idx) { return idx > 0 ? idx : Lua::GetTop(L) + idx + 1; }

// ---- reading breakpoints ------------------------------------------------------

// Where a breakpoint failed to read. Retail's error names the field and counts
// one argument per level of nesting: a breakpoint's fields are #3 to
// AddBreakpoint and #4 inside SetBreakpoints' list (probe).
struct ReadError {
    int depth = 0;               // 0: the table itself, 1: one of its fields, ...
    const char *field = nullptr; // null when the table itself is wrong
    bool notFinite = false;
};

bool Fail(ReadError &e, int depth, const char *field, bool notFinite = false) {
    e = ReadError{depth, field, notFinite};
    return false;
}

// t[key] as a number: false when it is set to something else. A missing key
// leaves `present` false and `out` as it was. Strings that read as numbers
// count, as they do for retail ({threshold = "5"} is accepted).
bool ReadNumber(void *L, int t, const char *key, double &out, bool &present) {
    Lua::PushString(L, key);
    Lua::GetTable(L, t);
    present = Lua::Type(L, -1) != Lua::TYPE_NIL;
    const bool ok = !present || Lua::IsNumber(L, -1);
    if (present && ok)
        out = Lua::ToNumber(L, -1);
    Lua::SetTop(L, -2);
    return ok;
}

// An Enum.NumericRuleFormatRounding value, or missing (probe: 7 is an error).
bool ReadRounding(void *L, int t, int &out) {
    double v = 0.0;
    bool present = false;
    if (!ReadNumber(L, t, "rounding", v, present))
        return false;
    if (!present)
        return true;
    if (v != ROUNDING_NEAREST && v != ROUNDING_UP && v != ROUNDING_DOWN)
        return false;
    out = static_cast<int>(v);
    return true;
}

bool ReadComponent(void *L, int t, Component &c, ReadError &e) {
    if (Lua::Type(L, t) != Lua::TYPE_TABLE)
        return Fail(e, 0, nullptr);
    bool present = false;
    if (!ReadNumber(L, t, "div", c.div, present))
        return Fail(e, 1, "div");
    if (!ReadNumber(L, t, "mod", c.mod, present))
        return Fail(e, 1, "mod");
    if (!ReadNumber(L, t, "step", c.step, present))
        return Fail(e, 1, "step");
    if (!ReadRounding(L, t, c.rounding))
        return Fail(e, 1, "rounding");
    return true;
}

// The breakpoint table at `t`. Fields the type does not have are ignored.
bool ReadBreakpoint(void *L, int t, Breakpoint &bp, ReadError &e) {
    if (Lua::Type(L, t) != Lua::TYPE_TABLE)
        return Fail(e, 0, nullptr);
    bool present = false;
    if (!ReadNumber(L, t, "threshold", bp.threshold, present) || !present)
        return Fail(e, 1, "threshold");
    if (!std::isfinite(bp.threshold))
        return Fail(e, 1, "threshold", true);
    if (!ReadNumber(L, t, "step", bp.step, present))
        return Fail(e, 1, "step");
    if (!ReadRounding(L, t, bp.rounding))
        return Fail(e, 1, "rounding");
    if (!ReadNumber(L, t, "min", bp.min, bp.hasMin))
        return Fail(e, 1, "min");
    if (!ReadNumber(L, t, "max", bp.max, bp.hasMax))
        return Fail(e, 1, "max");

    Lua::PushString(L, "format");
    Lua::GetTable(L, t);
    const bool isString = Lua::IsString(L, -1);
    if (isString)
        bp.format.assign(Lua::ToString(L, -1), Lua::StrLen(L, -1));
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
bool AddFrom(void *L, Formatter &f, int t, ReadError &e) {
    Breakpoint bp;
    if (!ReadBreakpoint(L, t, bp, e))
        return false;
    const auto at = std::find_if(f.breakpoints.begin(), f.breakpoints.end(),
                                 [&](const Breakpoint &b) { return b.threshold < bp.threshold; });
    f.breakpoints.insert(at, std::move(bp));
    return true;
}

// All or nothing.
bool ReplaceFrom(void *L, Formatter &f, int t, ReadError &e) {
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
    std::stable_sort(list.begin(), list.end(),
                     [](const Breakpoint &a, const Breakpoint &b) { return a.threshold > b.threshold; });
    f.breakpoints = std::move(list);
    return true;
}

// Raised by the caller after the reader's locals are gone (lua_error does not
// unwind C++ frames).
void RaiseReadError(void *L, int arg, const ReadError &e, const char *usage) {
    const int n = arg + e.depth;
    if (e.field == nullptr)
        Lua::Error(L, "bad argument #%d to '?' (Usage: %s)", n, usage);
    else if (e.notFinite)
        Lua::Error(L, "bad argument #%d to '?' (Current Field: [%s], must be a finite number - Usage: %s)",
                   n, e.field, usage);
    else
        Lua::Error(L, "bad argument #%d to '?' (Current Field: [%s], Usage: %s)", n, e.field, usage);
}

// ---- the methods ----------------------------------------------------------------

Formatter *Self(void *L, const char *usage) {
    Formatter *f = ToFormatter(L, 1);
    if (f == nullptr)
        Lua::Error(L, "bad argument #1 to '?' (Usage: %s)", usage);
    return f;
}

int __fastcall Script_AddBreakpoint(void *L) {
    constexpr const char *kUsage = "self:AddBreakpoint(breakpoint)";
    Formatter *f = Self(L, kUsage);
    if (f == nullptr)
        return 0;
    ReadError e;
    if (!AddFrom(L, *f, 2, e))
        RaiseReadError(L, 2, e, kUsage);
    return 0;
}

int __fastcall Script_SetBreakpoints(void *L) {
    constexpr const char *kUsage = "self:SetBreakpoints(breakpoints)";
    Formatter *f = Self(L, kUsage);
    if (f == nullptr)
        return 0;
    if (Lua::Type(L, 2) != Lua::TYPE_TABLE) {
        Lua::Error(L, "bad argument #2 to '?' (Usage: %s)", kUsage);
        return 0;
    }
    ReadError e;
    if (!ReplaceFrom(L, *f, 2, e))
        RaiseReadError(L, 3, e, kUsage);
    return 0;
}

int __fastcall Script_ClearBreakpoints(void *L) {
    Formatter *f = Self(L, "self:ClearBreakpoints()");
    if (f != nullptr)
        f->breakpoints.clear();
    return 0;
}

int __fastcall Script_GetBreakpoints(void *L) {
    Formatter *f = Self(L, "local breakpoints = self:GetBreakpoints()");
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
    Formatter *f = Self(L, "local copy = self:Copy()");
    if (f == nullptr)
        return 0;
    Push(L, new Formatter{0, f->breakpoints});
    return 1;
}

int __fastcall Script_FormatNumber(void *L) {
    constexpr const char *kUsage = "local formatted = self:FormatNumber(number)";
    Formatter *f = Self(L, kUsage);
    if (f == nullptr)
        return 0;
    if (Lua::Type(L, 2) <= Lua::TYPE_NIL) {
        Lua::Error(L, "bad argument #2 to '?' (Usage: %s)", kUsage);
        return 0;
    }
    const double v = Lua::IsNumber(L, 2) ? Lua::ToNumber(L, 2) : NAN;
    if (!std::isfinite(v)) {
        Lua::Error(L, "bad argument #2 to '?' (must be a finite number - Usage: %s)", kUsage);
        return 0;
    }
    if (Format(L, f, v) == nullptr) {
        const char *msg = Lua::ToString(L, -1);
        Lua::Error(L, "NumericFormatter:FormatNumber(): %s", msg != nullptr ? msg : "?");
    }
    return 1;
}

int __fastcall Meta_gc(void *L) {
    auto **block = static_cast<Formatter **>(Lua::ToUserdata(L, 1));
    if (block != nullptr) {
        Release(*block);
        *block = nullptr;
    }
    return 0;
}

int __fastcall Meta_tostring(void *L) {
    char text[48];
    std::snprintf(text, sizeof(text), "NumericRuleFormatter: %p", static_cast<void *>(ToFormatter(L, 1)));
    Lua::PushString(L, text);
    return 1;
}

int __fastcall Script_CreateNumericRuleFormatter(void *L) {
    Push(L, new Formatter);
    return 1;
}

struct Method {
    const char *name;
    Lua::CFunction fn;
};
const Method kMethods[] = {
    {"AddBreakpoint", &Script_AddBreakpoint},     {"SetBreakpoints", &Script_SetBreakpoints},
    {"GetBreakpoints", &Script_GetBreakpoints},   {"ClearBreakpoints", &Script_ClearBreakpoints},
    {"Copy", &Script_Copy},                       {"FormatNumber", &Script_FormatNumber},
};

void SetFunction(void *L, const char *key, Lua::CFunction fn) {
    Lua::PushString(L, key);
    Lua::PushCClosure(L, fn, 0);
    Lua::SetTable(L, -3);
}

// registry[kMetatableKey] = {__index = methods, __gc, __tostring, __metatable = false}
void CreateMetatable(void *L) {
    PushMetatable(L);
    const bool exists = Lua::Type(L, -1) == Lua::TYPE_TABLE;
    Lua::SetTop(L, -2);
    if (exists)
        return;
    Lua::PushString(L, kMetatableKey);
    Lua::NewTable(L);
    Lua::PushString(L, "__index");
    Lua::NewTable(L);
    for (const Method &m : kMethods)
        SetFunction(L, m.name, m.fn);
    Lua::SetTable(L, -3);
    SetFunction(L, "__gc", &Meta_gc);
    SetFunction(L, "__tostring", &Meta_tostring);
    Lua::PushString(L, "__metatable");
    Lua::PushBool(L, false);
    Lua::SetTable(L, -3);
    Lua::RawSet(L, Lua::REGISTRY_INDEX);
    Lua::PushString(L, kFormatKey);
    Lua::PushCClosure(L, reinterpret_cast<Lua::CFunction>(Offsets::FUN_LUA_STR_FORMAT), 0);
    Lua::RawSet(L, Lua::REGISTRY_INDEX);
}

const Lua::EnumIntegerEntry kRounding[] = {
    {"Nearest", ROUNDING_NEAREST},
    {"Up", ROUNDING_UP},
    {"Down", ROUNDING_DOWN},
};

const Game::Doc::Field kComponentFields[] = {
    Game::Doc::Opt("div", "number", nullptr, "Divides the value by this first."),
    Game::Doc::Opt("mod", "number", nullptr, "Then takes the remainder of a division by this."),
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
                   "A string.format format: one specifier, or one for each component."),
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
    CreateMetatable(Lua::State());
    Lua::RegisterTableFunction("C_StringUtil", "CreateNumericRuleFormatter",
                               &Script_CreateNumericRuleFormatter, &kCreateNumericRuleFormatter);
    Lua::RegisterIntegerEnum("Enum", "NumericRuleFormatRounding", kRounding, "StringUtil");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterLuaFunctions};

} // namespace

Formatter *ToFormatter(void *L, int idx) {
    if (Lua::Type(L, idx) != Lua::TYPE_USERDATA)
        return nullptr;
    idx = AbsIndex(L, idx);
    if (!Lua::GetMetatable(L, idx))
        return nullptr;
    PushMetatable(L);
    const bool ours = Lua::ToPointer(L, -1) == Lua::ToPointer(L, -2);
    Lua::SetTop(L, -3);
    return ours ? *static_cast<Formatter **>(Lua::ToUserdata(L, idx)) : nullptr;
}

void Push(void *L, Formatter *f) {
    auto **block = static_cast<Formatter **>(Lua::NewUserdata(L, sizeof(Formatter *)));
    *block = f;
    Retain(f);
    PushMetatable(L);
    Lua::SetMetatable(L, -2);
}

// No addon code can run while this reads `f`: 1.12 has no newproxy, so the
// only userdata finalizer an allocation here can run is Meta_gc, and the
// caller holds a reference.
const char *Format(void *L, Formatter *f, double value) {
    const Breakpoint *bp = nullptr;
    for (const Breakpoint &b : f->breakpoints) {
        if (value >= b.threshold) {
            bp = &b;
            break;
        }
    }
    if (bp == nullptr) {
        Lua::PushNumber(L, value);
        return Lua::ToString(L, -1);
    }
    double v = Snap(value, bp->step, bp->rounding);
    if (bp->hasMin && v < bp->min)
        v = bp->min;
    if (bp->hasMax && v > bp->max)
        v = bp->max;
    const int args = bp->components.empty() ? 1 : static_cast<int>(bp->components.size());
    Lua::CheckStack(L, args + 2);
    PushFormat(L);
    Lua::PushLString(L, bp->format.data(), static_cast<unsigned int>(bp->format.size()));
    if (bp->components.empty())
        Lua::PushNumber(L, v);
    for (const Component &c : bp->components)
        Lua::PushNumber(L, Apply(c, v));
    if (Lua::PCall(L, args + 1, 1, 0) != 0)
        return nullptr;
    return Lua::ToString(L, -1);
}

} // namespace Text::NumericFormatter
