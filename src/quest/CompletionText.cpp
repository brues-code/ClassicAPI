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

// `GetQuestLogCompletionText([index])` — the text the quest log shows for a
// quest that is ready to turn in.
//
// 4.3.4's Script_GetQuestLogCompletionText (0x0095E370) takes the quest at
// the 1-based log `index`, or the selected quest without one (0 for a header
// or an out-of-range index, as GetQuestIDArg does). It looks the quest up in
// the cache without a query, then expands the cached completion text, or the
// objectives when that is empty, for the active player. The 1.12 quest-query
// response carries no completion text (see Quest::Cache::OFF_END_TEXT), so
// every quest takes the objectives branch. The expansion buffer is
// Script_GetQuestLogQuestText's, not 4.3.4's 3000 bytes.

#include "Game.h"
#include "Offsets.h"
#include "quest/Cache.h"

#include <cstdint>

namespace Quest::CompletionText {

namespace {

uint32_t QuestIDArg(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return Game::Read<uint32_t>(Offsets::VAR_QUEST_LOG_SELECTED_QUEST_ID);

    const int index = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    const int total = Game::Read<int>(Offsets::VAR_QUEST_LOG_ENTRY_COUNT);
    if (index < 0 || index >= total)
        return 0;
    const auto *entry = reinterpret_cast<const uint8_t *>(
                            static_cast<uintptr_t>(Offsets::VAR_QUEST_LOG_ENTRIES)) +
                        index * Offsets::OFF_QUEST_LOG_ENTRY_STRIDE;
    if (Game::Read<const void *>(entry, Offsets::OFF_QUEST_LOG_ENTRY_HEADER_PTR) != nullptr)
        return 0;
    return Game::Read<uint32_t>(entry, Offsets::OFF_QUEST_LOG_ENTRY_QUEST_ID);
}

int __fastcall Script_GetQuestLogCompletionText(void *L) {
    const uint8_t *quest = Quest::Cache::Peek(QuestIDArg(L));
    if (quest == nullptr)
        return 0;

    char text[Quest::Cache::EXPANDED_TEXT_SIZE];
    Quest::Cache::ExpandText(reinterpret_cast<const char *>(quest + Quest::Cache::OFF_OBJECTIVES),
                             text, sizeof(text));
    Game::Lua::PushString(L, text);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Opt("questLogIndex", "number", nullptr,
                   "The quest log index. Without it, the selected quest."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("completionText", "string", nullptr,
                   "The text to show when the quest is ready to turn in."),
};
const Game::Doc::Function kGetQuestLogCompletionText{
    "Returns the text that the quest log shows when a quest is ready to turn in.",
    kArgs, kRets, "QuestLog"};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetQuestLogCompletionText",
                                      &Script_GetQuestLogCompletionText,
                                      &kGetQuestLogCompletionText);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Quest::CompletionText
