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

#include "Cache.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Quest::LeaderBoard {

// `GetQuestLogLeaderBoardID(objectiveIndex [, questIndex])` →
//   (id, kind) for whichever objective the existing `GetQuestLogLeaderBoard`
//   returns at the same indices, or nothing if `objectiveIndex` is out of
//   range / the quest's data hasn't been cached yet.
//
// Companion to `GetQuestLogLeaderBoard` — vanilla's call returns the
// formatted text (`"Young Stranglethorn Tiger slain: 4/10"`) and type
// string but throws the entry ID away. This call surfaces the same ID
// the server sent in SMSG_QUEST_QUERY_RESPONSE, so addons can resolve to
// a specific creature/gameobject/item without parsing the localized text.
//
// Arg shape matches the engine's actual `Script_GetQuestLogLeaderBoard`
// at 0x004E0110, not its usage string (which misleadingly says
// `(index)`): the function checks `lua_isnumber(L, 2)` and either uses
// that as the quest-log entry index, or falls back to
// `VAR_QUEST_LOG_SELECTED_QUEST_ID` when omitted. We mirror both forms
// so callers can pair `GetQuestLogLeaderBoardID` with either invocation
// of the original.
//
// Iteration mirrors the engine: walk the NPC/GO array (`+0x149C`) first,
// skipping zero slots, then the item array (`+0x14BC`). 1-based `objIdx`
// counts only non-empty slots.
//
// `id` is always positive (the raw NPCOrGO field is signed, but we strip
// the sign here and surface the type via `kind` instead — matching the
// API shape the user asked for). `kind` is one of `"monster"`, `"object"`,
// `"item"`. Reputation / event-text-only objectives produce nothing —
// they have no entry ID to return.
static int __fastcall Script_GetQuestLogLeaderBoardID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L,
            "Usage: GetQuestLogLeaderBoardID(objectiveIndex [, questIndex])");
        return 0;
    }
    const int objIdx = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (objIdx < 1)
        return 0;

    int questID = 0;
    if (Game::Lua::IsNumber(L, 2)) {
        const int questIdx = static_cast<int>(Game::Lua::ToNumber(L, 2));
        if (questIdx < 1)
            return 0;
        const int activeCount = *reinterpret_cast<const int *>(
            static_cast<uintptr_t>(Offsets::VAR_QUEST_LOG_ENTRY_COUNT));
        if (questIdx > activeCount)
            return 0;
        const auto *entry = reinterpret_cast<const uint8_t *>(
            static_cast<uintptr_t>(Offsets::VAR_QUEST_LOG_ENTRIES)) +
            (questIdx - 1) * Offsets::OFF_QUEST_LOG_ENTRY_STRIDE;
        if (*reinterpret_cast<const void *const *>(
                entry + Offsets::OFF_QUEST_LOG_ENTRY_HEADER_PTR) != nullptr) {
            return 0; // header row — no objectives
        }
        questID = *reinterpret_cast<const int *>(
            entry + Offsets::OFF_QUEST_LOG_ENTRY_QUEST_ID);
    } else {
        // Single-arg form: use whatever quest is currently selected via
        // `SelectQuestLogEntry`. Mirrors the engine's fallback at the
        // `mov eax, [0x00BB7480]` path right after the `lua_isnumber(L, 2)`
        // gate in `Script_GetQuestLogLeaderBoard`.
        questID = *reinterpret_cast<const int *>(
            static_cast<uintptr_t>(Offsets::VAR_QUEST_LOG_SELECTED_QUEST_ID));
    }
    if (questID <= 0)
        return 0;

    const uint8_t *record = Quest::Cache::Peek(static_cast<uint32_t>(questID));
    if (record == nullptr)
        return 0;

    int matched = 0;

    // NPC/GO array. Engine's iteration at 0x004E04A4 skips zero slots
    // when counting toward objIdx, so we do too.
    const auto *npcOrGo = reinterpret_cast<const int32_t *>(
        record + Quest::Cache::OFF_REQUIRED_NPC_OR_GO);
    for (int i = 0; i < Quest::Cache::OBJECTIVE_COUNT; ++i) {
        if (npcOrGo[i] == 0)
            continue;
        if (++matched == objIdx) {
            const bool isGameObject = (npcOrGo[i] < 0);
            const int32_t id = isGameObject ? -npcOrGo[i] : npcOrGo[i];
            Game::Lua::PushNumber(L, static_cast<double>(id));
            Game::Lua::PushString(L, isGameObject ? "object" : "monster");
            return 2;
        }
    }

    const auto *items = reinterpret_cast<const uint32_t *>(
        record + Quest::Cache::OFF_REQUIRED_ITEM);
    for (int i = 0; i < Quest::Cache::OBJECTIVE_COUNT; ++i) {
        if (items[i] == 0)
            continue;
        if (++matched == objIdx) {
            Game::Lua::PushNumber(L, static_cast<double>(items[i]));
            Game::Lua::PushString(L, "item");
            return 2;
        }
    }

    return 0;
}

static const Game::Doc::Field kGetQuestLogLeaderBoardIDArgs[] = {
    Game::Doc::Req("objectiveIndex", "luaIndex",
                   "Counts creature and game object objectives first, then items."),
    Game::Doc::Opt("questIndex", "luaIndex", nullptr,
                   "Position in the quest log; without it, the selected quest."),
};
static const Game::Doc::Field kGetQuestLogLeaderBoardIDRets[] = {
    Game::Doc::Opt("id", "number", nullptr,
                   "The creature, game object or item ID; nil, with kind, when there is none."),
    Game::Doc::Opt("kind", "string", nullptr, "\"monster\", \"object\" or \"item\"."),
};
static const Game::Doc::Function kGetQuestLogLeaderBoardID{
    "Returns the creature, game object or item that a quest objective needs.",
    kGetQuestLogLeaderBoardIDArgs, kGetQuestLogLeaderBoardIDRets, "QuestLogGlobals", true};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetQuestLogLeaderBoardID",
                                      &Script_GetQuestLogLeaderBoardID,
                                      &kGetQuestLogLeaderBoardID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Quest::LeaderBoard
