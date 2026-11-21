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

// `C_StringUtil.CreateSecondsFormatter()` and its three enums, as the 3.80.2
// client does them (docs/notes/seconds-formatter-3802.md; RTTI
// SecondsFormatter, a NumericFormatter). The 33 methods are 3.80.2's: retail's
// list without Get/SetRounding.
//
// FORMAT (0x141BA8D80). Below the milliseconds threshold (default 0, so every
// negative), the raw value as COOLDOWN_DURATION_TEN_SEC ("%.1f") of its float.
// Else the seconds are rounded up once, and:
//   - 0 or less: the zero format, the min interval (evaluated at 0) with 0.
//   - below approximationSeconds: LESS_THAN_OPERAND around the rounded-up count
//     of the first unit from max(min, Minutes) whose next unit does not fit;
//     3.80.2 walks past Days when the max interval is Seconds, this stops there.
//   - else up to desiredUnitCount units from max(min, max) down to min, each
//     floored (the last placed one, or the min unit, rounded up with
//     CanRoundUpLastUnit), the remainder carried by fmod. CanRoundUpIntervals
//     then promotes a count equal to its own unit's seconds (1, 60, 3600,
//     86400) to one of the next unit: 3.80.2's arithmetic, so 60 min becomes
//     1 h but 1 s becomes 1 min. The units are written largest first, joined
//     by TIME_UNIT_DELIMITER; none at all is the zero format.
// A unit is its global string (D_SECONDS / SECONDS_ABBR /
// SECOND_ONELETTER_ABBR and the minute, hour and day ones, by the
// abbreviation) with the count: lowered (UTF-8: ASCII, Latin-1 and Cyrillic
// letters) with ConvertToLower, its spaces removed by Strip (not in deDE and
// ruRU) or StripIgnoreLocale. This client's own SECONDS_ABBR etc. are plain
// words ("Sec"), not formats, and its FrameXML uses them, so Truncate shows the
// word without the count, as the ported SecondsFormatterMixin does.
//
// SETTINGS. The min interval, max interval and desired unit count each are a
// value or a curve (a LuaCurveObject the formatter keeps a reference to, not a
// copy). A curve is evaluated at the (float) seconds and cut to an integer: an
// interval past Days is Seconds, a count is its low byte. A value setter
// replaces a curve; the getters of the value give nil while a curve is set, and
// a curve getter gives a new object of the same curve. Nothing checks min <=
// max: the format takes the larger.

#include "text/NumericFormatter.h"

#include "Game.h"
#include "curve/Curve.h"
#include "localization/Locale.h"
#include "localization/Numbers.h"
#include "lua/Arg.h"
#include "lua/EnumArg.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace Text::SecondsFormatter {

namespace {

namespace Lua = Game::Lua;
namespace Arg = Game::Lua::Arg;
namespace NF = Text::NumericFormatter;

constexpr const char *kKey = "ClassicAPI.SecondsFormatter";

enum Abbreviation : uint8_t { ABBREVIATION_NONE = 0, ABBREVIATION_TRUNCATE = 1, ABBREVIATION_ONE_LETTER = 2 };
enum Interval : uint8_t { INTERVAL_SECONDS = 0, INTERVAL_MINUTES = 1, INTERVAL_HOURS = 2, INTERVAL_DAYS = 3 };
enum Whitespace : uint8_t { WHITESPACE_PRESERVE = 0, WHITESPACE_STRIP = 1, WHITESPACE_STRIP_IGNORE_LOCALE = 2 };

// The unit table (0x143BB3DD0).
struct Unit {
    double seconds;
    const char *names[3]; // by Abbreviation
};
constexpr Unit kUnits[4] = {
    {1, {"D_SECONDS", "SECONDS_ABBR", "SECOND_ONELETTER_ABBR"}},
    {60, {"D_MINUTES", "MINUTES_ABBR", "MINUTE_ONELETTER_ABBR"}},
    {3600, {"D_HOURS", "HOURS_ABBR", "HOUR_ONELETTER_ABBR"}},
    {86400, {"D_DAYS", "DAYS_ABBR", "DAY_ONELETTER_ABBR"}},
};

// A value or a curve (3.80.2's std::variant), holding a curve reference.
struct Setting {
    uint8_t value = 0;
    Curve::Scalar *curve = nullptr;

