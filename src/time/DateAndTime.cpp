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

// `C_DateAndTime.*` as the 3.80.2 Classic client has it (registered in
// FUN_140adb2f0; the functions at 0x140BFDCB0..0x140BFE640).
//
// CALENDARTIME. 3.80.2 reads a table into its WowTime (int[7]: minute, hour,
// weekday, monthDay, month, year, flags) by FUN_140DF13A0, field by field in
// the order day, monthDay, month, weekday, year, hour, minute. monthDay,
// month and weekday are numbers 0..4294967295 stored as (int)(v - 1), so 0
// becomes -1; year, hour and minute are int32 (truncated), year stored as
// year - 2000. A negative internal field means "unset" to the arithmetic. The
// writer (FUN_140DF1260) adds the 1 and the 2000 back and always writes
// day = 0. `day` is required there; retail's CalendarTime has no `day`, so
// here it is optional (validated when present) and retail-shaped tables work.
//
// ERRORS (FUN_140A604E0 and the readers FUN_140A6BF10 / FUN_140A91460 /
// FUN_140A6E2D0). A non-table date is luaL_argerror(n, usage). A field that
// is nil is argerror(fieldIndex, "Current Field: [f], usage"); a non-number
// or out-of-range one is "Current Field: [f], outside of expected range lo to
// hi - usage". fieldIndex is the stack slot the field was pushed to (gettop
// after the getfield), as there. A bad days/minutes argument has no field
// prefix, and a bad epoch is the usage alone.
//
// ARITHMETIC. AdjustTimeByDays is FUN_142A78440 with its local-time path:
// unset year/month/monthDay leaves the date as it is; otherwise mktime of the
// date at midnight, plus days * 86400 + 3600 (the hour keeps a DST change
// from landing on the previous day), then localtime; only the date and
// weekday change, hour and minute pass through. AdjustTimeByMinutes is
// FUN_142A786E0. CompareCalendarTime is FUN_142A78780: field by field (year,
// month, monthDay, weekday, hour, minute), a field unset on either side
// skipped, 1 when lhs is earlier and -1 when later.
//
// GetCalendarTimeFromEpoch (0x140BFE2A0) takes MICROSECONDS (0..2^53-1) and
// converts with FUN_140331430 at the PC's offset from FUN_1403313D0:
// -Bias, plus 60 minutes when Windows says daylight time is in effect now.
//
// CLOCKS. GetCurrentCalendarTime and GetServerTimeLocal show the realm's
// wall clock (3.80.2's WowTime global 0x1444AF550, fed by the time packets),
// which here is `Time::Server::RealmClockEpoch`. The daily reset counts in
// the instant, `CurrentEpoch`: 3.80.2 takes its reset from Cfg_Regions,
// which 1.12 has not, so the reset stays at UTC midnight, the server's day
// boundary. Its arithmetic is 3.80.2's: the last second of the period minus
// now, so 0..86399. There is no GetSecondsUntilWeeklyReset: vanilla raids
// reset on their own 7, 5 and 3 day cycles and the server sends no schedule.

#include "Game.h"
#include "Server.h"

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <windows.h>

