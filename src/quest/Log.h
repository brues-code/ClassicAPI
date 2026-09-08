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

// The one quest-log lookup by questID. `VAR_QUEST_LOG_ENTRIES` is walked
// from half a dozen call sites across the codebase; anything that needs
// "is this quest in the log, and where" goes through here rather than
// re-deriving the stride and the header-row gate.

namespace Quest::Log {

// 0-based index into `VAR_QUEST_LOG_ENTRIES` of the row holding
// `questID`, or -1 when the quest is not in the player's log. Header
// rows are skipped via the `+8` header-pointer gate, so the result is
// always a real quest row. Non-positive input returns -1.
//
// The index spans the full entry array (headers included), which is the
// numbering `GetQuestLogTitle` and `GetQuestIDForLogIndex` use — add 1
// for the Lua-facing 1-based form.
int IndexForQuestID(int questID);

// True while `questID` sits in the player's quest log — incomplete or
// ready to turn in, since the log holds both.
inline bool IsOnQuest(int questID) { return IndexForQuestID(questID) >= 0; }

} // namespace Quest::Log