    void Set(uint8_t v) {
        Game::Lua::FullUserdata::Release(curve);
        curve = nullptr;
        value = v;
    }
    void Set(Curve::Scalar *c) {
        Game::Lua::FullUserdata::Retain(c);
        Game::Lua::FullUserdata::Release(curve);
        curve = c;
    }
};

// A curve result cut as cvttss2si does: INT_MIN when it does not fit.
int CutToInt(float r) { return r >= -2147483648.0f && r < 2147483648.0f ? static_cast<int>(r) : INT32_MIN; }

struct Formatter : NF::Formatter {
    double approximationSeconds = 0.0;
    double millisecondsThreshold = 0.0;
    uint8_t defaultAbbreviation = ABBREVIATION_NONE;
    bool canRoundUpIntervals = false;
    bool canRoundUpLastUnit = false;
    bool convertToLower = false;
    uint8_t stripWhitespace = WHITESPACE_PRESERVE;
    Setting maxInterval{INTERVAL_DAYS};
    Setting minInterval{INTERVAL_SECONDS};
    Setting desiredUnitCount{2};

    ~Formatter() override {
        maxInterval.Set(uint8_t{0});
        minInterval.Set(uint8_t{0});
        desiredUnitCount.Set(uint8_t{0});
    }

    void Reset() {
        approximationSeconds = 0.0;
        millisecondsThreshold = 0.0;
        defaultAbbreviation = ABBREVIATION_NONE;
        canRoundUpIntervals = false;
        canRoundUpLastUnit = false;
        convertToLower = false;
        stripWhitespace = WHITESPACE_PRESERVE;
        maxInterval.Set(uint8_t{INTERVAL_DAYS});
        minInterval.Set(uint8_t{INTERVAL_SECONDS});
        desiredUnitCount.Set(uint8_t{2});
    }

    // 0x141BA8CF0 / 0x141BA8C60.
    static uint8_t EvaluateInterval(const Setting &s, float x) {
        if (s.curve == nullptr)
            return s.value;
        const uint8_t byte = static_cast<uint8_t>(CutToInt(s.curve->Evaluate(x)));
        return byte <= INTERVAL_DAYS ? byte : INTERVAL_SECONDS;
    }
    // 0x141BA8BF0.
    static uint8_t EvaluateCount(const Setting &s, float x) {
        return s.curve == nullptr ? s.value : static_cast<uint8_t>(CutToInt(s.curve->Evaluate(x)));
    }

    std::string Format(void *L, double seconds, uint8_t abbreviation) const;
    // The zero format (0x141BA92C0): the min interval, evaluated at 0, with 0.
    std::string FormatZero(void *L, uint8_t abbreviation) const {
        std::string out;
        AppendUnit(L, 0, EvaluateInterval(minInterval, 0.0f), abbreviation, out);
        return out;
    }
    bool FormatValue(void *L, double value, std::string &out) const override {
        out = Format(L, value, defaultAbbreviation);
        return true;
    }

  private:
    void AppendUnit(void *L, int count, uint8_t unit, uint8_t abbreviation, std::string &out) const;
};

// ---- text -------------------------------------------------------------------------

// `format` with one value, as the client's printf takes a global string: each
// conversion of `kinds` gets `value`, "%%" is "%", any other conversion stays
// as written (a global string cannot crash the formatter).
template <typename T> std::string FormatOne(const std::string &format, const char *kinds, T value) {
    std::string out;
    for (std::size_t i = 0; i < format.size(); ++i) {
        if (format[i] != '%') {
            out += format[i];
            continue;
        }
        if (i + 1 < format.size() && format[i + 1] == '%') {
            out += '%';
            ++i;
            continue;
        }
        std::size_t j = i + 1;
        while (j < format.size() && std::strchr("-+ #0", format[j]) != nullptr)
            ++j;
        while (j < format.size() && (std::isdigit(static_cast<unsigned char>(format[j])) || format[j] == '.'))
            ++j;
        if (j >= format.size() || std::strchr(kinds, format[j]) == nullptr) {
            out += format[i];
            continue;
        }
        const std::string spec = format.substr(i, j - i + 1);
        char text[512];
        std::snprintf(text, sizeof text, spec.c_str(), value);
        out += text;
        i = j;
    }
    return out;
}

// One UTF-8 character lowered (0x142B86860's Unicode table, for the scripts
// the unit strings use): ASCII, Latin-1 and Cyrillic capitals.
std::string LowerUtf8(const std::string &s) {
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            ++i;
            continue;
        }
        if ((c & 0xE0) == 0xC0 && i + 1 < s.size() && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80) {
            uint32_t cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            if ((cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) || (cp >= 0x410 && cp <= 0x42F))
                cp += 0x20;
            else if (cp >= 0x400 && cp <= 0x40F)
                cp += 0x50;
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
            i += 2;
            continue;
        }
        out += static_cast<char>(c);
        ++i;
    }
    return out;
}

