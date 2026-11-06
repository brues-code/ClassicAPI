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

// `C_DurationUtil.CreateDuration()` and `Enum.DurationTimeModifier`
// (DurationUtilDocumentation.lua, LuaDurationObjectAPIDocumentation.lua). The
// object is C in retail, so what follows is retail's behavior as probed in a
// 12.1.5 client (2026-11-01; the log is in docs/notes/duration-timer.md):
//
//   - The object is a userdata, tostring "LuaDurationObject: <address>",
//     getmetatable false. A span is a start time, a total and a mod rate.
//   - Elapsed is now - start clamped to [0, total]; remaining is total -
//     elapsed, so before the start it is the whole total. The percents are
//     0 to 1; a zero span is 100% elapsed, 0% remaining.
//   - HasStarted is now >= start, HasExpired now >= end, IsActive between
//     them. A fresh or Reset object (start 0, total 0) has started and
//     expired.
//   - SetTimeSpan with the end before the start keeps the start and a zero
//     total. SetTimeFromEnd sets the start to end - duration.
//   - modRate leaves every real-time answer alone. In base time the total is
//     total / modRate from the same start: 1 s into a 4 s span at mod rate 2
//     has 1 s left of a 2 s total.
//   - Copy is independent of the original; Reset does not reach a copy.
//
// Times are GetTime() seconds and the clock is GetTime()'s, so a span
// straddling the 2^32 ms tick wrap jumps exactly as GetTime() does.
//
// The Format* methods hand the value to a Text::NumericFormatter. Not ported:
// the Evaluate* methods (retail curve objects) and SetClock /
// C_DurationUtil.CreateManualClock. 1.12 has no secret values, so
// HasSecretValues is always false.

#include "duration/Object.h"

#include "Game.h"
#include "text/NumericFormatter.h"
#include "time/Clock.h"

#include <cmath>
#include <cstdio>

