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

// `C_DurationUtil.CreateDuration()` and `Enum.DurationTimeModifier`, as the
// 3.80.2 client's LuaDurationObject keeps them (method table FUN_140d322e0;
// the span helpers around 0x141ba4000):
//
//   - The object is a userdata, tostring "LuaDurationObject: <address>",
//     getmetatable false; two Lua values on one object compare equal (__eq
//     0x140d34e80).
//   - Times are whole milliseconds in 32 bits: an argument becomes
//     (int)(x * 1000), truncated and wrapping past 2^32 (FUN_140c2e220), and
//     a value comes back as ms * 0.001 (FUN_140c2e110). Compares are
//     unsigned, as GetTime()'s clock is.
//   - A span is one of four forms: empty (new, Reset), from a start
//     (SetTimeFromStart), from an end (SetTimeFromEnd), or a start and an
//     end (SetTimeSpan). The first two keep a duration and a float mod rate,
//     stored as given: no clamp, no check of the rate.
//   - Real time ignores the mod rate. Base time divides the duration by it
//     (a rate of 0 counts as 1), keeping the start of a span from a start
//     and the end of a span from an end; a span by start and end has no
//     rate.
//   - Elapsed and remaining are 0 before / after the span as 3.80.2's
//     FUN_141ba4280 / FUN_141ba40f0 compute them; the percents are floats,
//     elapsed 1 and remaining 0 for a zero total.
//   - HasStarted is start <= now, HasExpired end <= now, IsActive between.
//     IsZero is a zero real-time total.
//   - Copy is independent of the original; Reset does not reach a copy.
//
// The Format* methods hand the value (ms * 0.001) to a Text::NumericFormatter.
// Not ported: the Evaluate* methods (curve objects), SetClock /
// C_DurationUtil.CreateManualClock, and per-object fields (3.80.2 stores
// assigned keys in a table of the object's own). 1.12 has no secret values,
// so HasSecretValues is always false.

#include "duration/Object.h"

#include "Game.h"
#include "lua/Boxed.h"
#include "lua/EnumArg.h"
#include "text/NumericFormatter.h"
#include "time/Clock.h"

#include <cfloat>
#include <cmath>
#include <cstdio>

namespace Duration::Object {

namespace Lua = Game::Lua;

enum Form : int {
    FORM_EMPTY = 0,
    FORM_FROM_START = 1,
    FORM_FROM_END = 2,
    FORM_SPAN = 3,
};

// `a`: the start (FromStart, Span) or the end (FromEnd); `b`: the duration
// (FromStart, FromEnd) or the end (Span), every one in ms.
struct Span {
    int refs = 0;
    Form form = FORM_EMPTY;
    uint32_t a = 0;
    uint32_t b = 0;
    float modRate = 1.0f;
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

// The duration in base time: (int)(ms * 0.001 / rate * 1000), a rate of 0
// read as 1 (0x141ba4b90).
uint32_t Scaled(const Span &s) {
    const double rate = s.modRate == 0.0f ? 1.0 : static_cast<double>(s.modRate);
    const double ms = static_cast<double>(s.b) * 0.001 / rate * 1000.0;
    return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int64_t>(ms)));
}

uint32_t Duration(const Span &s, int m) { return m == MODIFIER_BASE_TIME ? Scaled(s) : s.b; }

uint32_t Start(const Span &s, int m) {
    switch (s.form) {
    case FORM_FROM_START:
    case FORM_SPAN:
        return s.a;
    case FORM_FROM_END:
        return s.a - Duration(s, m);
    default:
        return 0;
    }
}

uint32_t End(const Span &s, int m) {
    switch (s.form) {
    case FORM_FROM_START:
        return s.a + Duration(s, m);
    case FORM_FROM_END:
        return s.a;
    case FORM_SPAN:
        return s.b;
    default:
        return 0;
    }
}

uint32_t Total(const Span &s, int m) {
    switch (s.form) {
    case FORM_FROM_START:
    case FORM_FROM_END:
        return Duration(s, m);
    case FORM_SPAN:
        return s.b - s.a;
    default:
        return 0;
    }
}

