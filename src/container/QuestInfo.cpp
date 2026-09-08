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

// `C_Container.GetContainerItemQuestInfo(containerIndex, slotIndex)` —
// the quest state of a bag slot, as one table:
//
//   { isQuestItem = bool, questID = number|nil, isActive = bool }
//
// This is what draws the two overlays on a bag button: a `questID` the
// player has not accepted yet gets the exclamation mark, and either an
// accepted `questID` or a plain quest item gets the question-mark
// border.
//
// The three fields come from three places:
//
//   isQuestItem  the item's class is Quest — an item that exists to be
//                a quest objective.
//   questID      the quest the item BEGINS (`m_startQuest` in the
//                cached item record). Absent, not zero, when the item
//                starts nothing — callers test `if questInfo.questID`.
//   isActive     that started quest is already in the quest log.
//
// The table is always returned, even for an empty slot or an item whose
// data has not arrived yet, so callers can index it without a nil test.
// An uncached item warms the cache, so the next bag update draws the
// overlay the first hover could not.

#include "Game.h"
#include "Offsets.h"
#include "item/Data.h"
#include "item/ID.h"
#include "item/Location.h"
#include "item/Record.h"
#include "quest/Log.h"

#include <cstdint>

namespace Container::QuestInfo {

namespace {

// ItemClass.dbc row 12. Only this file cares which class means "quest",
// so the code stays local per the single-use rule in the offsets policy.
constexpr uint32_t kItemClassQuest = 12;

int __fastcall Script_C_Container_GetContainerItemQuestInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L,
            "Usage: C_Container.GetContainerItemQuestInfo(containerIndex, slotIndex)");
        return 0;
    }
    const int bagID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const int slotIndex = static_cast<int>(Game::Lua::ToNumber(L, 2));

    // Resolve before building the table: `ResolveBag` stomps stack
    // slots 1 and 2 through the engine's `PackBagSlot`, so nothing we
    // want to keep may be on the stack across this call.
    const int itemID =
        Item::ID::FromCGItem(Item::Location::ResolveBag(L, bagID, slotIndex));

    bool isQuestItem = false;
    uint32_t startQuest = 0;
    if (itemID != 0) {
        auto *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
        if (record == nullptr) {
            // Cold cache. Report the empty answer now and fetch, so the
            // overlay is right on the next bag update.
            Item::Data::WarmCache(static_cast<uint32_t>(itemID));
        } else {
            isQuestItem = *reinterpret_cast<const uint32_t *>(
                              record + Offsets::OFF_ITEMSTATS_CLASS) ==
                          kItemClassQuest;
            startQuest = *reinterpret_cast<const uint32_t *>(
                record + Offsets::OFF_ITEMSTATS_START_QUEST);
        }
    }

    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldBool(L, "isQuestItem", isQuestItem);
    if (startQuest > 0) {
        Game::Lua::SetFieldNumber(L, "questID",
                                  static_cast<double>(startQuest));
        Game::Lua::SetFieldBool(
            L, "isActive", Quest::Log::IsOnQuest(static_cast<int>(startQuest)));
    } else {
        // `questID` stays absent rather than 0 — the field is nilable and
        // every caller tests it for truth.
        Game::Lua::SetFieldBool(L, "isActive", false);
    }
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Container", "GetContainerItemQuestInfo",
                                     &Script_C_Container_GetContainerItemQuestInfo);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::QuestInfo