void RemoveSpaces(std::string &s) {
    std::string out;
    for (char c : s)
        if (c != ' ')
            out += c;
    s = out;
}

// 0x141BA9310.
void Formatter::AppendUnit(void *L, int count, uint8_t unit, uint8_t abbreviation, std::string &out) const {
    if (unit > INTERVAL_DAYS || abbreviation > ABBREVIATION_ONE_LETTER)
        return;
    std::string format = Localization::Numbers::GlobalString(L, kUnits[unit].names[abbreviation]);
    if (convertToLower)
        format = LowerUtf8(format);
    const char *locale = Localization::Locale::Client();
    if ((stripWhitespace == WHITESPACE_STRIP && std::strcmp(locale, "deDE") != 0 && std::strcmp(locale, "ruRU") != 0) ||
        stripWhitespace == WHITESPACE_STRIP_IGNORE_LOCALE)
        RemoveSpaces(format);
    out += FormatOne(format, "diouxXc", count);
}

// The seconds rounded up once (cvttsd2si, plus one when that cut a fraction).
double CeilSeconds(double s) {
    const int64_t whole = s >= -9223372036854775808.0 && s < 9223372036854775808.0 ? static_cast<int64_t>(s)
                                                                                   : INT64_MIN;
    double x = static_cast<double>(whole);
    if (x < s)
        x += 1.0;
    return x;
}

std::string Formatter::Format(void *L, double seconds, uint8_t abbreviation) const {
    std::string out;
    if (!(seconds >= millisecondsThreshold))
        return FormatOne(Localization::Numbers::GlobalString(L, "COOLDOWN_DURATION_TEN_SEC"), "eEfgG",
                         static_cast<double>(static_cast<float>(seconds)));
    const double x = CeilSeconds(seconds);
    if (x <= 0.0)
        return FormatZero(L, abbreviation);
    const float fx = static_cast<float>(x);
    const uint8_t min = EvaluateInterval(minInterval, fx);
    uint8_t max = EvaluateInterval(maxInterval, fx);
    if (min >= max)
        max = min;
    if (x < approximationSeconds) {
        uint8_t u = min > INTERVAL_MINUTES ? min : INTERVAL_MINUTES;
        while (u != max && u < INTERVAL_DAYS && kUnits[u + 1].seconds < x)
            ++u;
        std::string inner;
        AppendUnit(L, static_cast<int>(std::ceil(x / kUnits[u].seconds)), u, abbreviation, inner);
        return FormatOne(Localization::Numbers::GlobalString(L, "LESS_THAN_OPERAND"), "s", inner.c_str());
    }
    const uint8_t n = EvaluateCount(desiredUnitCount, fx);
    int counts[4] = {};
    int placed = 0;
    double rem = x;
    if (n != 0) {
        for (uint8_t u = max;; --u) {
            if (rem >= kUnits[u].seconds) {
                ++placed;
                double q = rem / kUnits[u].seconds;
                q = canRoundUpLastUnit && (u == min || placed == n) ? std::ceil(q) : std::floor(q);
                counts[u] = static_cast<int>(q);
                rem = std::fmod(rem, kUnits[u].seconds);
            }
            if (u == min || placed >= n)
                break;
        }
    }
    if (canRoundUpIntervals) {
        for (int i = 0; i + 1 < 4; ++i) {
            if (static_cast<double>(counts[i]) == kUnits[i].seconds) {
                ++counts[i + 1];
                counts[i] = 0;
            }
        }
    }
    const std::string delimiter = Localization::Numbers::GlobalString(L, "TIME_UNIT_DELIMITER");
    for (int u = INTERVAL_DAYS; u >= INTERVAL_SECONDS; --u) {
        if (counts[u] <= 0)
            continue;
        if (!out.empty())
            out += delimiter;
        AppendUnit(L, counts[u], static_cast<uint8_t>(u), abbreviation, out);
    }
    return out.empty() ? FormatZero(L, abbreviation) : out;
}

