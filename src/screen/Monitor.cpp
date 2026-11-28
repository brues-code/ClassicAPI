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

// `GetMonitorCount()`, `GetMonitorName(index)` and
// `GetMonitorAspectRatio([index])`, as the 3.80.2 client has them
// (FUN_141a2f700 / FUN_141a2f820 / FUN_141a2f730).
//
// 3.80.2 keeps a list of waMonitor records (0x170 bytes) on its graphics
// settings, built by FUN_1401ead50:
//   - Slot 0 is a copy of the primary monitor (the last one flagged primary),
//     or an empty record when none is. Slots 1..N are the monitors in
//     EnumDisplayMonitors order (Windows: FUN_140292150, callback
//     FUN_140291e30). So the count is N + 1, and Lua index 1 is "the primary".
//   - A record's size (+0x08 / +0x0C) is GetMonitorInfoW's rcMonitor width
//     and height: the monitor's desktop rectangle. The primary flag (+0x64) is
//     MONITORINFOF_PRIMARY. The name (+0x65) is EnumDisplayDevicesW(szDevice,
//     0).DeviceString.
//   - Names: slot 0 has none. A monitor called "Generic PnP Monitor" has none.
//     Names that are equal get " [n]" suffixes, from a pairwise loop that
//     appends again for a third equal name ("A [2] [1]"). That loop is kept.
//
// 1.12's device knows one adapter and no monitors, so the list is built here
// from the same Win32 calls, on each call. 3.80.2 rebuilds its list when the
// display changes, so a live read gives what its list holds.
//
// GetMonitorAspectRatio: width / height, each cut to 16 bits; 1.0 when the
// height is 0 or the index is outside the list. The index is read only when
// it is a number, as (int)value - 1, so 0 and negatives are outside.
// GetMonitorName: nothing at all without a number. Else the name (up to 127
// bytes) or nil, and the record's primary flag (false outside the list).

#include "Game.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>

namespace Screen::Monitor {

namespace {

constexpr size_t kRawNameCap = 0xFF;  // the record's 0x100-byte name field
constexpr size_t kLuaNameCap = 0x7F;  // GetMonitorName's 128-byte copy

struct Record {
    int width = 0;
    int height = 0;
    bool primary = false;
    std::string name; // raw device string
};

std::string ToUtf8(const wchar_t *text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return {};
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    out.resize(static_cast<size_t>(size - 1));
    if (out.size() > kRawNameCap)
        out.resize(kRawNameCap);
    return out;
}

BOOL CALLBACK OnMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
    auto &records = *reinterpret_cast<std::vector<Record> *>(param);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
        Record r;
        r.width = info.rcMonitor.right - info.rcMonitor.left;
        r.height = info.rcMonitor.bottom - info.rcMonitor.top;
        r.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        if (EnumDisplayDevicesW(info.szDevice, 0, &device, 0))
            r.name = ToUtf8(device.DeviceString);
        records.push_back(std::move(r));
    }
    return TRUE;
}

struct List {
    std::vector<Record> records; // slot 0 = the primary's copy
    std::vector<std::string> names;
};

List Build() {
    List list;
    list.records.emplace_back();
    EnumDisplayMonitors(nullptr, nullptr, &OnMonitor, reinterpret_cast<LPARAM>(&list.records));
    const size_t count = list.records.size();
    list.names.resize(count);
    bool havePrimary = false;
    for (size_t i = 1; i < count; ++i) {
        const Record &r = list.records[i];
        if (r.name != "Generic PnP Monitor")
            list.names[i] = r.name;
        if (r.primary) {
            list.records[0] = r;
            havePrimary = true;
        }
    }
    if (!havePrimary)
        list.records[0] = Record{};
    // FUN_1401ead50's suffix pass, as it runs.
    for (size_t outer = 1; outer <= count; ++outer) {
        int n = 0;
        const size_t first = outer - 1;
        for (size_t other = outer; other < count; ++other) {
            if (list.names[first].empty())
                continue;
            if (list.records[first].name != list.records[other].name)
                continue;
            if (n == 0) {
                n = 1;
                list.names[first] += " [1]";
            }
            ++n;
            list.names[other] += " [" + std::to_string(n) + "]";
        }
    }
    return list;
}

// The record index for a Lua index argument at stack 1, as (int)value - 1;
// `fallback` when it is not a number.
uint32_t IndexArg(void *L, uint32_t fallback) {
    if (!Game::Lua::IsNumber(L, 1))
        return fallback;
    const double value = Game::Lua::ToNumber(L, 1);
    return static_cast<uint32_t>(static_cast<int>(static_cast<int64_t>(value)) - 1);
}

int __fastcall Script_GetMonitorCount(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(Build().records.size()));
    return 1;
}

int __fastcall Script_GetMonitorName(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0;
    const uint32_t index = IndexArg(L, 0);
    const List list = Build();
    const size_t count = list.records.size();
    if (index <= count && index < list.names.size() && !list.names[index].empty()) {
        std::string name = list.names[index];
        if (name.size() > kLuaNameCap)
            name.resize(kLuaNameCap);
        Game::Lua::PushString(L, name.c_str());
    } else {
        Game::Lua::PushNil(L);
    }
    Game::Lua::PushBool(L, index < count && list.records[index].primary);
    return 2;
}

int __fastcall Script_GetMonitorAspectRatio(void *L) {
    const uint32_t index = IndexArg(L, 0);
    const List list = Build();
    Record r;
    if (index < list.records.size())
        r = list.records[index];
    float ratio = 1.0f;
    const auto height = static_cast<uint16_t>(r.height);
    if (height != 0)
        ratio = static_cast<float>(static_cast<uint16_t>(r.width)) / static_cast<float>(height);
    Game::Lua::PushNumber(L, static_cast<double>(ratio));
    return 1;
}

const Game::Doc::Field kCountRets[] = {
    Game::Doc::Req("count", "number", "The monitors, plus 1 for the primary monitor at index 1."),
};
const Game::Doc::Function kGetMonitorCount{"Returns the number of monitor entries.", {}, kCountRets,
                                           "SystemGlobals"};

const Game::Doc::Field kNameArgs[] = {
    Game::Doc::Req("index", "number", "1 is the primary monitor; 2 and up are the monitors."),
};
const Game::Doc::Field kNameRets[] = {
    Game::Doc::Opt("name", "string", nullptr, "nil for index 1 and for a monitor with no name."),
    Game::Doc::Req("isPrimary", "boolean"),
};
const Game::Doc::Function kGetMonitorName{"Returns the name of a monitor and whether it is the "
                                          "primary monitor.",
                                          kNameArgs, kNameRets, "SystemGlobals"};

const Game::Doc::Field kAspectArgs[] = {
    Game::Doc::Opt("index", "number", "1", "1 is the primary monitor; 2 and up are the monitors."),
};
const Game::Doc::Field kAspectRets[] = {
    Game::Doc::Req("aspectRatio", "number", "Width divided by height of the monitor's desktop."),
};
const Game::Doc::Function kGetMonitorAspectRatio{"Returns the aspect ratio of a monitor.",
                                                 kAspectArgs, kAspectRets, "SystemGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetMonitorCount", &Script_GetMonitorCount, &kGetMonitorCount);
    Game::Lua::RegisterGlobalFunction("GetMonitorName", &Script_GetMonitorName, &kGetMonitorName);
    Game::Lua::RegisterGlobalFunction("GetMonitorAspectRatio", &Script_GetMonitorAspectRatio,
                                      &kGetMonitorAspectRatio);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Screen::Monitor