uint32_t Remaining(const Span &s, int m, uint32_t now) {
    const uint32_t start = Start(s, m);
    const uint32_t end = End(s, m);
    if (start < end && now < end)
        return start < now ? end - now : Total(s, m);
    return 0;
}

uint32_t Elapsed(const Span &s, int m, uint32_t now) {
    const uint32_t start = Start(s, m);
    const uint32_t end = End(s, m);
    if (start < end && start < now)
        return now < end ? now - start : Total(s, m);
    return 0;
}

float RemainingPct(const Span &s, int m, uint32_t now) {
    const uint32_t total = Total(s, m);
    if (total == 0)
        return 0.0f;
    return static_cast<float>(Remaining(s, m, now) * 0.001 / (total * 0.001));
}

float ElapsedPct(const Span &s, int m, uint32_t now) {
    const uint32_t total = Total(s, m);
    if (total == 0)
        return 1.0f;
    return static_cast<float>(Elapsed(s, m, now) * 0.001 / (total * 0.001));
}

double Seconds(uint32_t ms) { return static_cast<double>(ms) * 0.001; }

void Set(Span &s, Form form, uint32_t a, uint32_t b, float modRate) {
    s.form = form;
    s.a = a;
    s.b = b;
    s.modRate = modRate;
}

// ---- arguments ------------------------------------------------------------------

void Usage(void *L, int arg, const char *usage) {
    Lua::Error(L, "bad argument #%d to '?' (Usage: %s)", arg, usage);
}

Span *Self(void *L, const char *usage) {
    Span *s = ToSpan(L, 1);
    if (s == nullptr)
        Usage(L, 1, usage);
    return s;
}

// A finite number of seconds at `idx` as ms: truncated, wrapping past 2^32.
bool Time(void *L, int idx, const char *usage, uint32_t &out) {
    const double x = Lua::Type(L, idx) == Lua::TYPE_NUMBER ? Lua::ToNumber(L, idx) : NAN;
    if (!std::isfinite(x)) {
        Usage(L, idx, usage);
        return false;
    }
    double ms = std::fmod(std::trunc(x * 1000.0), 4294967296.0);
    if (ms < 0.0)
        ms += 4294967296.0;
    out = static_cast<uint32_t>(ms);
    return true;
}

// The optional mod rate at `idx`: 1 when nil, else any finite float, zero
// and negative ones too (FUN_140a6df60).
bool ModRate(void *L, int idx, const char *usage, float &out) {
    out = 1.0f;
    if (Lua::Type(L, idx) <= Lua::TYPE_NIL)
        return true;
    if (Lua::Type(L, idx) != Lua::TYPE_NUMBER) {
        Usage(L, idx, usage);
        return false;
    }
    const double rate = Lua::ToNumber(L, idx);
    if (!std::isfinite(rate)) {
        Lua::Error(L, "bad argument #%d to '?' (must be a finite number - Usage: %s)", idx, usage);
        return false;
    }
    if (rate < -FLT_MAX || rate > FLT_MAX) {
        Lua::Error(L, "bad argument #%d to '?' (outside of expected range %e to %e - Usage: %s)",
                   idx, static_cast<double>(-FLT_MAX), static_cast<double>(FLT_MAX), usage);
        return false;
    }
    out = static_cast<float>(rate);
    return true;
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
    answer(*s, m, Time::Clock::NowMs());
    return 1;
}

int __fastcall Script_GetStartTime(void *L) {
    return Query(L, "local startTime = self:GetStartTime([modifier])",
                 [L](const Span &s, int m, uint32_t) { Lua::PushNumber(L, Seconds(Start(s, m))); });
}

int __fastcall Script_GetEndTime(void *L) {
    return Query(L, "local endTime = self:GetEndTime([modifier])",
                 [L](const Span &s, int m, uint32_t) { Lua::PushNumber(L, Seconds(End(s, m))); });
}

