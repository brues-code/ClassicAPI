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

// Retail's input history: an added line always goes to the end of the ring,
// and the next Up starts again from that newest line.
//
// The engine's ring keeps ONE position, OFF_EDITBOX_HISTORY_POS, for two jobs:
// AddHistoryLine writes there and advances it, and the Up/Down walkers move it
// to the line they show (benilla-re ui/editbox "History"; 3.3.5's
// CSimpleEditBox::AddHistoryLine is the same). So a line sent after browsing
// overwrites the line the browsing reached, and the sends after it overwrite
// the newer lines in turn. Probe, chat box: send /run print(1) .. print(4),
// Up twice, Escape, send print(5), then walk Up — 1.12 shows 5, 2, 1 (3 and 4
// are gone), retail shows 5, 4, 3, 2, 1 (2026-10-26).
//
// This keeps each box's write position beside the engine's field. The add
// puts it back first, so the line lands at the end, and the advanced position
// it leaves is where the next Up starts. The walkers are untouched: the
// engine's field is their browse position. The resize does the same, so the
// engine's own clamp applies to the write position, and its size-0 call from
// the destructor drops the box before its address can be reused.
//
// A line equal to the newest one is not added again, as on retail (sending
// print(5) twice keeps one); the browse position still goes back to the end.
// Only the newest line is compared, case-sensitively: retail keeps older
// copies (print(1), print(2), print(1) walks 1, 2, 1) and adds /RUN print(5)
// after /run print(5) (2026-10-26).

#include "Game.h"
#include "Offsets.h"

#include <cstring>
#include <unordered_map>

namespace EditBox::History {

namespace {

using AddHistoryLine_t = void(__fastcall *)(void *editBox, void *edx, const char *text, int exec);
using SetHistoryMax_t = void(__fastcall *)(void *editBox, void *edx, int maxLines);

AddHistoryLine_t g_origAddHistoryLine = nullptr;
SetHistoryMax_t g_origSetHistoryMax = nullptr;

struct HistoryRecord {
    const char *text;
    int exec;
};

// The write position of every box with a history ring.
std::unordered_map<void *, int> g_writePos;

void RestoreWritePos(void *editBox) {
    const auto it = g_writePos.find(editBox);
    if (it != g_writePos.end())
        Game::Ref<int>(editBox, Offsets::OFF_EDITBOX_HISTORY_POS) = it->second;
}

void RecordWritePos(void *editBox) {
    if (Game::Read<int>(editBox, Offsets::OFF_EDITBOX_HISTORY_MAX) > 0)
        g_writePos[editBox] = Game::Read<int>(editBox, Offsets::OFF_EDITBOX_HISTORY_POS);
    else
        g_writePos.erase(editBox);
}

// Whether text equals the line just before the write position.
bool IsNewest(void *editBox, const char *text) {
    const int max = Game::Read<int>(editBox, Offsets::OFF_EDITBOX_HISTORY_MAX);
    const auto *records = Game::Read<const HistoryRecord *>(editBox, Offsets::OFF_EDITBOX_HISTORY_DATA);
    if (max <= 0 || records == nullptr || text == nullptr)
        return false;
    const int newest = (Game::Read<int>(editBox, Offsets::OFF_EDITBOX_HISTORY_POS) + max - 1) % max;
    return records[newest].text != nullptr && std::strcmp(records[newest].text, text) == 0;
}

void __fastcall AddHistoryLine_h(void *editBox, void *edx, const char *text, int exec) {
    RestoreWritePos(editBox);
    if (IsNewest(editBox, text))
        return;
    g_origAddHistoryLine(editBox, edx, text, exec);
    RecordWritePos(editBox);
}

void __fastcall SetHistoryMax_h(void *editBox, void *edx, int maxLines) {
    RestoreWritePos(editBox);
    g_origSetHistoryMax(editBox, edx, maxLines);
    RecordWritePos(editBox);
}

const Game::HookAutoRegister _addHook{Offsets::FUN_EDITBOX_ADD_HISTORY_LINE,
                                      reinterpret_cast<void *>(&AddHistoryLine_h),
                                      reinterpret_cast<void **>(&g_origAddHistoryLine)};
const Game::HookAutoRegister _setMaxHook{Offsets::FUN_EDITBOX_SET_HISTORY_MAX,
                                         reinterpret_cast<void *>(&SetHistoryMax_h),
                                         reinterpret_cast<void **>(&g_origSetHistoryMax)};

} // namespace

} // namespace EditBox::History
