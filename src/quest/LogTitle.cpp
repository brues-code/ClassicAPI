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

// `GetQuestLogTitle(index)` in the layout TBC-through-MoP addons read.
//
//   vanilla:    title, level, questTag, isHeader, isCollapsed, isComplete
//   2.0 - 5.4:  title, level, questTag, suggestedGroup, isHeader,
//               isCollapsed, isComplete, isDaily, questID (questID from 3.3)
//
// One return list can't serve both: suggestedGroup at position 4 moves
// isHeader/isCollapsed/isComplete, so code written for either layout reads
// the other's in the wrong slots. Zygor Guides Viewer (Interface 40000) takes
// the vanilla isCollapsed as isHeader - caching zone headers as quests - and
// finds no questID at position 9.
//
// Co-hook: when the calling addon's `## Interface:` (Api::CallerInterface) is
// one the original 2.0 - 5.4 clients used - 20000-20400, 30000-30300,
// 40000-40300, 50000-50400 - the engine's six returns are rearranged into
// that layout. Vanilla has no suggested group sizes and no dailies, so
// suggestedGroup is 0 and isDaily nil, what those clients return for a quest
// without either; questID is the row's quest, 0 for a header (as
// C_QuestLog.GetQuestIDForLogIndex) and nil for an out-of-range index.
// Every other caller gets the engine's layout: vanilla addons, FrameXML, and
// addons for the later layouts (6.0+ and the Classic re-releases dropped
// questTag and moved questID), which this does not produce.

#include "Game.h"
#include "Offsets.h"
#include "api/CallerInterface.h"
#include "quest/Log.h"

namespace Quest::LogTitle {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);

ScriptFn_t g_orig = nullptr;

// The Interface numbers the original 2.0 - 5.4 clients used; their last
// patches (2.4.3, 3.3.5, 4.3.4, 5.4.8) are 20400, 30300, 40300 and 50400.
bool UsesTbcLayout(int iface) {
    return (iface >= 20000 && iface <= 20400) || (iface >= 30000 && iface <= 30300) ||
           (iface >= 40000 && iface <= 40300) || (iface >= 50000 && iface <= 50400);
}

int __fastcall GetQuestLogTitle_h(void *L) {
    const int n = g_orig(L);
    if (n != 6 || !UsesTbcLayout(Api::CallerInterface::Get(L)))
        return n;

    // The six results are the top of the stack; title is 5 below the top.
    const int title = Game::Lua::GetTop(L) - 5;
    Game::Lua::PushNumber(L, 0.0);   // suggestedGroup,
    Game::Lua::Insert(L, title + 3); // moved in before isHeader
    Game::Lua::PushNil(L);           // isDaily
    const int questID = Quest::Log::QuestIDAt(static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1);
    if (questID < 0)
        Game::Lua::PushNil(L);
    else
        Game::Lua::PushNumber(L, static_cast<double>(questID));
    return 9;
}

const Game::HookAutoRegister _hook{Offsets::FUN_SCRIPT_GET_QUEST_LOG_TITLE,
                                   reinterpret_cast<void *>(&GetQuestLogTitle_h),
                                   reinterpret_cast<void **>(&g_orig)};

} // namespace

} // namespace Quest::LogTitle