namespace Duration::Object {

namespace Lua = Game::Lua;

struct Span {
    int refs = 0;
    double start = 0.0;
    double total = 0.0;
    double modRate = 1.0;
};

void Retain(Span *s) { ++s->refs; }

void Release(Span *s) {
    if (s != nullptr && --s->refs == 0)
        delete s;
}

namespace {

// Enum.DurationTimeModifier.
enum Modifier : int {
    MODIFIER_REAL_TIME = 0,
    MODIFIER_BASE_TIME = 1,
};

constexpr const char *kMetatableKey = "ClassicAPI.LuaDurationObject";

void PushMetatable(void *L) {
    Lua::PushString(L, kMetatableKey);
    Lua::RawGet(L, Lua::REGISTRY_INDEX);
}

int AbsIndex(void *L, int idx) { return idx > 0 ? idx : Lua::GetTop(L) + idx + 1; }

double Total(const Span &s, int m) { return m == MODIFIER_BASE_TIME ? s.total / s.modRate : s.total; }

double Elapsed(const Span &s, int m, double now) {
    const double e = now - s.start;
    const double total = Total(s, m);
    return e < 0.0 ? 0.0 : (e > total ? total : e);
}

double ElapsedPct(const Span &s, int m, double now) {
    const double total = Total(s, m);
    return total > 0.0 ? Elapsed(s, m, now) / total : 1.0;
}

double RemainingPct(const Span &s, int m, double now) {
    const double total = Total(s, m);
    return total > 0.0 ? (total - Elapsed(s, m, now)) / total : 0.0;
}

void Set(Span &s, double start, double total, double modRate) {
    s.start = start;
    s.total = total > 0.0 ? total : 0.0;
    s.modRate = modRate;
}

// ---- arguments ------------------------------------------------------------------

void Usage(void *L, int arg, const char *usage) { Lua::Error(L, "bad argument #%d to '?' (Usage: %s)", arg, usage); }

Span *Self(void *L, const char *usage) {
    Span *s = ToSpan(L, 1);
    if (s == nullptr)
        Usage(L, 1, usage);
    return s;
}

// A finite number at `idx`; raises the usage error otherwise.
bool Number(void *L, int idx, const char *usage, double &out) {
    out = Lua::Type(L, idx) == Lua::TYPE_NUMBER ? Lua::ToNumber(L, idx) : NAN;
    if (std::isfinite(out))
        return true;
    Usage(L, idx, usage);
    return false;
}

// The optional mod rate at `idx`: 1 when nil, else a positive number.
bool ModRate(void *L, int idx, const char *usage, double &out) {
    out = 1.0;
    if (Lua::Type(L, idx) <= Lua::TYPE_NIL)
        return true;
    if (Number(L, idx, usage, out) && out > 0.0)
        return true;
    Usage(L, idx, usage);
    return false;
}

// The optional DurationTimeModifier at `idx`: RealTime when nil.
bool ModifierArg(void *L, int idx, const char *usage, int &out) {
    if (ToModifier(L, idx, out))
        return true;
    Usage(L, idx, usage);
    return false;
}

// ---- the methods ----------------------------------------------------------------

// The getters that take a modifier: `self:Get...([modifier])`.
template <typename Answer> int Query(void *L, const char *usage, Answer answer) {
    Span *s = Self(L, usage);
    int m;
    if (s == nullptr || !ModifierArg(L, 2, usage, m))
        return 0;
    answer(*s, m, Time::Clock::NowSeconds());
    return 1;
}

int __fastcall Script_GetStartTime(void *L) {
    return Query(L, "local startTime = self:GetStartTime([modifier])",
                 [L](const Span &s, int, double) { Lua::PushNumber(L, s.start); });
}

int __fastcall Script_GetEndTime(void *L) {
    return Query(L, "local endTime = self:GetEndTime([modifier])",
                 [L](const Span &s, int m, double) { Lua::PushNumber(L, s.start + Total(s, m)); });
}

int __fastcall Script_GetTotalDuration(void *L) {
    return Query(L, "local totalDuration = self:GetTotalDuration([modifier])",
                 [L](const Span &s, int m, double) { Lua::PushNumber(L, Total(s, m)); });
}

int __fastcall Script_GetElapsedDuration(void *L) {
    return Query(L, "local elapsedDuration = self:GetElapsedDuration([modifier])",
                 [L](const Span &s, int m, double now) { Lua::PushNumber(L, Elapsed(s, m, now)); });
}

int __fastcall Script_GetRemainingDuration(void *L) {
    return Query(L, "local remainingDuration = self:GetRemainingDuration([modifier])",
                 [L](const Span &s, int m, double now) { Lua::PushNumber(L, Total(s, m) - Elapsed(s, m, now)); });
}

int __fastcall Script_GetElapsedPercent(void *L) {
    return Query(L, "local elapsedPercent = self:GetElapsedPercent([modifier])",
                 [L](const Span &s, int m, double now) { Lua::PushNumber(L, ElapsedPct(s, m, now)); });
}

int __fastcall Script_GetRemainingPercent(void *L) {
    return Query(L, "local remainingPercent = self:GetRemainingPercent([modifier])",
                 [L](const Span &s, int m, double now) { Lua::PushNumber(L, RemainingPct(s, m, now)); });
}

int __fastcall Script_HasStarted(void *L) {
    return Query(L, "local hasStarted = self:HasStarted([modifier])",
                 [L](const Span &s, int, double now) { Lua::PushBool(L, now >= s.start); });
}

int __fastcall Script_HasExpired(void *L) {
    return Query(L, "local hasExpired = self:HasExpired([modifier])",
                 [L](const Span &s, int m, double now) { Lua::PushBool(L, now >= s.start + Total(s, m)); });
}

int __fastcall Script_IsActive(void *L) {
    return Query(L, "local isActive = self:IsActive([modifier])", [L](const Span &s, int m, double now) {
        Lua::PushBool(L, now >= s.start && now < s.start + Total(s, m));
    });
}

// `self:Format...Duration(formatter [, modifier])`: the formatter's text for
// the value, or its format error, raised as FormatNumber raises it.
int Format(void *L, const char *usage, const char *method, int property) {
    Span *s = Self(L, usage);
    if (s == nullptr)
        return 0;
    Text::NumericFormatter::Formatter *f = Text::NumericFormatter::ToFormatter(L, 2);
    int m;
    if (f == nullptr) {
        Usage(L, 2, usage);
        return 0;
    }
    if (!ModifierArg(L, 3, usage, m))
        return 0;
    if (Text::NumericFormatter::Format(L, f, Sample(s, property, m)) == nullptr) {
        const char *msg = Lua::ToString(L, -1);
        Lua::Error(L, "LuaDurationObject:%s(): %s", method, msg != nullptr ? msg : "?");
    }
    return 1;
}

int __fastcall Script_FormatElapsedDuration(void *L) {
    return Format(L, "local formatted = self:FormatElapsedDuration(formatter [, modifier])",
                  "FormatElapsedDuration", PROPERTY_ELAPSED_DURATION);
}

int __fastcall Script_FormatRemainingDuration(void *L) {
    return Format(L, "local formatted = self:FormatRemainingDuration(formatter [, modifier])",
                  "FormatRemainingDuration", PROPERTY_REMAINING_DURATION);
}

int __fastcall Script_FormatTotalDuration(void *L) {
    return Format(L, "local formatted = self:FormatTotalDuration(formatter [, modifier])",
                  "FormatTotalDuration", PROPERTY_TOTAL_DURATION);
}

int __fastcall Script_IsZero(void *L) {
    Span *s = Self(L, "local isZero = self:IsZero()");
    if (s == nullptr)
        return 0;
    Lua::PushBool(L, s->total == 0.0);
    return 1;
}

int __fastcall Script_GetModRate(void *L) {
    Span *s = Self(L, "local modRate = self:GetModRate()");
    if (s == nullptr)
        return 0;
    Lua::PushNumber(L, s->modRate);
    return 1;
}

// Nil: the default clock, GetTime().
int __fastcall Script_GetClock(void *L) {
    if (Self(L, "local clock = self:GetClock()") == nullptr)
        return 0;
    Lua::PushNil(L);
    return 1;
}

int __fastcall Script_GetClockTime(void *L) {
    if (Self(L, "local clockTime = self:GetClockTime()") == nullptr)
        return 0;
    Lua::PushNumber(L, Time::Clock::NowSeconds());
    return 1;
}

int __fastcall Script_HasSecretValues(void *L) {
    if (Self(L, "local hasSecretValues = self:HasSecretValues()") == nullptr)
        return 0;
    Lua::PushBool(L, false);
    return 1;
}

int __fastcall Script_SetTimeFromStart(void *L) {
    constexpr const char *kUsage = "self:SetTimeFromStart(startTime, duration [, modRate])";
    Span *s = Self(L, kUsage);
    double start, duration, modRate;
    if (s == nullptr || !Number(L, 2, kUsage, start) || !Number(L, 3, kUsage, duration) ||
        !ModRate(L, 4, kUsage, modRate))
        return 0;
    Set(*s, start, duration, modRate);
    return 0;
}

int __fastcall Script_SetTimeFromEnd(void *L) {
    constexpr const char *kUsage = "self:SetTimeFromEnd(endTime, duration [, modRate])";
    Span *s = Self(L, kUsage);
    double end, duration, modRate;
    if (s == nullptr || !Number(L, 2, kUsage, end) || !Number(L, 3, kUsage, duration) ||
        !ModRate(L, 4, kUsage, modRate))
        return 0;
    if (duration < 0.0)
        duration = 0.0;
    Set(*s, end - duration, duration, modRate);
    return 0;
}

// Takes no mod rate, so the span has none (not probed).
int __fastcall Script_SetTimeSpan(void *L) {
    constexpr const char *kUsage = "self:SetTimeSpan(startTime, endTime)";
    Span *s = Self(L, kUsage);
    double start, end;
    if (s == nullptr || !Number(L, 2, kUsage, start) || !Number(L, 3, kUsage, end))
        return 0;
    Set(*s, start, end - start, 1.0);
    return 0;
}

int __fastcall Script_Reset(void *L) {
    if (Span *s = Self(L, "self:Reset()"))
        Set(*s, 0.0, 0.0, 1.0);
    return 0;
}

// Reset plus the secret flag and the clock, neither of which 1.12 has.
int __fastcall Script_SetToDefaults(void *L) {
    if (Span *s = Self(L, "self:SetToDefaults()"))
        Set(*s, 0.0, 0.0, 1.0);
    return 0;
}

int __fastcall Script_Assign(void *L) {
    constexpr const char *kUsage = "self:Assign(other)";
    Span *s = Self(L, kUsage);
    if (s == nullptr)
        return 0;
    const Span *other = ToSpan(L, 2);
    if (other == nullptr) {
        Usage(L, 2, kUsage);
        return 0;
    }
    Set(*s, other->start, other->total, other->modRate);
    return 0;
}

int __fastcall Script_Copy(void *L) {
    Span *s = Self(L, "local copy = self:Copy()");
    if (s == nullptr)
        return 0;
    Span *copy = new Span;
    Set(*copy, s->start, s->total, s->modRate);
    Push(L, copy);
    return 1;
}

int __fastcall Meta_gc(void *L) {
    auto **block = static_cast<Span **>(Lua::ToUserdata(L, 1));
    if (block != nullptr) {
        Release(*block);
        *block = nullptr;
    }
    return 0;
}

int __fastcall Meta_tostring(void *L) {
    char text[48];
    std::snprintf(text, sizeof(text), "LuaDurationObject: %p", static_cast<void *>(ToSpan(L, 1)));
    Lua::PushString(L, text);
    return 1;
}

int __fastcall Script_CreateDuration(void *L) {
    Push(L, new Span);
    return 1;
}

struct Method {
    const char *name;
    Lua::CFunction fn;
};
const Method kMethods[] = {
    {"Assign", &Script_Assign},
    {"Copy", &Script_Copy},
    {"FormatElapsedDuration", &Script_FormatElapsedDuration},
    {"FormatRemainingDuration", &Script_FormatRemainingDuration},
    {"FormatTotalDuration", &Script_FormatTotalDuration},
    {"GetClock", &Script_GetClock},
    {"GetClockTime", &Script_GetClockTime},
    {"GetElapsedDuration", &Script_GetElapsedDuration},
    {"GetElapsedPercent", &Script_GetElapsedPercent},
    {"GetEndTime", &Script_GetEndTime},
    {"GetModRate", &Script_GetModRate},
    {"GetRemainingDuration", &Script_GetRemainingDuration},
    {"GetRemainingPercent", &Script_GetRemainingPercent},
    {"GetStartTime", &Script_GetStartTime},
    {"GetTotalDuration", &Script_GetTotalDuration},
    {"HasExpired", &Script_HasExpired},
    {"HasSecretValues", &Script_HasSecretValues},
    {"HasStarted", &Script_HasStarted},
    {"IsActive", &Script_IsActive},
    {"IsZero", &Script_IsZero},
    {"Reset", &Script_Reset},
    {"SetTimeFromEnd", &Script_SetTimeFromEnd},
    {"SetTimeFromStart", &Script_SetTimeFromStart},
    {"SetTimeSpan", &Script_SetTimeSpan},
    {"SetToDefaults", &Script_SetToDefaults},
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
}

const Lua::EnumIntegerEntry kModifiers[] = {
    {"RealTime", MODIFIER_REAL_TIME},
    {"BaseTime", MODIFIER_BASE_TIME},
};

const Game::Doc::Field kCreateRets[] = {
    Game::Doc::Req("duration", "LuaDurationObject"),
};
const Game::Doc::Function kCreateDuration{"Creates a zero duration object that can hold a time span.", {},
                                          kCreateRets};

void RegisterLuaFunctions() {
    CreateMetatable(Lua::State());
    Lua::RegisterTableFunction("C_DurationUtil", "CreateDuration", &Script_CreateDuration, &kCreateDuration);
    Lua::RegisterIntegerEnum("Enum", "DurationTimeModifier", kModifiers, "LuaDurationObjectShared");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterLuaFunctions};

} // namespace

Span *ToSpan(void *L, int idx) {
    if (Lua::Type(L, idx) != Lua::TYPE_USERDATA)
        return nullptr;
    idx = AbsIndex(L, idx);
    if (!Lua::GetMetatable(L, idx))
        return nullptr;
    PushMetatable(L);
    const bool ours = Lua::ToPointer(L, -1) == Lua::ToPointer(L, -2);
    Lua::SetTop(L, -3);
    return ours ? *static_cast<Span **>(Lua::ToUserdata(L, idx)) : nullptr;
}

void Push(void *L, Span *s) {
    auto **block = static_cast<Span **>(Lua::NewUserdata(L, sizeof(Span *)));
    *block = s;
    Retain(s);
    PushMetatable(L);
    Lua::SetMetatable(L, -2);
}

void PushFromStart(void *L, double startSec, double totalSec) {
    Span *s = new Span;
    Set(*s, startSec, totalSec, 1.0);
    Push(L, s);
}

double ElapsedPercent(const Span *s) { return ElapsedPct(*s, MODIFIER_REAL_TIME, Time::Clock::NowSeconds()); }

double RemainingPercent(const Span *s) { return RemainingPct(*s, MODIFIER_REAL_TIME, Time::Clock::NowSeconds()); }

double Sample(const Span *s, int property, int modifier) {
    const double now = Time::Clock::NowSeconds();
    switch (property) {
    case PROPERTY_REMAINING_DURATION:
        return Total(*s, modifier) - Elapsed(*s, modifier, now);
    case PROPERTY_REMAINING_PERCENT:
        return RemainingPct(*s, modifier, now);
    case PROPERTY_ELAPSED_DURATION:
        return Elapsed(*s, modifier, now);
    case PROPERTY_ELAPSED_PERCENT:
        return ElapsedPct(*s, modifier, now);
    case PROPERTY_TOTAL_DURATION:
        return Total(*s, modifier);
    case PROPERTY_START_TIME:
        return s->start;
    default:
        return s->start + Total(*s, modifier);
    }
}

bool IsZero(const Span *s) { return s->total == 0.0; }

bool HasExpired(const Span *s, int modifier) {
    return Time::Clock::NowSeconds() >= s->start + Total(*s, modifier);
}

bool ToModifier(void *L, int idx, int &out) {
    out = MODIFIER_REAL_TIME;
    if (Lua::Type(L, idx) <= Lua::TYPE_NIL)
        return true;
    const double n = Lua::Type(L, idx) == Lua::TYPE_NUMBER ? Lua::ToNumber(L, idx) : NAN;
    if (n != MODIFIER_REAL_TIME && n != MODIFIER_BASE_TIME)
        return false;
    out = static_cast<int>(n);
    return true;
}

} // namespace Duration::Object
