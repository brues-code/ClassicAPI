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

// `C_StringUtil.CreateAbbreviatedNumberFormatter()`, as the 3.80.2 client does
// it (0x140DE3B70; RTTI AbbreviatedNumberFormatter, a NumericFormatter).
//
// A new formatter holds the client's default breakpoints (ResetBreakpoints,
// 0x141B92850: the list AbbreviateNumbers and
// GetDefaultAbbreviationBreakpoints use, by the client's locale). Its methods
// (0x140B10B60): AddBreakpoint, ClearBreakpoints, Copy, GetBreakpoints,
// ResetBreakpoints, SetBreakpoints, and the base's FormatNumber.
//
// FORMAT (0x141B91EA0). The first breakpoint the value reaches (the list is
// kept largest first): value / significandDivisor, floored (a quotient past
// the int64 range is left as it is), divided by fractionDivisor, written
// "%.14g" with the abbreviation after it: the global string it names unless
// abbreviationIsGlobal is false (a name with no string adds nothing). Below
// every breakpoint the value is written "%.14g".
//
// BREAKPOINTS (0x140D185F0). Fields in this order: breakpoint,
// abbreviation (a string, to its first NUL), significandDivisor and
// fractionDivisor, each required, the numbers finite; abbreviationIsGlobal, a
// bool, true when absent. Each is read from the stack slot it is pushed to, so
// AddBreakpoint names #3 and an entry of SetBreakpoints #4. A read breakpoint
// must then have breakpoint, significandDivisor and fractionDivisor above zero
// (0x141B926D0, 0x141B91E00), else "AbbreviatedNumberFormatter:<Method>():
// Invalid ...\nNo Lua Taint" (the pending-error formatter 0x1405AC380 always
// adds the taint line, and this client has no taint); the check's power-of-ten
// bit is masked off by both callers.
// AddBreakpoint (0x140B0FB80) inserts after every breakpoint with the same or
// a larger breakpoint. SetBreakpoints (0x140B10890) reads and checks every
// entry, then replaces the list, sorted by std::sort over reverse iterators on
// `a.breakpoint < b.breakpoint` (0x141B92D20, 0x141B927D0). 3.80.2 reads the
// list by its length; 1.12's C API reads it to its first nil.

#include "text/NumericFormatter.h"

#include "Game.h"
#include "localization/Numbers.h"
#include "lua/Arg.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace Text::AbbreviatedNumberFormatter {

namespace {

namespace Lua = Game::Lua;
namespace Arg = Game::Lua::Arg;
namespace NF = Text::NumericFormatter;
using Localization::Numbers::AbbreviationBreakpoint;

constexpr const char *kKey = "ClassicAPI.AbbreviatedNumberFormatter";

struct Formatter : NF::Formatter {
    std::vector<AbbreviationBreakpoint> breakpoints; // largest first