int __fastcall Script_GetTotalDuration(void *L) {
    return Query(L, "local totalDuration = self:GetTotalDuration([modifier])",
                 [L](const Span &s, int m, uint32_t) { Lua::PushNumber(L, Seconds(Total(s, m))); });
}

int __fastcall Script_GetElapsedDuration(void *L) {
    return Query(L, "local elapsedDuration = self:GetElapsedDuration([modifier])",
                 [L](const Span &s, int m, uint32_t now) {
                     Lua::PushNumber(L, Seconds(Elapsed(s, m, now)));
                 });
}

int __fastcall Script_GetRemainingDuration(void *L) {
    return Query(L, "local remainingDuration = self:GetRemainingDuration([modifier])",
                 [L](const Span &s, int m, uint32_t now) {
                     Lua::PushNumber(L, Seconds(Remaining(s, m, now)));
                 });
}

int __fastcall Script_GetElapsedPercent(void *L) {
    return Query(L, "local elapsedPercent = self:GetElapsedPercent([modifier])",
                 [L](const Span &s, int m, uint32_t now) { Lua::PushNumber(L, ElapsedPct(s, m, now)); });
}

int __fastcall Script_GetRemainingPercent(void *L) {
    return Query(L, "local remainingPercent = self:GetRemainingPercent([modifier])",
                 [L](const Span &s, int m, uint32_t now) { Lua::PushNumber(L, RemainingPct(s, m, now)); });
}

int __fastcall Script_HasStarted(void *L) {
    return Query(L, "local hasStarted = self:HasStarted([modifier])",
                 [L](const Span &s, int m, uint32_t now) { Lua::PushBool(L, Start(s, m) <= now); });
}

int __fastcall Script_HasExpired(void *L) {
    return Query(L, "local hasExpired = self:HasExpired([modifier])",
                 [L](const Span &s, int m, uint32_t now) { Lua::PushBool(L, End(s, m) <= now); });
}

