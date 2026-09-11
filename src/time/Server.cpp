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

#include "Server.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <ctime>

#include <windows.h>

namespace Time::Server {

namespace {

using LocalSeconds_t = uint32_t(__fastcall *)();

// Primary source: the engine's own server-clock sync. It sends
// `CMSG_QUERY_TIME` on every enter-world and caches the skew between the
// server's reply (a raw `time(nullptr)`) and the local clock, refreshing
// hourly — so the subtraction below yields a true Unix epoch with real
// seconds, free of both the timezone guess and any per-map time offset the
// realm applies to the gametime it broadcasts. See `Offsets.h`
// (`VAR_SERVER_TIME_DELTA`) for the full derivation.
//
// Returns 0 when the engine has not synced yet — the ~1 RTT after
// enter-world, or a server that never answers the query. 0 is the engine's
// own sentinel for that, not ours: the handler substitutes 1 for a computed
// skew of 0 precisely so the value stays a reliable "synced" flag.
int64_t EpochFromServerSync() {
    const uint32_t delta =
        Game::Read<uint32_t>(Offsets::VAR_SERVER_TIME_DELTA);
    if (delta == 0)
        return 0;

    const auto localSeconds = reinterpret_cast<LocalSeconds_t>(
        static_cast<uintptr_t>(Offsets::FUN_SERVER_TIME_LOCAL_SECONDS));
    const uint32_t now = localSeconds();
    if (now == 0)
        return 0; // local clock unreadable; the delta alone says nothing

    // Unsigned subtraction, so a local clock set behind the server's (a
    // delta that reads as a huge uint32) still lands on the right epoch.
    return static_cast<int64_t>(static_cast<uint32_t>(now - delta));
}

// Interpolation anchor for the FALLBACK path below. The 1.12 wire protocol
// carries the broadcast gametime at minute
// granularity — `SMSG_LOGIN_SETTIMESPEED`'s packed gametime field has
// no seconds — so the engine's stored hour/minute fields only step
// every minute. To produce a Unix timestamp that ticks every second
// (which is what callers of `GetServerTime` actually need), we anchor
// to `GetTickCount` whenever we observe the engine's minute change,
// then add `(now - anchor) / 1000` to interpolate within the minute.
//
// First-call accuracy: we have no way to know how far into the current
// minute we are when the engine first reports it, so the cold-start
// minute is reported as :00 (off by 0..59 seconds). After the first
// minute rollover we observe, the anchor lands at the rollover boundary
// and subsequent calls are accurate.
int g_lastYear = 0;
int g_lastMonth = -1;
int g_lastDay = -1;
int g_lastHour = -1;
int g_lastMinute = -1;
DWORD g_anchorTick = 0;

// Fallback: rebuild an epoch from the broadcast gametime struct. Used only
// until the sync above lands, and on servers that don't implement
// `CMSG_QUERY_TIME`. Weaker on three counts — minute granularity papered
// over by the interpolation above, no timezone on the wire (so the server's
// wall clock is treated as UTC), and it carries whatever per-map offset the
// realm folded in before broadcasting.
int64_t EpochFromGameTime() {
    auto *base = reinterpret_cast<const uint8_t *>(
        static_cast<uintptr_t>(Offsets::VAR_GAMETIME_STRUCT));

    const int year = Game::Read<int>(base, Offsets::OFF_GAMETIME_YEAR);
    if (year <= 0)
        return 0; // pre-login / not yet sync'd

    const int month = Game::Read<int>(base, Offsets::OFF_GAMETIME_MONTH);
    const int day = Game::Read<int>(base, Offsets::OFF_GAMETIME_DAY);
    const int hour = Game::Read<int>(base, Offsets::OFF_GAMETIME_HOUR);
    const int minute = Game::Read<int>(base, Offsets::OFF_GAMETIME_MINUTE);

    const DWORD now = GetTickCount();
    if (year != g_lastYear || month != g_lastMonth || day != g_lastDay ||
        hour != g_lastHour || minute != g_lastMinute) {
        g_lastYear = year;
        g_lastMonth = month;
        g_lastDay = day;
        g_lastHour = hour;
        g_lastMinute = minute;
        g_anchorTick = now;
    }

    std::tm t{};
    t.tm_year = (year % 100) + 100; // matches engine's normalization at 0x0064234A
    t.tm_mon = month;                // [+0x10] is 0-based, feeds tm_mon directly
    t.tm_mday = day + 1;             // [+0x0C] is 0-based, engine `inc`s before tm_mday use
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = 0;
    t.tm_isdst = -1;

    const std::time_t minuteStart = _mkgmtime(&t);
    if (minuteStart < 0)
        return 0;

    // GetTickCount is unsigned and wraps every ~49 days; subtraction in
    // DWORD arithmetic gives the right elapsed value across wraparound.
    const DWORD elapsedMs = now - g_anchorTick;
    int elapsedSec = static_cast<int>(elapsedMs / 1000);
    if (elapsedSec > 59) elapsedSec = 59; // clamp at minute end

    return static_cast<int64_t>(minuteStart) + elapsedSec;
}

int64_t ComputeCurrentEpoch() {
    const int64_t synced = EpochFromServerSync();
    if (synced > 0)
        return synced;
    return EpochFromGameTime();
}

} // namespace

int64_t CurrentEpoch() { return ComputeCurrentEpoch(); }

int64_t RealmClockEpoch() { return EpochFromGameTime(); }

// `GetServerTime()` — returns the current server clock as a Unix epoch
// timestamp (seconds since 1970-01-01 UTC). 1.12's stock `GetTime()` is
// frame-relative seconds-since-login, useless for any addon that needs
// wall-clock alignment (calendar, log timestamps, cooldown sync).
//
// Prefers the engine's `CMSG_QUERY_TIME` skew (`EpochFromServerSync`) and
// falls back to the broadcast gametime struct (`EpochFromGameTime`) until
// that syncs. Returns `nil` when neither source has data — before login,
// while the gametime struct is still BSS-zero.
static int __fastcall Script_GetServerTime(void *L) {
    const int64_t epoch = ComputeCurrentEpoch();
    if (epoch <= 0)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(epoch));
    return 1;
}

// This global is not ours to keep. Registration happens inside
// `FUN_LOAD_SCRIPT_FUNCTIONS` and FrameXML loads after that, so a client
// whose `GameTime.lua` declares its own `function GetServerTime()`
// (Turtle's returns `(serverHour, serverMinute)`) silently replaces it on
// every login and every `/reload`. The reachable name when that happens is
// `ClassicAPI.GetServerTime`, which the registrar binds on its own — see
// `MirrorRegistration` in Game.cpp.
static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetServerTime", &Script_GetServerTime);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Time::Server
