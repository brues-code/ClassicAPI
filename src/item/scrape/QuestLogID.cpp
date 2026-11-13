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
#include "quest/Cache.h"

#include <cstdint>

namespace Item::QuestLogID {

namespace {

// Tiny case-insensitive comparison — we only ever match the two literal
// strings the engine accepts ("reward" / "choice"), so a single-pass
// tolower-as-we-go beats pulling in <cstring>/SStrCmpI for two callers.
bool EqualsIgnoreCase(const char *s, const char *literalLowercase) {
    for (;; ++s, ++literalLowercase) {
        const unsigned char a = static_cast<unsigned char>(*s);
        const unsigned char b = static_cast<unsigned char>(*literalLowercase);
        const unsigned char aLower = (a >= 'A' && a <= 'Z') ? a + 32 : a;
        if (aLower != b)
            return false;
        if (b == 0)
            return true;
    }
}

} // namespace

// `GetQuestLogItemID(type, index)` — itemID companion to
// `GetQuestLogItemLink`. Reads the same quest-cache record the engine
// reads via `[VAR_QUEST_LOG_SELECTED_QUEST_ID]`. Returns nil for
// unknown type, OOR index, empty reward/choice slot, or when the
// currently-selected quest's data isn't cached yet (no
// `SetQuestLogSelection` called, or response packet still in flight).
static int __fastcall Script_GetQuestLogItemID(void *L) {
    if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: GetQuestLogItemID(\"reward\"|\"choice\", index)");
        return 0;
    }
    const char *type = Game::Lua::ToString(L, 1);
    const int index = static_cast<int>(Game::Lua::ToNumber(L, 2)) - 1;
    if (type == nullptr || index < 0)
        return 0;

    const uint32_t questID = *reinterpret_cast<const uint32_t *>(
        Offsets::VAR_QUEST_LOG_SELECTED_QUEST_ID);
    if (questID == 0)
        return 0;

    const uint8_t *record = Quest::Cache::Peek(questID);
    if (record == nullptr)
        return 0;

    int byteOffset = 0;
    if (EqualsIgnoreCase(type, "choice")) {
        if (index >= Quest::Cache::CHOICE_COUNT)
            return 0;
        byteOffset = Quest::Cache::OFF_CHOICE_ITEMS + index * 4;
    } else if (EqualsIgnoreCase(type, "reward")) {
        if (index >= Quest::Cache::REWARD_COUNT)
            return 0;
        byteOffset = Quest::Cache::OFF_REWARD_ITEMS + index * 4;
    } else {
        return 0;
    }

    const int itemID = static_cast<int>(*reinterpret_cast<const uint32_t *>(
        record + byteOffset));
    if (itemID == 0)
        return 0;

    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    return 1;
}

// --- Documentation ----------------------------------------------------------

static const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("type", "cstring", "\"reward\" or \"choice\"."),
    Game::Doc::Req("index", "luaIndex"),
};
static const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("itemID", "number", nullptr,
                   "Nil when no quest is selected, its data is not loaded, or the slot is empty."),
};
static const Game::Doc::Function kGetQuestLogItemID{
    "Returns a reward item of the quest selected in the quest log.", kArgs, kRets,
    "ItemGlobals", true};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetQuestLogItemID", &Script_GetQuestLogItemID,
                                      &kGetQuestLogItemID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::QuestLogID
