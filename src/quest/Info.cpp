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

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <iterator>

// `C_QuestLog.GetInfo(questLogIndex)` and `C_QuestLog.GetQuestLogTitle(
// questLogIndex)` — the modern table and 17-tuple shapes of one quest-log row.
//
// Both run the engine's own `Script_GetQuestLogTitle` (`0x004DF930`) and
// repackage its six returns, so the title, level, header / collapse gates and
// the complete / failed test are exactly what stock `GetQuestLogTitle` answers.
// The rest is what 1.12 has no data for: no dailies, tasks, bounties, scaling,
// campaigns or POIs, and the vanilla quest query response carries no
// SuggestedPlayers (see Quest::Cache +0x14), so those fields carry their
// "plain quest" values.
namespace Quest::Info {

namespace {

using ScriptFn = int(__fastcall *)(void *L);

// Enum.QuestFrequency.Default / Enum.QuestClassification.Normal.
constexpr int kFrequencyDefault = 0;
constexpr int kClassificationNormal = 7;

// Engine result slots after `RunEngineTitle` (stack indices 2..7).
constexpr int kTitle = 2;
constexpr int kLevel = 3;
constexpr int kIsHeader = 5;
constexpr int kIsCollapsed = 6;
constexpr int kIsComplete = 7;

// Validates the 1-based index, then leaves `[1] = index, [2..7] = the engine's
// six returns` on the stack. Returns the 0-based row, or -1 (stack untouched
// beyond arg 1) when the index is not a row of the log. The results stay on
// the stack while callers build their output so the title string can't be
// collected mid-build.
int RunEngineTitle(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return -1;
    const int row = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    const int total = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_QUEST_LOG_ENTRY_COUNT));
    if (row < 0 || row >= total)
        return -1;

    Game::Lua::SetTop(L, 0);
    Game::Lua::PushNumber(L, static_cast<double>(row + 1));
    reinterpret_cast<ScriptFn>(
        static_cast<uintptr_t>(Offsets::FUN_SCRIPT_GET_QUEST_LOG_TITLE))(L);
    return row;
}

// questID of the row, 0 for a header (the `+8` header gate, as
// `C_QuestLog.GetQuestIDForLogIndex`).
int QuestIDForRow(int row) {
    auto *entry = reinterpret_cast<const uint8_t *>(
                      static_cast<uintptr_t>(Offsets::VAR_QUEST_LOG_ENTRIES)) +
                  row * Offsets::OFF_QUEST_LOG_ENTRY_STRIDE;
    if (*reinterpret_cast<const void *const *>(
            entry + Offsets::OFF_QUEST_LOG_ENTRY_HEADER_PTR) != nullptr)
        return 0;
    return *reinterpret_cast<const int *>(entry + Offsets::OFF_QUEST_LOG_ENTRY_QUEST_ID);
}

bool IsSet(void *L, int idx) { return Game::Lua::Type(L, idx) != Game::Lua::TYPE_NIL; }

} // namespace

