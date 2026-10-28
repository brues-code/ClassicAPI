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

// `C_QuestLog.GetQuestDetails(questID)` — single accessor that returns
// everything we know how to read from the quest static-info cache as a
// Lua table. Powers quest-helper-style addons that want one round-trip
// instead of N separate `C_QuestLog.GetTitleForQuestID` / etc. calls.
//
// Returns `nil` if the quest isn't cached. Caller should pair with
// `C_QuestLog.RequestLoadQuestByID` + listen for `QUEST_DATA_LOAD_RESULT`.
// All offsets read here come from the engine's own `Script_GetQuestLog*`
// accessors — see `Cache.h` for the per-field derivation.

#include "Game.h"
#include "Offsets.h"
#include "quest/Cache.h"

#include <cstdint>

namespace Quest::Details {

namespace {

// QuestInfo.dbc — 12-row table holding the localized quest-type tags
// ("Dungeon" / "Raid" / "Group" / "PvP" / "Elite"). Address pair from
// `docs/DBCs.md`.
constexpr uintptr_t VAR_QUESTINFO_DBC_RECORDS = 0x00C0D9CC;
constexpr uintptr_t VAR_QUESTINFO_DBC_COUNT = 0x00C0D9D0;

const char *LookupQuestType(uint32_t row) {
    if (row == 0)
        return nullptr;
    const int count = *reinterpret_cast<const int *>(VAR_QUESTINFO_DBC_COUNT);
    if (count <= 0 || row > static_cast<uint32_t>(count))
        return nullptr;
    const uint8_t *const *records =
        *reinterpret_cast<const uint8_t *const *const *>(
            VAR_QUESTINFO_DBC_RECORDS);
    if (!records)
        return nullptr;
    const uint8_t *record = records[row];
    if (!record)
        return nullptr;
    const int localeIdx =
        Game::Read<int>(Offsets::VAR_LOCALE_INDEX);
    if (localeIdx < 0 || localeIdx > 8)
        return nullptr;
    auto *names = reinterpret_cast<const char *const *>(record + 4);
    return names[localeIdx];
}

// Pushes an array of `{id=itemID, count=N}` tables for each non-zero
// reward slot. `ids[count]` and `counts[count]` are parallel arrays
// in the cache block. Leaves exactly one table on top of the stack.
void PushItemArray(void *L, const uint32_t *ids, const uint32_t *counts,
                   int count) {
    Game::Lua::NewTable(L);
    int next = 1;
    for (int i = 0; i < count; ++i) {
        if (ids[i] == 0)
            continue;
        Game::Lua::PushNumber(L, static_cast<double>(next++));
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldNumber(L, "id", static_cast<double>(ids[i]));
        Game::Lua::SetFieldNumber(L, "count", static_cast<double>(counts[i]));
        Game::Lua::SetTable(L, -3);
    }
}

// Per-objective override text — the 4-slot array at `+0x14DC` with
// 256-byte stride. Index in objective-declaration order (NPC/GO first,
// then item, same enumeration the LeaderBoard accessor walks).
// Returns empty string for slots past the cache's 4-entry capacity
// or for slots with an empty buffer (engine auto-formats those).
const char *OverrideTextFor(const uint8_t *data, int objectiveSlot) {
    if (objectiveSlot < 0 || objectiveSlot >= Quest::Cache::OBJECTIVE_TEXT_COUNT)
        return "";
    return reinterpret_cast<const char *>(
        data + Quest::Cache::OFF_OBJECTIVE_TEXT +
        objectiveSlot * Quest::Cache::OBJECTIVE_TEXT_STRIDE);
}

void PushRequirements(void *L, const uint8_t *data) {
    Game::Lua::NewTable(L);
    int next = 1;
    int overrideSlot = 0;

    auto *npcOrGo = reinterpret_cast<const int32_t *>(
        data + Quest::Cache::OFF_REQUIRED_NPC_OR_GO);
    auto *npcOrGoCount = reinterpret_cast<const uint32_t *>(
        data + Quest::Cache::OFF_REQUIRED_NPC_OR_GO_COUNT);
    for (int i = 0; i < Quest::Cache::OBJECTIVE_COUNT; ++i) {
        const int32_t v = npcOrGo[i];
        if (v == 0)
            continue;
        Game::Lua::PushNumber(L, static_cast<double>(next++));
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldString(L, "kind", v > 0 ? "monster" : "object");
        Game::Lua::SetFieldNumber(L, "id",
                                  static_cast<double>(v > 0 ? v : -v));
        Game::Lua::SetFieldNumber(L, "count",
                                  static_cast<double>(npcOrGoCount[i]));
        Game::Lua::SetFieldString(L, "text",
                                  OverrideTextFor(data, overrideSlot++));
        Game::Lua::SetTable(L, -3);
    }

    auto *items = reinterpret_cast<const uint32_t *>(
        data + Quest::Cache::OFF_REQUIRED_ITEM);
    auto *itemCounts = reinterpret_cast<const uint32_t *>(
        data + Quest::Cache::OFF_REQUIRED_ITEM_COUNT);
    for (int i = 0; i < Quest::Cache::OBJECTIVE_COUNT; ++i) {
        if (items[i] == 0)
            continue;
        Game::Lua::PushNumber(L, static_cast<double>(next++));
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldString(L, "kind", "item");
        Game::Lua::SetFieldNumber(L, "id", static_cast<double>(items[i]));
        Game::Lua::SetFieldNumber(L, "count",
                                  static_cast<double>(itemCounts[i]));
        Game::Lua::SetFieldString(L, "text",
                                  OverrideTextFor(data, overrideSlot++));
        Game::Lua::SetTable(L, -3);
    }
}

// `C_QuestLog.GetNumQuestObjectives(questID)` — the quest's objective
// count (the length `GetQuestDetails(questID).requirements` would have),
// read straight from the cache block with zero Lua table/string
// materialization. Exists so per-step validation loops (guide addons
// bounds-checking objective indices at load time) stay O(1)-cheap even
// with a warm quest cache — materializing full detail tables for hundreds
// of quests inside a login-path loop can exhaust vanilla's fixed Lua
// memory pool. Returns nil when the quest isn't cached (same contract as
// GetQuestDetails).
int __fastcall Script_GetNumQuestObjectives(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L,
            "Usage: C_QuestLog.GetNumQuestObjectives(questID)");
        return 0;
    }
    const int questID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (questID <= 0)
        return 0;
    const uint8_t *data = Quest::Cache::Peek(static_cast<uint32_t>(questID));
    if (data == nullptr)
        return 0;