// ---- readers ----------------------------------------------------------------------

Formatter *Self(void *L, const char *usage) {
    auto *f = static_cast<Formatter *>(NF::ToClass(L, 1, kKey));
    if (f == nullptr)
        Arg::Raise(L, 1, nullptr, usage);
    return f;
}

// An enum argument (the generated enum readers): a number whose truncated low
// byte is at most `max`. Nil is `out` unchanged when optional.
bool Enum(void *L, int arg, int max, const char *usage, uint8_t &out, bool optional = false) {
    int value = out;
    if (Lua::Type(L, arg) <= Lua::TYPE_NIL && !optional) {
        Arg::Raise(L, arg, nullptr, usage);
        return false;
    }
    if (!Lua::ToEnumByte(L, arg, max, value)) {
        Arg::Raise(L, arg, nullptr, usage);
        return false;
    }
    out = static_cast<uint8_t>(value);
    return true;
}

// A LuaCurveObject argument (0x140D27C60), required.
Curve::Scalar *CurveArg(void *L, int arg, const char *usage) {
    Curve::Scalar *c = Curve::ToScalar(L, arg);
    if (c == nullptr)
        Arg::Raise(L, arg, nullptr, usage);
    return c;
}

void PushSetting(void *L, const Setting &s) {
    if (s.curve != nullptr)
        Lua::PushNil(L);
    else
        Lua::PushNumber(L, s.value);
}

void PushCurve(void *L, const Setting &s) {
    if (s.curve != nullptr)
        Curve::Push(L, s.curve);
    else
        Lua::PushNil(L);
}

// ---- the methods ----------------------------------------------------------------

int __fastcall Script_Format(void *L) {
    constexpr const char *kUsage = "Usage: local formattedSeconds = self:Format(seconds [, abbreviation])";
    Formatter *f = Self(L, kUsage);
    double seconds = 0.0;
    Arg::FiniteNumber(L, 2, kUsage, seconds);
    uint8_t abbreviation = f->defaultAbbreviation;
    Enum(L, 3, ABBREVIATION_ONE_LETTER, kUsage, abbreviation, true);
    const std::string text = f->Format(L, seconds, abbreviation);
    Lua::PushLString(L, text.data(), static_cast<unsigned int>(text.size()));
    return 1;
}

int __fastcall Script_FormatZero(void *L) {
    constexpr const char *kUsage = "Usage: local formattedSeconds = self:FormatZero([abbreviation])";
    Formatter *f = Self(L, kUsage);
    uint8_t abbreviation = f->defaultAbbreviation;
    Enum(L, 2, ABBREVIATION_ONE_LETTER, kUsage, abbreviation, true);
    const std::string text = f->FormatZero(L, abbreviation);
    Lua::PushLString(L, text.data(), static_cast<unsigned int>(text.size()));
    return 1;
}

int __fastcall Script_CanApproximate(void *L) {
    constexpr const char *kUsage = "Usage: local canApproximate = self:CanApproximate(seconds)";
    Formatter *f = Self(L, kUsage);
    double seconds = 0.0;
    Arg::FiniteNumber(L, 2, kUsage, seconds);
    Lua::PushBool(L, seconds > 0.0 && seconds < f->approximationSeconds);
    return 1;
}

int __fastcall Script_EvaluateDesiredUnitCount(void *L) {
    constexpr const char *kUsage = "Usage: local count = self:EvaluateDesiredUnitCount(seconds)";
    Formatter *f = Self(L, kUsage);
    double seconds = 0.0;
    Arg::FiniteNumber(L, 2, kUsage, seconds);
    Lua::PushNumber(L, Formatter::EvaluateCount(f->desiredUnitCount, static_cast<float>(seconds)));
    return 1;
}

int __fastcall Script_EvaluateMaxInterval(void *L) {
    constexpr const char *kUsage = "Usage: local interval = self:EvaluateMaxInterval(seconds)";
    Formatter *f = Self(L, kUsage);
    double seconds = 0.0;
    Arg::FiniteNumber(L, 2, kUsage, seconds);
    Lua::PushNumber(L, Formatter::EvaluateInterval(f->maxInterval, static_cast<float>(seconds)));
    return 1;
}