static int __fastcall Script_GetInfo(void *L) {
    const int row = RunEngineTitle(L);
    if (row < 0)
        return 0; // nil — not a row of the log

    const char *title = Game::Lua::ToString(L, kTitle);
    const double level = Game::Lua::ToNumber(L, kLevel);
    const bool isHeader = IsSet(L, kIsHeader);

    Game::Lua::NewTable(L);
    Game::Lua::SetFieldString(L, "title", title);
    Game::Lua::SetFieldNumber(L, "questLogIndex", row + 1);
    Game::Lua::SetFieldNumber(L, "questID", QuestIDForRow(row));
    Game::Lua::SetFieldNumber(L, "level", level);
    Game::Lua::SetFieldNumber(L, "difficultyLevel", level);
    Game::Lua::SetFieldNumber(L, "suggestedGroup", 0);
    Game::Lua::SetFieldNumber(L, "frequency", kFrequencyDefault);
    Game::Lua::SetFieldBool(L, "isHeader", isHeader);
    Game::Lua::SetFieldBool(L, "useMinimalHeader", false);
    Game::Lua::SetFieldBool(L, "sortAsNormalQuest", false);
    Game::Lua::SetFieldBool(L, "isCollapsed", IsSet(L, kIsCollapsed));
    Game::Lua::SetFieldBool(L, "startEvent", false);
    Game::Lua::SetFieldBool(L, "isTask", false);
    Game::Lua::SetFieldBool(L, "isBounty", false);
    Game::Lua::SetFieldBool(L, "isStory", false);
    Game::Lua::SetFieldBool(L, "isScaling", false);
    Game::Lua::SetFieldBool(L, "isOnMap", false);
    Game::Lua::SetFieldBool(L, "hasLocalPOI", false);
    Game::Lua::SetFieldBool(L, "isHidden", false);
    Game::Lua::SetFieldBool(L, "isAutoComplete", false);
    Game::Lua::SetFieldBool(L, "overridesSortOrder", false);
    Game::Lua::SetFieldBool(L, "readyForTranslation", true);
    Game::Lua::SetFieldBool(L, "isInternalOnly", false);
    Game::Lua::SetFieldBool(L, "isAbandonOnDisable", false);
    Game::Lua::SetFieldNumber(L, "questClassification", kClassificationNormal);
    return 1;
}

static int __fastcall Script_GetQuestLogTitle(void *L) {
    const int row = RunEngineTitle(L);
    if (row < 0)
        return 0; // nothing — not a row of the log

    const int questID = QuestIDForRow(row);
    Game::Lua::PushValue(L, kTitle);                            // title
    Game::Lua::PushValue(L, kLevel);                            // level
    Game::Lua::PushNumber(L, 0);                                // suggestedGroup
    Game::Lua::PushBool(L, IsSet(L, kIsHeader));                // isHeader
    Game::Lua::PushBool(L, IsSet(L, kIsCollapsed));             // isCollapsed
    Game::Lua::PushValue(L, kIsComplete);                       // isComplete (1 / -1 / nil)
    Game::Lua::PushNumber(L, kFrequencyDefault);                // frequency
    Game::Lua::PushNumber(L, static_cast<double>(questID));     // questID
    Game::Lua::PushBool(L, false);                              // startEvent
    Game::Lua::PushBool(L, false);                              // displayQuestID
    Game::Lua::PushBool(L, false);                              // isOnMap
    Game::Lua::PushBool(L, false);                              // hasLocalPOI
    Game::Lua::PushBool(L, false);                              // isTask
    Game::Lua::PushBool(L, false);                              // isBounty
    Game::Lua::PushBool(L, false);                              // isStory
    Game::Lua::PushBool(L, false);                              // isHidden
    Game::Lua::PushBool(L, false);                              // isScaling
    return 17;
}

// --- Documentation ----------------------------------------------------------