    int count = 0;
    auto *npcOrGo = reinterpret_cast<const int32_t *>(
        data + Quest::Cache::OFF_REQUIRED_NPC_OR_GO);
    auto *items = reinterpret_cast<const uint32_t *>(
        data + Quest::Cache::OFF_REQUIRED_ITEM);
    for (int i = 0; i < Quest::Cache::OBJECTIVE_COUNT; ++i) {
        if (npcOrGo[i] != 0)
            ++count;
        if (items[i] != 0)
            ++count;
    }
    Game::Lua::PushNumber(L, static_cast<double>(count));
    return 1;
}

int __fastcall Script_GetQuestDetails(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_QuestLog.GetQuestDetails(questID)");
        return 0;
    }
    const int questID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (questID <= 0)
        return 0;

    const uint8_t *data = Quest::Cache::Peek(static_cast<uint32_t>(questID));
    if (data == nullptr)
        return 0;

    const auto u32 = [data](int off) {
        return *reinterpret_cast<const uint32_t *>(data + off);
    };
    const auto i32 = [data](int off) {
        return *reinterpret_cast<const int32_t *>(data + off);
    };
    const auto str = [data](int off) {
        return reinterpret_cast<const char *>(data + off);
    };

    const auto f32 = [data](int off) {
        return *reinterpret_cast<const float *>(data + off);
    };

    Game::Lua::NewTable(L);

    Game::Lua::SetFieldNumber(L, "questID", static_cast<double>(questID));
    Game::Lua::SetFieldString(L, "title", str(Quest::Cache::OFF_TITLE));
    Game::Lua::SetFieldNumber(L, "level",
                              static_cast<double>(i32(Quest::Cache::OFF_LEVEL)));

    if (const char *tag =
            LookupQuestType(u32(Quest::Cache::OFF_QUEST_INFO_ROW))) {
        if (*tag != '\0')
            Game::Lua::SetFieldString(L, "questType", tag);
    }

    // Race / class / skill / timer / suggested-players gates aren't
    // here intentionally — vanilla 1.12's quest-query response leaves
    // those cache slots (+0x14..+0x24) zero-filled; the server enforces
    // those constraints and only ships quests to clients who could
    // accept them. Addons that need that data must source it from an
    // external scraped DB (pfQuest-style).
    const int32_t money = i32(Quest::Cache::OFF_MONEY_DELTA);
    Game::Lua::SetFieldNumber(L, "rewardMoney",
                              money > 0 ? static_cast<double>(money) : 0.0);
    Game::Lua::SetFieldNumber(L, "requiredMoney",
                              money < 0 ? static_cast<double>(-money) : 0.0);
    Game::Lua::SetFieldNumber(
        L, "rewardMoneyAtMaxLevel",
        static_cast<double>(i32(Quest::Cache::OFF_LEVEL_CAP_BONUS_MONEY)));
    Game::Lua::SetFieldNumber(
        L, "rewardSpellID",
        static_cast<double>(i32(Quest::Cache::OFF_REWARD_SPELL_ID)));
    Game::Lua::SetFieldNumber(
        L, "srcItemID",
        static_cast<double>(u32(Quest::Cache::OFF_SRC_ITEM_ID)));

    const uint32_t flags = u32(Quest::Cache::OFF_QUEST_FLAGS);
    Game::Lua::SetFieldNumber(L, "questFlags", static_cast<double>(flags));
    Game::Lua::SetFieldBool(L, "isSharable", (flags & 0x08) != 0);

    Game::Lua::SetFieldString(L, "description",
                              str(Quest::Cache::OFF_DESCRIPTION));
    Game::Lua::SetFieldString(L, "objectives",
                              str(Quest::Cache::OFF_OBJECTIVES));
    Game::Lua::SetFieldString(L, "endText",
                              str(Quest::Cache::OFF_END_TEXT));

    if (const uint32_t mapID = u32(Quest::Cache::OFF_POI_MAP_ID)) {
        Game::Lua::PushString(L, "poi");
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldNumber(L, "mapID", static_cast<double>(mapID));
        Game::Lua::SetFieldNumber(L, "x", static_cast<double>(f32(Quest::Cache::OFF_POI_X)));
        Game::Lua::SetFieldNumber(L, "y", static_cast<double>(f32(Quest::Cache::OFF_POI_Y)));
        Game::Lua::SetFieldNumber(L, "opt",
                                  static_cast<double>(u32(Quest::Cache::OFF_POI_OPT)));
        Game::Lua::SetTable(L, -3);
    }

    Game::Lua::PushString(L, "rewardItems");
    PushItemArray(L,
                  reinterpret_cast<const uint32_t *>(
                      data + Quest::Cache::OFF_REWARD_ITEMS),
                  reinterpret_cast<const uint32_t *>(
                      data + Quest::Cache::OFF_REWARD_ITEMS_COUNT),
                  Quest::Cache::REWARD_COUNT);
    Game::Lua::SetTable(L, -3);

    Game::Lua::PushString(L, "choiceItems");
    PushItemArray(L,
                  reinterpret_cast<const uint32_t *>(
                      data + Quest::Cache::OFF_CHOICE_ITEMS),
                  reinterpret_cast<const uint32_t *>(
                      data + Quest::Cache::OFF_CHOICE_ITEMS_COUNT),
                  Quest::Cache::CHOICE_COUNT);
    Game::Lua::SetTable(L, -3);

    Game::Lua::PushString(L, "requirements");
    PushRequirements(L, data);
    Game::Lua::SetTable(L, -3);

    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_QuestLog", "GetQuestDetails",
                                     &Script_GetQuestDetails);
    Game::Lua::RegisterTableFunction("C_QuestLog", "GetNumQuestObjectives",
                                     &Script_GetNumQuestObjectives);
}

} // namespace

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Quest::Details