int __fastcall Script_EvaluateMinInterval(void *L) {
    constexpr const char *kUsage = "Usage: local interval = self:EvaluateMinInterval(seconds)";
    Formatter *f = Self(L, kUsage);
    double seconds = 0.0;
    Arg::FiniteNumber(L, 2, kUsage, seconds);
    Lua::PushNumber(L, Formatter::EvaluateInterval(f->minInterval, static_cast<float>(seconds)));
    return 1;
}

int __fastcall Script_GetApproximationSeconds(void *L) {
    Lua::PushNumber(L, Self(L, "Usage: local approximationSeconds = self:GetApproximationSeconds()")->approximationSeconds);
    return 1;
}

int __fastcall Script_GetMillisecondsThreshold(void *L) {
    Lua::PushNumber(L, Self(L, "Usage: local threshold = self:GetMillisecondsThreshold()")->millisecondsThreshold);
    return 1;
}

int __fastcall Script_GetConvertToLower(void *L) {
    Lua::PushBool(L, Self(L, "Usage: local convert = self:GetConvertToLower()")->convertToLower);
    return 1;
}

int __fastcall Script_CanRoundUpIntervals(void *L) {
    Lua::PushBool(L, Self(L, "Usage: local canRound = self:CanRoundUpIntervals()")->canRoundUpIntervals);
    return 1;
}

int __fastcall Script_CanRoundUpLastUnit(void *L) {
    Lua::PushBool(L, Self(L, "Usage: local canRound = self:CanRoundUpLastUnit()")->canRoundUpLastUnit);
    return 1;
}

int __fastcall Script_GetDefaultAbbreviation(void *L) {
    Lua::PushNumber(L, Self(L, "Usage: local abbreviation = self:GetDefaultAbbreviation()")->defaultAbbreviation);
    return 1;
}

int __fastcall Script_GetStripIntervalWhitespace(void *L) {
    Lua::PushNumber(L, Self(L, "Usage: local strip = self:GetStripIntervalWhitespace()")->stripWhitespace);
    return 1;
}

int __fastcall Script_GetDesiredUnitCount(void *L) {
    PushSetting(L, Self(L, "Usage: local count = self:GetDesiredUnitCount()")->desiredUnitCount);
    return 1;
}

int __fastcall Script_GetMaxInterval(void *L) {
    PushSetting(L, Self(L, "Usage: local interval = self:GetMaxInterval()")->maxInterval);
    return 1;
}

int __fastcall Script_GetMinInterval(void *L) {
    PushSetting(L, Self(L, "Usage: local interval = self:GetMinInterval()")->minInterval);
    return 1;
}

int __fastcall Script_GetDesiredUnitCountCurve(void *L) {
    PushCurve(L, Self(L, "Usage: local curve = self:GetDesiredUnitCountCurve()")->desiredUnitCount);
    return 1;
}

int __fastcall Script_GetMaxIntervalCurve(void *L) {
    PushCurve(L, Self(L, "Usage: local curve = self:GetMaxIntervalCurve()")->maxInterval);
    return 1;
}

int __fastcall Script_GetMinIntervalCurve(void *L) {
    PushCurve(L, Self(L, "Usage: local curve = self:GetMinIntervalCurve()")->minInterval);
    return 1;
}

int __fastcall Script_Reset(void *L) {
    Self(L, "Usage: self:Reset()")->Reset();
    return 0;
}

int __fastcall Script_SetApproximationSeconds(void *L) {
    constexpr const char *kUsage = "Usage: self:SetApproximationSeconds(seconds)";
    Formatter *f = Self(L, kUsage);
    Arg::FiniteNumber(L, 2, kUsage, f->approximationSeconds);
    return 0;
}

int __fastcall Script_SetMillisecondsThreshold(void *L) {
    constexpr const char *kUsage = "Usage: self:SetMillisecondsThreshold(threshold)";
    Formatter *f = Self(L, kUsage);
    Arg::FiniteNumber(L, 2, kUsage, f->millisecondsThreshold);
    return 0;
}