namespace {

const Game::Doc::Field kIndexArgs[] = {
    Game::Doc::Req("questLogIndex", "luaIndex",
                   "Position in the quest log, headers included, as GetQuestLogTitle counts it."),
};

const Game::Doc::Field kQuestInfoFields[] = {
    Game::Doc::Req("title", "string", "Quest name, or the header text for a header row."),
    Game::Doc::Req("questLogIndex", "luaIndex", "Position in the quest log."),
    Game::Doc::Req("questID", "number", "The quest ID; 0 for a header row."),
    Game::Doc::Opt("campaignID", "number"),
    Game::Doc::Req("level", "number", "Quest level."),
    Game::Doc::Req("difficultyLevel", "number", "Same as level."),
    Game::Doc::Req("suggestedGroup", "number", "Always 0; the server does not send it."),
    Game::Doc::Opt("frequency", "QuestFrequency", nullptr, "Always Default."),
    Game::Doc::Req("isHeader", "bool", "True for a category header row."),
    Game::Doc::Req("useMinimalHeader", "bool"),
    Game::Doc::Req("sortAsNormalQuest", "bool"),
    Game::Doc::Req("isCollapsed", "bool", "True for a collapsed header row."),
    Game::Doc::Req("startEvent", "bool"),
    Game::Doc::Req("isTask", "bool"),
    Game::Doc::Req("isBounty", "bool"),
    Game::Doc::Req("isStory", "bool"),
    Game::Doc::Req("isScaling", "bool"),
    Game::Doc::Req("isOnMap", "bool"),
    Game::Doc::Req("hasLocalPOI", "bool"),
    Game::Doc::Req("isHidden", "bool"),
    Game::Doc::Req("isAutoComplete", "bool"),
    Game::Doc::Req("overridesSortOrder", "bool"),
    Game::Doc::Opt("readyForTranslation", "bool", "true"),
    Game::Doc::Req("isInternalOnly", "bool"),
    Game::Doc::Req("isAbandonOnDisable", "bool"),
    Game::Doc::Opt("headerSortKey", "number"),
    Game::Doc::Req("questClassification", "QuestClassification", "Always Normal."),
};
const Game::Doc::Structure kQuestInfoStruct{
    "QuestInfo", "QuestLog", kQuestInfoFields, "One row of the quest log."};

const Game::Doc::Field kGetInfoRets[] = {
    Game::Doc::Opt("info", "QuestInfo", nullptr, "Nil when the index is not a row of the log."),
};
const Game::Doc::Function kGetInfo{
    "A table that describes one row of the quest log.", kIndexArgs, kGetInfoRets};

const Game::Doc::Field kTitleRets[] = {
    Game::Doc::Req("title", "string", "Quest name, or the header text for a header row."),
    Game::Doc::Req("level", "number", "Quest level."),
    Game::Doc::Req("suggestedGroup", "number", "Always 0; the server does not send it."),
    Game::Doc::Req("isHeader", "bool", "True for a category header row."),
    Game::Doc::Req("isCollapsed", "bool", "True for a collapsed header row."),
    Game::Doc::Opt("isComplete", "number", nullptr,
                   "1 when the objectives are done, -1 when the quest failed, else nil."),
    Game::Doc::Req("frequency", "QuestFrequency", "Always Default."),
    Game::Doc::Req("questID", "number", "The quest ID; 0 for a header row."),
    Game::Doc::Req("startEvent", "bool"),
    Game::Doc::Req("displayQuestID", "bool"),
    Game::Doc::Req("isOnMap", "bool"),
    Game::Doc::Req("hasLocalPOI", "bool"),
    Game::Doc::Req("isTask", "bool"),
    Game::Doc::Req("isBounty", "bool"),
    Game::Doc::Req("isStory", "bool"),
    Game::Doc::Req("isHidden", "bool"),
    Game::Doc::Req("isScaling", "bool"),
};
const Game::Doc::Function kGetQuestLogTitle{
    "The title, level, header state and completion state of one row of the quest log.",
    kIndexArgs, kTitleRets};

constexpr Game::Lua::EnumIntegerEntry kQuestFrequencyEntries[] = {
    {"Default", 0},
    {"Daily", 1},
    {"Weekly", 2},
    {"ResetByScheduler", 3},
};

constexpr Game::Lua::EnumIntegerEntry kQuestClassificationEntries[] = {
    {"Important", 0},  {"Legendary", 1},      {"Campaign", 2}, {"Calling", 3},
    {"Meta", 4},       {"Recurring", 5},      {"Questline", 6}, {"Normal", 7},
    {"BonusObjective", 8}, {"Threat", 9},     {"WorldQuest", 10},
};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_QuestLog", "GetInfo", &Script_GetInfo, &kGetInfo);
    Game::Lua::RegisterTableFunction("C_QuestLog", "GetQuestLogTitle",
                                     &Script_GetQuestLogTitle, &kGetQuestLogTitle);
    Game::Lua::RegisterIntegerEnum("Enum", "QuestFrequency", kQuestFrequencyEntries,
                                   static_cast<int>(std::size(kQuestFrequencyEntries)),
                                   "QuestLog");
    Game::Lua::RegisterIntegerEnum("Enum", "QuestClassification", kQuestClassificationEntries,
                                   static_cast<int>(std::size(kQuestClassificationEntries)),
                                   "QuestLog");
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Quest::Info
