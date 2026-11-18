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

#pragma once

// A text log in Logs\ whose lines carry Classic Era's combat log stamp. The
// combat log file and the movement file both write through one.

#include <cstdint>
#include <cstdio>
#include <string>

namespace CombatLog {

class LogFile {
public:
    // Opens Logs\<prefix>-<fileTime>.txt for appending, shared for reading and
    // writing. True when it is open.
    bool Open(const char *prefix, const std::string &fileTime);
    void Close();
    bool IsOpen() const { return file_ != nullptr; }

    // The stamp, the body and a line end.
    void Emit(const std::string &body);

    // Between Hold and Release (they nest), every line carries the stamp of
    // the outermost Hold: lines that describe one moment.
    void Hold();
    void Release();

    // Flushes the buffer once a second; call it once a frame.
    void Tick();

private:
    std::FILE *file_ = nullptr;
    uint32_t flushedMs_ = 0;
    std::string held_; // the held stamp
    int holds_ = 0;
};

} // namespace CombatLog