int __fastcall Script_SetCanRoundUpIntervals(void *L) {
    constexpr const char *kUsage = "Usage: self:SetCanRoundUpIntervals(canRound)";
    Formatter *f = Self(L, kUsage);
    Arg::Bool(L, 2, kUsage, f->canRoundUpIntervals);
    return 0;
}

int __fastcall Script_SetCanRoundUpLastUnit(void *L) {
    constexpr const char *kUsage = "Usage: self:SetCanRoundUpLastUnit(canRound)";
    Formatter *f = Self(L, kUsage);
    Arg::Bool(L, 2, kUsage, f->canRoundUpLastUnit);
    return 0;
}

int __fastcall Script_SetConvertToLower(void *L) {
    constexpr const char *kUsage = "Usage: self:SetConvertToLower(convert)";
    Formatter *f = Self(L, kUsage);
    Arg::Bool(L, 2, kUsage, f->convertToLower);
    return 0;
}

int __fastcall Script_SetDefaultAbbreviation(void *L) {
    constexpr const char *kUsage = "Usage: self:SetDefaultAbbreviation(abbreviation)";
    Formatter *f = Self(L, kUsage);
    Enum(L, 2, ABBREVIATION_ONE_LETTER, kUsage, f->defaultAbbreviation);
    return 0;
}

int __fastcall Script_SetStripIntervalWhitespace(void *L) {
    constexpr const char *kUsage = "Usage: self:SetStripIntervalWhitespace(strip)";
    Formatter *f = Self(L, kUsage);
    Enum(L, 2, WHITESPACE_STRIP_IGNORE_LOCALE, kUsage, f->stripWhitespace);
    return 0;
}

// The count reader (0x140B826E0): a finite number from 0 to 255, truncated.
int __fastcall Script_SetDesiredUnitCount(void *L) {
    constexpr const char *kUsage = "Usage: self:SetDesiredUnitCount(count)";
    Formatter *f = Self(L, kUsage);
    double v = 0.0;
    Arg::FiniteNumber(L, 2, kUsage, v);
    if (!(v >= 0.0 && v <= 255.0)) {
        Arg::Raise(L, 2, "outside of expected range 0 to 255", kUsage);
        return 0;
    }
    f->desiredUnitCount.Set(static_cast<uint8_t>(v));
    return 0;
}

int __fastcall Script_SetMaxInterval(void *L) {
    constexpr const char *kUsage = "Usage: self:SetMaxInterval(interval)";
    Formatter *f = Self(L, kUsage);
    uint8_t interval = 0;
    Enum(L, 2, INTERVAL_DAYS, kUsage, interval);
    f->maxInterval.Set(interval);
    return 0;
}

int __fastcall Script_SetMinInterval(void *L) {
    constexpr const char *kUsage = "Usage: self:SetMinInterval(interval)";
    Formatter *f = Self(L, kUsage);
    uint8_t interval = 0;
    Enum(L, 2, INTERVAL_DAYS, kUsage, interval);
    f->minInterval.Set(interval);
    return 0;
}

int __fastcall Script_SetDesiredUnitCountCurve(void *L) {
    constexpr const char *kUsage = "Usage: self:SetDesiredUnitCountCurve(curve)";
    Formatter *f = Self(L, kUsage);
    f->desiredUnitCount.Set(CurveArg(L, 2, kUsage));
    return 0;
}

int __fastcall Script_SetMaxIntervalCurve(void *L) {
    constexpr const char *kUsage = "Usage: self:SetMaxIntervalCurve(curve)";
    Formatter *f = Self(L, kUsage);
    f->maxInterval.Set(CurveArg(L, 2, kUsage));
    return 0;
}

int __fastcall Script_SetMinIntervalCurve(void *L) {
    constexpr const char *kUsage = "Usage: self:SetMinIntervalCurve(curve)";
    Formatter *f = Self(L, kUsage);
    f->minInterval.Set(CurveArg(L, 2, kUsage));
    return 0;
}

int __fastcall Script_CreateSecondsFormatter(void *L) {
    NF::Push(L, kKey, new Formatter);
    return 1;
}

