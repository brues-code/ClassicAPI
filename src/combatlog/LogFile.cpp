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

// The log is written through the CRT, not Storm's SLogWrite: Storm stamps
// every line with "M/D HH:MM:SS.mmm" and keeps no API to turn that off, and
// Era's stamp carries the year and the UTC offset
// ("5/24/2025 20:40:21.890-5  "). The buffer is flushed once a second, so a
// crash loses at most that much.

#include "combatlog/LogFile.h"

#include "time/Clock.h"

#include <share.h>
#include <windows.h>

namespace CombatLog {

namespace {

constexpr std::size_t kBufferSize = 0x10000;
constexpr uint32_t kFlushMs = 1000;

int64_t Ticks(const FILETIME &t) {
    return (static_cast<int64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}

// Era's stamp: "M/D/YYYY HH:MM:SS.mmm" in local time, the offset from UTC in
// hours, then two spaces.
void Stamp(std::string &line) {
    FILETIME utc, local;
    SYSTEMTIME t;
    GetSystemTimeAsFileTime(&utc);
    if (!FileTimeToLocalFileTime(&utc, &local) || !FileTimeToSystemTime(&local, &t)) {
        local = utc;
        FileTimeToSystemTime(&utc, &t);
    }
    const int64_t offsetMinutes = (Ticks(local) - Ticks(utc)) / 600000000; // 100 ns units
    char text[64];
    int n = std::snprintf(text, sizeof text, "%u/%u/%u %02u:%02u:%02u.%03u", t.wMonth, t.wDay,
                          t.wYear, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    if (offsetMinutes % 60 == 0)
        std::snprintf(text + n, sizeof text - n, "%+d  ", static_cast<int>(offsetMinutes / 60));
    else
        std::snprintf(text + n, sizeof text - n, "%+g  ", static_cast<double>(offsetMinutes) / 60);
    line += text;
}

} // namespace

bool LogFile::Open(const char *prefix, const std::string &fileTime) {
    CreateDirectoryA("Logs", nullptr); // fails harmlessly when it exists
    const std::string path = std::string("Logs\\") + prefix + "-" + fileTime + ".txt";
    // Shared for reading and writing, as Storm opens its logs: live log
    // readers tail the file while the game writes it. (fopen_s shares
    // nothing, which locked every reader out.)
    file_ = _fsopen(path.c_str(), "ab", _SH_DENYNO);
    if (file_ == nullptr)
        return false;
    std::setvbuf(file_, nullptr, _IOFBF, kBufferSize);
    flushedMs_ = Time::Clock::NowMs();
    return true;
}

void LogFile::Close() {
    if (file_ == nullptr)
        return;
    std::fclose(file_);
    file_ = nullptr;
}

void LogFile::Emit(const std::string &body) {
    std::string line;
    if (holds_ > 0)
        line = held_;
    else
        Stamp(line);
    line += body;
    line += "\r\n";
    std::fwrite(line.data(), 1, line.size(), file_);
}

void LogFile::Hold() {
    if (holds_++ == 0) {
        held_.clear();
        Stamp(held_);
    }
}

void LogFile::Release() {
    if (holds_ > 0)
        --holds_;
}

void LogFile::Tick() {
    if (file_ == nullptr)
        return;
    const uint32_t now = Time::Clock::NowMs();
    if (Time::Clock::Elapsed(flushedMs_, now) >= kFlushMs) {
        std::fflush(file_);
        flushedMs_ = now;
    }
}

} // namespace CombatLog
