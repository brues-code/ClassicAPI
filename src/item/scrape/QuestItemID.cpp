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

namespace Item::QuestItemID {

namespace {

// Engine helper used by `Script_GetQuestItemLink` /
// `Script_GetQuestItemInfo` to translate (type, 0-based index) into
// an itemID. Looks up the active quest-offer state — NOT the quest
// log; this is the "QuestFrame" view of a quest you're currently
// being offered. Accepts "reward" (max 4) and "choice" (max 6).
using QuestItemResolver_t = int(__fastcall *)(const char *type, int index0);

} // namespace

// `GetQuestItemID(type, index)` — itemID companion to
// `GetQuestItemLink`. nil for any failure (bad type, OOR index,
// empty slot, no quest currently offered).
static int __fastcall Script_GetQuestItemID(void *L) {
    if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: GetQuestItemID(\"reward\"|\"choice\", index)");
        return 0;
    }
    const char *type = Game::Lua::ToString(L, 1);
    const int index = static_cast<int>(Game::Lua::ToNumber(L, 2)) - 1;
    if (type == nullptr || *type == '\0' || index < 0)
        return 0;

    auto resolver = reinterpret_cast<QuestItemResolver_t>(Offsets::FUN_QUEST_ITEM_RESOLVER);
    const int itemID = resolver(type, index);
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
                   "Nil when no quest is offered or the slot is empty."),
};
static const Game::Doc::Function kGetQuestItemID{
    "Returns a reward item of the quest that the quest window offers.", kArgs, kRets,
    "ItemGlobals", true};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetQuestItemID", &Script_GetQuestItemID,
                                      &kGetQuestItemID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::QuestItemID