const Lua::Boxed::Method kMethods[] = {
    {"CanApproximate", &Script_CanApproximate},
    {"CanRoundUpIntervals", &Script_CanRoundUpIntervals},
    {"CanRoundUpLastUnit", &Script_CanRoundUpLastUnit},
    {"EvaluateDesiredUnitCount", &Script_EvaluateDesiredUnitCount},
    {"EvaluateMaxInterval", &Script_EvaluateMaxInterval},
    {"EvaluateMinInterval", &Script_EvaluateMinInterval},
    {"Format", &Script_Format},
    {"FormatZero", &Script_FormatZero},
    {"GetApproximationSeconds", &Script_GetApproximationSeconds},
    {"GetConvertToLower", &Script_GetConvertToLower},
    {"GetDefaultAbbreviation", &Script_GetDefaultAbbreviation},
    {"GetDesiredUnitCount", &Script_GetDesiredUnitCount},
    {"GetDesiredUnitCountCurve", &Script_GetDesiredUnitCountCurve},
    {"GetMaxInterval", &Script_GetMaxInterval},
    {"GetMaxIntervalCurve", &Script_GetMaxIntervalCurve},
    {"GetMillisecondsThreshold", &Script_GetMillisecondsThreshold},
    {"GetMinInterval", &Script_GetMinInterval},
    {"GetMinIntervalCurve", &Script_GetMinIntervalCurve},
    {"GetStripIntervalWhitespace", &Script_GetStripIntervalWhitespace},
    {"Reset", &Script_Reset},
    {"SetApproximationSeconds", &Script_SetApproximationSeconds},
    {"SetCanRoundUpIntervals", &Script_SetCanRoundUpIntervals},
    {"SetCanRoundUpLastUnit", &Script_SetCanRoundUpLastUnit},
    {"SetConvertToLower", &Script_SetConvertToLower},
    {"SetDefaultAbbreviation", &Script_SetDefaultAbbreviation},
    {"SetDesiredUnitCount", &Script_SetDesiredUnitCount},
    {"SetDesiredUnitCountCurve", &Script_SetDesiredUnitCountCurve},
    {"SetMaxInterval", &Script_SetMaxInterval},
    {"SetMaxIntervalCurve", &Script_SetMaxIntervalCurve},
    {"SetMillisecondsThreshold", &Script_SetMillisecondsThreshold},
    {"SetMinInterval", &Script_SetMinInterval},
    {"SetMinIntervalCurve", &Script_SetMinIntervalCurve},
    {"SetStripIntervalWhitespace", &Script_SetStripIntervalWhitespace},
};

const Lua::EnumIntegerEntry kAbbreviations[] = {
    {"None", ABBREVIATION_NONE},
    {"Truncate", ABBREVIATION_TRUNCATE},
    {"OneLetter", ABBREVIATION_ONE_LETTER},
};
const Lua::EnumIntegerEntry kIntervals[] = {
    {"Seconds", INTERVAL_SECONDS},
    {"Minutes", INTERVAL_MINUTES},
    {"Hours", INTERVAL_HOURS},
    {"Days", INTERVAL_DAYS},
};
const Lua::EnumIntegerEntry kWhitespace[] = {
    {"Preserve", WHITESPACE_PRESERVE},
    {"Strip", WHITESPACE_STRIP},
    {"StripIgnoreLocale", WHITESPACE_STRIP_IGNORE_LOCALE},
};

const Game::Doc::Field kCreateRets[] = {
    Game::Doc::Req("formatter", "SecondsFormatter"),
};
const Game::Doc::Function kCreateSecondsFormatter{
    "Creates a formatter that writes a number of seconds as a duration, such as 93 as \"1 Minute 33 Seconds\".", {},
    kCreateRets};

void RegisterLuaFunctions() {
    NF::CreateClass(Lua::State(), kKey, "SecondsFormatter", kMethods);
    Lua::RegisterTableFunction("C_StringUtil", "CreateSecondsFormatter", &Script_CreateSecondsFormatter,
                               &kCreateSecondsFormatter);
    Lua::RegisterIntegerEnum("Enum", "SecondsFormatterAbbreviation", kAbbreviations, "StringUtil");
    Lua::RegisterIntegerEnum("Enum", "SecondsFormatterInterval", kIntervals, "StringUtil");
    Lua::RegisterIntegerEnum("Enum", "SecondsFormatterIntervalWhitespace", kWhitespace, "StringUtil");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterLuaFunctions};

} // namespace

} // namespace Text::SecondsFormatter