int __fastcall Script_IsActive(void *L) {
    return Query(L, "local isActive = self:IsActive([modifier])", [L](const Span &s, int m, uint32_t now) {
        Lua::PushBool(L, Start(s, m) <= now && now < End(s, m));
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
    Lua::PushBool(L, IsZero(s));
    return 1;
}

// The stored rate of a span with a duration, else 1 (0x141ba4b80 /
// 0x140549dc0).
int __fastcall Script_GetModRate(void *L) {
    Span *s = Self(L, "local modRate = self:GetModRate()");
    if (s == nullptr)
        return 0;
    const bool rated = s->form == FORM_FROM_START || s->form == FORM_FROM_END;
    Lua::PushNumber(L, rated ? s->modRate : 1.0f);
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
    Lua::PushNumber(L, Seconds(Time::Clock::NowMs()));
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
    uint32_t start, duration;
    float modRate;
    if (s == nullptr || !Time(L, 2, kUsage, start) || !Time(L, 3, kUsage, duration) ||
        !ModRate(L, 4, kUsage, modRate))
        return 0;
    Set(*s, FORM_FROM_START, start, duration, modRate);
    return 0;
}

int __fastcall Script_SetTimeFromEnd(void *L) {
    constexpr const char *kUsage = "self:SetTimeFromEnd(endTime, duration [, modRate])";
    Span *s = Self(L, kUsage);
    uint32_t end, duration;
    float modRate;
    if (s == nullptr || !Time(L, 2, kUsage, end) || !Time(L, 3, kUsage, duration) ||
        !ModRate(L, 4, kUsage, modRate))
        return 0;
    Set(*s, FORM_FROM_END, end, duration, modRate);
    return 0;
}

// An end before the start is the start (0x140d31ee0).
int __fastcall Script_SetTimeSpan(void *L) {
    constexpr const char *kUsage = "self:SetTimeSpan(startTime, endTime)";
    Span *s = Self(L, kUsage);
    uint32_t start, end;
    if (s == nullptr || !Time(L, 2, kUsage, start) || !Time(L, 3, kUsage, end))
        return 0;
    Set(*s, FORM_SPAN, start, end < start ? start : end, 1.0f);
    return 0;
}

int __fastcall Script_Reset(void *L) {
    if (Span *s = Self(L, "self:Reset()"))
        Set(*s, FORM_EMPTY, 0, 0, 1.0f);
    return 0;
}

// Reset plus the secret flag and the clock, neither of which 1.12 has.
int __fastcall Script_SetToDefaults(void *L) {
    if (Span *s = Self(L, "self:SetToDefaults()"))
        Set(*s, FORM_EMPTY, 0, 0, 1.0f);
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
    Set(*s, other->form, other->a, other->b, other->modRate);
    return 0;
}

int __fastcall Script_Copy(void *L) {
    Span *s = Self(L, "local copy = self:Copy()");
    if (s == nullptr)
        return 0;
    Span *copy = new Span;
    Set(*copy, s->form, s->a, s->b, s->modRate);
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

int __fastcall Meta_eq(void *L) {
    const Span *a = ToSpan(L, 1);
    Lua::PushBool(L, a != nullptr && a == ToSpan(L, 2));
    return 1;
}

int __fastcall Script_CreateDuration(void *L) {
    Push(L, new Span);
    return 1;
}

using Lua::Boxed::Method;
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

// registry[kMetatableKey] = {__index = methods, __gc, __tostring, __eq, __metatable = false}
void CreateMetatable(void *L) {
    Lua::Boxed::CreateMetatable(L, kMetatableKey, kMethods, &Meta_gc, &Meta_tostring, &Meta_eq);
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

void PushNew(void *L, Form form, uint32_t a, uint32_t b, float modRate) {
    Span *s = new Span;
    Set(*s, form, a, b, modRate);
    Push(L, s);
}

} // namespace

Span *ToSpan(void *L, int idx) {
    return static_cast<Span *>(Lua::Boxed::To(L, idx, kMetatableKey));
}

void Push(void *L, Span *s) {
    Retain(s);
    Lua::Boxed::Push(L, kMetatableKey, s);
}

void PushFromStart(void *L, uint32_t startMs, uint32_t durationMs) {
    PushNew(L, FORM_FROM_START, startMs, durationMs, 1.0f);
}

void PushSpan(void *L, uint32_t startMs, uint32_t endMs) {
    PushNew(L, FORM_SPAN, startMs, endMs < startMs ? startMs : endMs, 1.0f);
}

double ElapsedPercent(const Span *s) { return ElapsedPct(*s, MODIFIER_REAL_TIME, Time::Clock::NowMs()); }

double RemainingPercent(const Span *s) { return RemainingPct(*s, MODIFIER_REAL_TIME, Time::Clock::NowMs()); }

double Sample(const Span *s, int property, int modifier) {
    const uint32_t now = Time::Clock::NowMs();
    switch (property) {
    case PROPERTY_REMAINING_DURATION:
        return Seconds(Remaining(*s, modifier, now));
    case PROPERTY_REMAINING_PERCENT:
        return RemainingPct(*s, modifier, now);
    case PROPERTY_ELAPSED_DURATION:
        return Seconds(Elapsed(*s, modifier, now));
    case PROPERTY_ELAPSED_PERCENT:
        return ElapsedPct(*s, modifier, now);
    case PROPERTY_TOTAL_DURATION:
        return Seconds(Total(*s, modifier));
    case PROPERTY_START_TIME:
        return Seconds(Start(*s, modifier));
    default:
        return Seconds(End(*s, modifier));
    }
}

bool IsZero(const Span *s) { return Total(*s, MODIFIER_REAL_TIME) == 0; }

bool HasExpired(const Span *s, int modifier) { return End(*s, modifier) <= Time::Clock::NowMs(); }

bool ToModifier(void *L, int idx, int &out) {
    out = MODIFIER_REAL_TIME;
    return Lua::ToEnumByte(L, idx, MODIFIER_BASE_TIME, out);
}

} // namespace Duration::Object