    bool FormatValue(void *L, double value, std::string &out) const override;
};

std::string PrintG(const char *format, double v, const char *suffix = "") {
    char text[512];
    std::snprintf(text, sizeof text, format, v, suffix);
    return text;
}

bool Formatter::FormatValue(void *L, double value, std::string &out) const {
    for (const AbbreviationBreakpoint &b : breakpoints) {
        if (!(b.breakpoint <= value))
            continue;
        double q = value / b.significandDivisor;
        // floor by the x64 cut: cvttsd2si, then one less for a negative
        // fraction; INT64_MIN (out of range) leaves q.
        if (q >= -9223372036854775808.0 && q < 9223372036854775808.0) {
            const int64_t whole = static_cast<int64_t>(q);
            if (whole != INT64_MIN && static_cast<double>(whole) != q)
                q = static_cast<double>(whole - (std::signbit(q) ? 1 : 0));
        }
        const std::string label = b.abbreviationIsGlobal
                                      ? Localization::Numbers::GlobalString(L, b.abbreviation.c_str())
                                      : b.abbreviation;
        out = PrintG("%.14g%s", q / b.fractionDivisor, label.c_str());
        return true;
    }
    out = PrintG("%.14g", value);
    return true;
}

// The usage error at `arg`, into `e`.
bool Usage(int arg, const char *usage, Arg::Error &e) {
    std::snprintf(e.text, sizeof e.text, "bad argument #%d to '?' (%s)", arg, usage);
    return false;
}

bool ReadBreakpoint(void *L, int t, const char *usage, AbbreviationBreakpoint &b, Arg::Error &e) {
    Arg::Text abbreviation;
    const bool ok = Arg::FieldFiniteNumber(L, t, "breakpoint", usage, b.breakpoint, false, &e) &&
                    Arg::FieldString(L, t, "abbreviation", usage, abbreviation, false, &e) &&
                    Arg::FieldFiniteNumber(L, t, "significandDivisor", usage, b.significandDivisor, false, &e) &&
                    Arg::FieldFiniteNumber(L, t, "fractionDivisor", usage, b.fractionDivisor, false, &e) &&
                    Arg::FieldBool(L, t, "abbreviationIsGlobal", usage, b.abbreviationIsGlobal, true, &e);
    if (ok)
        b.abbreviation.assign(abbreviation.data, abbreviation.size);
    return ok;
}

// The check's message for a breakpoint that fails it, else null.
const char *Invalid(const AbbreviationBreakpoint &b) {
    if (!(b.breakpoint > 0.0))
        return "Invalid Breakpoint - must be greater than zero and finite";
    if (!(b.significandDivisor > 0.0))
        return "Invalid Significand Divisor - must be greater than zero and finite";
    if (!(b.fractionDivisor > 0.0))
        return "Invalid Fraction Divisor - must be greater than zero and finite";
    return nullptr;
}

bool Check(const AbbreviationBreakpoint &b, const char *method, Arg::Error &e) {
    const char *message = Invalid(b);
    if (message == nullptr)
        return true;
    std::snprintf(e.text, sizeof e.text, "AbbreviatedNumberFormatter:%s(): %s\nNo Lua Taint", method, message);
    return false;
}

// ---- the methods ----------------------------------------------------------------
// Each Impl fills `e` and returns false on an error, which its Script_ raises
// once the Impl's locals are gone.

bool AddBreakpointImpl(void *L, Arg::Error &e) {
    constexpr const char *kUsage = "Usage: self:AddBreakpoint(breakpoint)";
    Formatter *f = static_cast<Formatter *>(NF::ToClass(L, 1, kKey));
    if (f == nullptr)
        return Usage(1, kUsage, e);
    if (Lua::Type(L, 2) != Lua::TYPE_TABLE)
        return Usage(2, kUsage, e);
    AbbreviationBreakpoint b;
    if (!ReadBreakpoint(L, 2, kUsage, b, e) || !Check(b, "AddBreakpoint", e))
        return false;
    const auto at = std::find_if(f->breakpoints.begin(), f->breakpoints.end(),
                                 [&](const AbbreviationBreakpoint &x) { return x.breakpoint < b.breakpoint; });
    f->breakpoints.insert(at, std::move(b));
    return true;
}

bool SetBreakpointsImpl(void *L, Arg::Error &e) {
    constexpr const char *kUsage = "Usage: self:SetBreakpoints(breakpoints)";
    Formatter *f = static_cast<Formatter *>(NF::ToClass(L, 1, kKey));
    if (f == nullptr)
        return Usage(1, kUsage, e);
    if (Lua::Type(L, 2) != Lua::TYPE_TABLE)
        return Usage(2, kUsage, e);
    std::vector<AbbreviationBreakpoint> list;
    for (int i = 1;; ++i) {
        Lua::RawGetI(L, 2, i);
        const int entry = Lua::GetTop(L);
        const int type = Lua::Type(L, entry);
        bool ok = type != Lua::TYPE_NIL;
        if (ok && type != Lua::TYPE_TABLE)
            ok = Usage(entry, kUsage, e);
        AbbreviationBreakpoint b;
        if (ok)
            ok = ReadBreakpoint(L, entry, kUsage, b, e);
        Lua::SetTop(L, entry - 1);
        if (type == Lua::TYPE_NIL)
            break;
        if (!ok)
            return false;
        list.push_back(std::move(b));
    }
    for (const AbbreviationBreakpoint &b : list) {
        if (!Check(b, "SetBreakpoints", e))
            return false;
    }
    std::sort(list.rbegin(), list.rend(), [](const AbbreviationBreakpoint &a, const AbbreviationBreakpoint &b) {
        return a.breakpoint < b.breakpoint;
    });
    f->breakpoints = std::move(list);
    return true;
}

template <bool (*Impl)(void *, Arg::Error &)> int __fastcall Raising(void *L) {
    Arg::Error e;
    if (!Impl(L, e))
        Arg::Raise(L, e);
    return 0;
}

Formatter *SelfOrRaise(void *L, const char *usage) {
    auto *f = static_cast<Formatter *>(NF::ToClass(L, 1, kKey));
    if (f == nullptr)
        Arg::Raise(L, 1, nullptr, usage);
    return f;
}

int __fastcall Script_ClearBreakpoints(void *L) {
    if (Formatter *f = SelfOrRaise(L, "Usage: self:ClearBreakpoints()"))
        f->breakpoints.clear();
    return 0;
}

int __fastcall Script_ResetBreakpoints(void *L) {
    if (Formatter *f = SelfOrRaise(L, "Usage: self:ResetBreakpoints()"))
        f->breakpoints = Localization::Numbers::DefaultAbbreviationBreakpoints();
    return 0;
}

int __fastcall Script_GetBreakpoints(void *L) {
    Formatter *f = SelfOrRaise(L, "Usage: local breakpoints = self:GetBreakpoints()");
    if (f == nullptr)
        return 0;
    Lua::NewTable(L);
    int i = 0;
    for (const AbbreviationBreakpoint &b : f->breakpoints) {
        Lua::PushNumber(L, ++i);
        Lua::NewTable(L);
        Lua::SetFieldNumber(L, "breakpoint", b.breakpoint);
        Lua::PushString(L, "abbreviation");
        Lua::PushLString(L, b.abbreviation.data(), static_cast<unsigned int>(b.abbreviation.size()));
        Lua::SetTable(L, -3);
        Lua::SetFieldNumber(L, "significandDivisor", b.significandDivisor);
        Lua::SetFieldNumber(L, "fractionDivisor", b.fractionDivisor);
        Lua::SetFieldBool(L, "abbreviationIsGlobal", b.abbreviationIsGlobal);
        Lua::SetTable(L, -3);
    }
    return 1;
}

int __fastcall Script_Copy(void *L) {
    Formatter *f = SelfOrRaise(L, "Usage: local copy = self:Copy()");
    if (f == nullptr)
        return 0;
    auto *copy = new Formatter;
    copy->breakpoints = f->breakpoints;
    NF::Push(L, kKey, copy);
    return 1;
}

int __fastcall Script_CreateAbbreviatedNumberFormatter(void *L) {
    auto *f = new Formatter;
    f->breakpoints = Localization::Numbers::DefaultAbbreviationBreakpoints();
    NF::Push(L, kKey, f);
    return 1;
}

const Lua::Boxed::Method kMethods[] = {
    {"AddBreakpoint", &Raising<&AddBreakpointImpl>},
    {"ClearBreakpoints", &Script_ClearBreakpoints},
    {"Copy", &Script_Copy},
    {"GetBreakpoints", &Script_GetBreakpoints},
    {"ResetBreakpoints", &Script_ResetBreakpoints},
    {"SetBreakpoints", &Raising<&SetBreakpointsImpl>},
};

const Game::Doc::Field kCreateRets[] = {
    Game::Doc::Req("formatter", "AbbreviatedNumberFormatter"),
};
const Game::Doc::Function kCreateAbbreviatedNumberFormatter{
    "Creates a formatter that shortens numbers with abbreviations, such as 123456 to \"123K\". It starts "
    "with the default NumberAbbreviationBreakpoint entries.",
    {}, kCreateRets};

void RegisterLuaFunctions() {
    NF::CreateClass(Lua::State(), kKey, "AbbreviatedNumberFormatter", kMethods);
    Lua::RegisterTableFunction("C_StringUtil", "CreateAbbreviatedNumberFormatter",
                               &Script_CreateAbbreviatedNumberFormatter, &kCreateAbbreviatedNumberFormatter);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterLuaFunctions};

} // namespace

} // namespace Text::AbbreviatedNumberFormatter
