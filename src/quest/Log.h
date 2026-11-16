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

// The quest-log row readers. Every row read goes through the engine's own
// GetQuestIdOfRow (FUN_QUEST_LOG_ROW_QUEST_ID), so nothing re-derives the
// row stride or the header gate.

#include "Offsets.h"

#include <cstdint>

namespace Quest::Log {

// Rows in the log, headers included (VAR_QUEST_LOG_ENTRY_COUNT): the range
// of the 0-based row numbers below.
inline int RowCount() {
    return *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_QUEST_LOG_ENTRY_COUNT));
}

// The questID in 0-based `row`: 0 for a header row or a row out of range
// (the engine's FUN_QUEST_LOG_ROW_QUEST_ID). An in-range 0 is a header.
inline int QuestIDForRow(int row) {
    using Fn = uint32_t(__fastcall *)(int row);
    return static_cast<int>(reinterpret_cast<Fn>(Offsets::FUN_QUEST_LOG_ROW_QUEST_ID)(row));
}

// 0-based row holding `questID`, or -1 when the quest is not in the
// player's log. Always a real quest row (a header reads questID 0).
// Non-positive input returns -1.
//
// The index spans the full entry array (headers included), which is the
// numbering `GetQuestLogTitle` and `GetQuestIDForLogIndex` use — add 1
// for the Lua-facing 1-based form.
int IndexForQuestID(int questID);

// True while `questID` sits in the player's quest log — incomplete or
// ready to turn in, since the log holds both.
inline bool IsOnQuest(int questID) { return IndexForQuestID(questID) >= 0; }

} // namespace Quest::Log