namespace Time::DateAndTime {

namespace {

constexpr int SECONDS_PER_DAY = 86400;
constexpr int MINUTES_PER_DAY = 1440;

constexpr const char *kUsageAdjustDays =
    "Usage: local newDate = C_DateAndTime.AdjustTimeByDays(date, days)";
constexpr const char *kUsageAdjustMinutes =
    "Usage: local newDate = C_DateAndTime.AdjustTimeByMinutes(date, minutes)";
constexpr const char *kUsageCompare = "Usage: local comparison = "
                                      "C_DateAndTime.CompareCalendarTime(lhsCalendarTime, "
                                      "rhsCalendarTime)";
constexpr const char *kUsageFromEpoch =
    "Usage: local date = C_DateAndTime.GetCalendarTimeFromEpoch(epoch)";

// 3.80.2's WowTime: monthDay, month and weekday 0-based, year - 2000, a
// negative field unset.
struct WowTime {
    int minute;
    int hour;
    int weekday;
    int monthDay;
    int month;
    int year;
};

// Two's-complement adds, as the client's int arithmetic wraps.
int Wrap(int a, int b) {
    return static_cast<int>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

// cvttsd2si: a double out of int range converts to INT_MIN.
int TruncInt(double v) {
    if (!(v > -2147483649.0 && v < 2147483648.0))
        return INT32_MIN;
    return static_cast<int>(v);
}

enum class Kind {
    Index, // 0..4294967295, stored as v - 1
    Uint,  // 0..4294967295
    Int,   // int32
};

// Reads table[field] for the table at absolute index `table`. False when it
// is nil and `required` is false; any other bad value raises.
bool ReadField(void *L, int table, const char *field, Kind kind, bool required,
               const char *usage, int *out) {
    Game::Lua::PushString(L, field);
    Game::Lua::GetTable(L, table);
    const int idx = Game::Lua::GetTop(L);
    char msg[512];
    if (Game::Lua::Type(L, idx) <= Game::Lua::TYPE_NIL) {
        if (!required) {
            Game::Lua::SetTop(L, idx - 1);
            return false;
        }
        std::snprintf(msg, sizeof(msg), "Current Field: [%s], %s", field, usage);
        Game::Lua::ArgError(L, idx, msg);
    }
    const bool isInt = kind == Kind::Int;
    const double lo = isInt ? -2147483648.0 : 0.0;
    const double hi = isInt ? 2147483647.0 : 4294967295.0;
    const double v = Game::Lua::IsNumber(L, idx) ? Game::Lua::ToNumber(L, idx) : -1e300;
    if (!(v >= lo && v <= hi)) {
        std::snprintf(msg, sizeof(msg),
                      isInt ? "Current Field: [%s], outside of expected range -2147483648 to "
                              "2147483647 - %s"
                            : "Current Field: [%s], outside of expected range 0 to 4294967295 - %s",
                      field, usage);
        Game::Lua::ArgError(L, idx, msg);
    }
    *out = kind == Kind::Index ? TruncInt(v - 1.0) : TruncInt(v);
    Game::Lua::SetTop(L, idx - 1);
    return true;
}

void ReadCalendarTime(void *L, int idx, const char *usage, WowTime *t) {
    if (Game::Lua::Type(L, idx) != Game::Lua::TYPE_TABLE)
        Game::Lua::ArgError(L, idx, usage);
    int day = 0;
    ReadField(L, idx, "day", Kind::Uint, false, usage, &day);
    ReadField(L, idx, "monthDay", Kind::Index, true, usage, &t->monthDay);
    ReadField(L, idx, "month", Kind::Index, true, usage, &t->month);
    ReadField(L, idx, "weekday", Kind::Index, true, usage, &t->weekday);
    ReadField(L, idx, "year", Kind::Int, true, usage, &t->year);
    ReadField(L, idx, "hour", Kind::Int, true, usage, &t->hour);
    ReadField(L, idx, "minute", Kind::Int, true, usage, &t->minute);
    t->year = Wrap(t->year, -2000);
}

// The days / minutes argument: required int32, truncated.
int ReadIntArg(void *L, int idx, const char *usage) {
    if (Game::Lua::Type(L, idx) <= Game::Lua::TYPE_NIL)
        Game::Lua::ArgError(L, idx, usage);
    const double v = Game::Lua::IsNumber(L, idx) ? Game::Lua::ToNumber(L, idx) : -1e300;
    if (!(v >= -2147483648.0 && v <= 2147483647.0)) {
        char msg[512];
        std::snprintf(msg, sizeof(msg),
                      "outside of expected range -2147483648 to 2147483647 - %s", usage);
        Game::Lua::ArgError(L, idx, msg);
    }
    return static_cast<int>(v);
}

void PushCalendarTime(void *L, const WowTime &t) {
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "day", 0.0);
    Game::Lua::SetFieldNumber(L, "monthDay", static_cast<double>(Wrap(t.monthDay, 1)));
    Game::Lua::SetFieldNumber(L, "month", static_cast<double>(Wrap(t.month, 1)));
    Game::Lua::SetFieldNumber(L, "weekday", static_cast<double>(Wrap(t.weekday, 1)));
    Game::Lua::SetFieldNumber(L, "year", static_cast<double>(Wrap(t.year, 2000)));
    Game::Lua::SetFieldNumber(L, "hour", static_cast<double>(t.hour));
    Game::Lua::SetFieldNumber(L, "minute", static_cast<double>(t.minute));
}

// FUN_142A78440, local-time path: the date moves, hour and minute stay.
void AdjustDays(WowTime *t, int days) {
    if (t->year < 0 || t->month < 0 || t->monthDay < 0)
        return;
    std::tm in{};
    in.tm_mday = Wrap(t->monthDay, 1);
    in.tm_mon = t->month;
    in.tm_year = Wrap(t->year, 100);
    in.tm_isdst = -1;
    __time64_t when = _mktime64(&in) + static_cast<__int64>(days) * SECONDS_PER_DAY + 3600;
    std::tm out{};
    if (_localtime64_s(&out, &when) != 0)
        out = std::tm{};
    t->month = out.tm_mon;
    t->year = out.tm_year - 100;
    t->monthDay = out.tm_mday - 1;
    t->weekday = out.tm_wday;
}

// FUN_142A786E0.
void AdjustMinutes(WowTime *t, int minutes) {
    int total = (t->hour < 0 || t->minute < 0)
                    ? 0
                    : Wrap(static_cast<int>(static_cast<uint32_t>(t->hour) * 60u), t->minute);
    total = Wrap(total, minutes);
    const int days = total / MINUTES_PER_DAY;
    if (days != 0) {
        total %= MINUTES_PER_DAY;
        AdjustDays(t, days);
    }
    if (total < 0) {
        AdjustDays(t, -1);
        total += MINUTES_PER_DAY;
    }
    t->hour = total / 60;
    t->minute = total % 60;
}

// FUN_142A78780: 1 when lhs is earlier, -1 when later, 0 otherwise.
int Compare(const WowTime &lhs, const WowTime &rhs) {
    const int l[] = {lhs.year, lhs.month, lhs.monthDay, lhs.weekday, lhs.hour, lhs.minute};
    const int r[] = {rhs.year, rhs.month, rhs.monthDay, rhs.weekday, rhs.hour, rhs.minute};
    for (int i = 0; i < 6; ++i) {
        if (l[i] < 0 || r[i] < 0)
            continue;
        if (l[i] < r[i])
            return 1;
        if (r[i] < l[i])
            return -1;
    }
    return 0;
}

// FUN_140331430 on whole seconds already shifted to the wanted zone: the
// civil-from-days conversion (March-based years, 400-year eras).
WowTime FromSeconds(int64_t seconds) {
    int64_t days = seconds / SECONDS_PER_DAY;
    int64_t secOfDay = seconds % SECONDS_PER_DAY;
    if (secOfDay < 0) {
        --days;
        secOfDay += SECONDS_PER_DAY;
    }
    const int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int64_t month0 = mp < 10 ? mp + 2 : mp - 10;
    const int64_t year = yoe + era * 400 + (month0 < 2 ? 1 : 0);

    WowTime t{};
    t.minute = static_cast<int>((secOfDay % 3600) / 60);
    t.hour = static_cast<int>(secOfDay / 3600);
    t.weekday = static_cast<int>((days + 4) % 7);
    t.monthDay = static_cast<int>(doy - (153 * mp + 2) / 5);
    t.month = static_cast<int>(month0);
    t.year = static_cast<int>(year - 2000);
    return t;
}

// FUN_1403313D0: the PC's UTC offset in minutes, with a flat 60 for daylight
// time when it is in effect now.
int LocalOffsetMinutes() {
    TIME_ZONE_INFORMATION tzi{};
    const DWORD mode = GetTimeZoneInformation(&tzi);
    return (mode == TIME_ZONE_ID_DAYLIGHT ? 60 : 0) - static_cast<int>(tzi.Bias);
}

int __fastcall Script_AdjustTimeByDays(void *L) {
    WowTime t{};
    ReadCalendarTime(L, 1, kUsageAdjustDays, &t);
    const int days = ReadIntArg(L, 2, kUsageAdjustDays);
    AdjustDays(&t, days);
    PushCalendarTime(L, t);
    return 1;
}

int __fastcall Script_AdjustTimeByMinutes(void *L) {
    WowTime t{};
    ReadCalendarTime(L, 1, kUsageAdjustMinutes, &t);
    const int minutes = ReadIntArg(L, 2, kUsageAdjustMinutes);
    AdjustMinutes(&t, minutes);
    PushCalendarTime(L, t);
    return 1;
}

int __fastcall Script_CompareCalendarTime(void *L) {
    WowTime lhs{}, rhs{};
    ReadCalendarTime(L, 1, kUsageCompare, &lhs);
    ReadCalendarTime(L, 2, kUsageCompare, &rhs);
    Game::Lua::PushNumber(L, static_cast<double>(Compare(lhs, rhs)));
    return 1;
}

int __fastcall Script_GetCalendarTimeFromEpoch(void *L) {
    if (Game::Lua::Type(L, 1) <= Game::Lua::TYPE_NIL)
        Game::Lua::ArgError(L, 1, kUsageFromEpoch);
    const double v = Game::Lua::IsNumber(L, 1) ? Game::Lua::ToNumber(L, 1) : -1.0;
    if (!(v >= 0.0 && v <= 9007199254740991.0))
        Game::Lua::ArgError(L, 1, kUsageFromEpoch);
    const uint64_t micro = static_cast<uint64_t>(v);
    const int64_t seconds = static_cast<int64_t>(micro * 1000 / 1000000000);
    PushCalendarTime(L, FromSeconds(seconds + static_cast<int64_t>(LocalOffsetMinutes()) * 60));
    return 1;
}

// Realm time, not the instant: its hour and minute agree with GetGameTime().
int __fastcall Script_GetCurrentCalendarTime(void *L) {
    const int64_t epoch = Time::Server::RealmClockEpoch();
    if (epoch <= 0)
        return 0;
    PushCalendarTime(L, FromSeconds(epoch));
    return 1;
}

// The last second of the UTC day minus now (3.80.2's period arithmetic over
// the instant), so 0 in the day's last second.
int __fastcall Script_GetSecondsUntilDailyReset(void *L) {
    const int64_t epoch = Time::Server::CurrentEpoch();
    if (epoch <= 0)
        return 0;
    const int remaining = SECONDS_PER_DAY - 1 - static_cast<int>(epoch % SECONDS_PER_DAY);
    Game::Lua::PushNumber(L, static_cast<double>(remaining));
    return 1;
}

// The realm's clock digits read as UTC: `RealmClockEpoch` is that value by
// construction (GetServerTime() + the realm's offset). 3.80.2's
// (FUN_142A789B0) has seconds 0; this keeps the seconds, which clocks that
// format it with %S show.
int __fastcall Script_GetServerTimeLocal(void *L) {
    const int64_t epoch = Time::Server::RealmClockEpoch();
    if (epoch <= 0)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(epoch));
    return 1;
}

const Game::Doc::Field kCalendarTimeFields[] = {
    Game::Doc::Req("day", "number", "Always 0. An argument can leave it out."),
    Game::Doc::Req("monthDay", "luaIndex"),
    Game::Doc::Req("month", "luaIndex"),
    Game::Doc::Req("weekday", "luaIndex", "1 is Sunday."),
    Game::Doc::Req("year", "number", "The full year, such as 2026."),
    Game::Doc::Req("hour", "number", "0 to 23."),
    Game::Doc::Req("minute", "number", "0 to 59."),
};
const Game::Doc::Structure kCalendarTime{"CalendarTime", "DateAndTime", kCalendarTimeFields};

const Game::Doc::Field kAdjustByDaysArgs[] = {
    Game::Doc::Req("date", "CalendarTime"),
    Game::Doc::Req("days", "number", "Negative goes back in time."),
};
const Game::Doc::Field kAdjustRets[] = {
    Game::Doc::Req("newDate", "CalendarTime"),
};
const Game::Doc::Function kAdjustTimeByDays{
    "Returns a calendar time moved by a number of days. The hour and minute do not change.",
    kAdjustByDaysArgs, kAdjustRets};

const Game::Doc::Field kAdjustByMinutesArgs[] = {
    Game::Doc::Req("date", "CalendarTime"),
    Game::Doc::Req("minutes", "number", "Negative goes back in time."),
};
const Game::Doc::Function kAdjustTimeByMinutes{
    "Returns a calendar time moved by a number of minutes.", kAdjustByMinutesArgs,
    kAdjustRets};

const Game::Doc::Field kCompareArgs[] = {
    Game::Doc::Req("lhsCalendarTime", "CalendarTime"),
    Game::Doc::Req("rhsCalendarTime", "CalendarTime"),
};
const Game::Doc::Field kCompareRets[] = {
    Game::Doc::Req("comparison", "number",
                   "1 when lhsCalendarTime is earlier, 0 when equal, -1 when later."),
};
const Game::Doc::Function kCompareCalendarTime{"Compares two calendar times.", kCompareArgs,
                                               kCompareRets};

const Game::Doc::Field kFromEpochArgs[] = {
    Game::Doc::Req("epoch", "BigUInteger", "Microseconds since 1970-01-01 UTC."),
};
const Game::Doc::Field kFromEpochRets[] = {
    Game::Doc::Req("date", "CalendarTime", "In the time zone of the computer."),
};
const Game::Doc::Function kGetCalendarTimeFromEpoch{
    "Converts a timestamp in microseconds to a calendar time.", kFromEpochArgs,
    kFromEpochRets};

const Game::Doc::Field kCurrentRets[] = {
    Game::Doc::Opt("date", "CalendarTime", nullptr, "Nothing before login."),
};
const Game::Doc::Function kGetCurrentCalendarTime{
    "Returns the realm's current date and time, which agree with GetGameTime.", {},
    kCurrentRets};

const Game::Doc::Field kDailyResetRets[] = {
    Game::Doc::Opt("seconds", "number", nullptr, "0 to 86399. Nothing before login."),
};
const Game::Doc::Function kGetSecondsUntilDailyReset{
    "Returns the seconds until the next daily reset, at midnight UTC.", {}, kDailyResetRets};

const Game::Doc::Field kServerTimeLocalRets[] = {
    Game::Doc::Opt("serverTimeLocal", "number", nullptr,
                   "A display value, not an instant: formatted as UTC it shows the realm's "
                   "clock. Nothing before login."),
};
const Game::Doc::Function kGetServerTimeLocal{
    "Returns the server time shifted by the realm's time zone, for display.", {},
    kServerTimeLocalRets};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_DateAndTime", "AdjustTimeByDays",
                                     &Script_AdjustTimeByDays, &kAdjustTimeByDays);
    Game::Lua::RegisterTableFunction("C_DateAndTime", "AdjustTimeByMinutes",
                                     &Script_AdjustTimeByMinutes, &kAdjustTimeByMinutes);
    Game::Lua::RegisterTableFunction("C_DateAndTime", "CompareCalendarTime",
                                     &Script_CompareCalendarTime, &kCompareCalendarTime);
    Game::Lua::RegisterTableFunction("C_DateAndTime", "GetCalendarTimeFromEpoch",
                                     &Script_GetCalendarTimeFromEpoch,
                                     &kGetCalendarTimeFromEpoch);
    Game::Lua::RegisterTableFunction("C_DateAndTime", "GetCurrentCalendarTime",
                                     &Script_GetCurrentCalendarTime,
                                     &kGetCurrentCalendarTime);
    Game::Lua::RegisterTableFunction("C_DateAndTime", "GetSecondsUntilDailyReset",
                                     &Script_GetSecondsUntilDailyReset,
                                     &kGetSecondsUntilDailyReset);
    Game::Lua::RegisterTableFunction("C_DateAndTime", "GetServerTimeLocal",
                                     &Script_GetServerTimeLocal, &kGetServerTimeLocal);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Time::DateAndTime
