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
#include "event/Custom.h"
#include "quest/Cache.h"

#include <cstdint>

namespace Quest::Data {

static constexpr const char *kQuestDataLoadResult = "QUEST_DATA_LOAD_RESULT";
static const Game::Doc::Field kLoadResultPayload[] = {
    Game::Doc::Req("questID", "number"),
    Game::Doc::Opt("success", "number", nullptr, "1 when the data loaded, else nil."),
};
static const Game::Doc::Event kQuestDataLoadResultDoc{
    "QuestLog", "Fires when quest data that C_QuestLog.RequestLoadQuestByID asked for loads.",
    kLoadResultPayload};
static const Event::Custom::AutoReserve _reserveQuestDataLoadResult{kQuestDataLoadResult,
                                                                    &kQuestDataLoadResultDoc};

// Same `__stdcall(userData, success)` shape as `Item::Data::ItemLoadCallback`.
// The engine's invocation pattern at 0x00562EEB et al. is:
//   mov ecx, [entry+0x18]   ; userData (= arg4 we passed to _GetRecord)
//   push 1                  ; success (1/0)
//   push ecx                ; userData
//   call [entry+0x08]       ; this callback
// `ret 8` cleans both args. We encode the questID into `userData` at
// request time so the callback knows which quest completed.
static void __stdcall QuestLoadCallback(void *userData, int success) {
    const auto questID =
        static_cast<int>(reinterpret_cast<uintptr_t>(userData));
    Event::Custom::FireIdSuccess(_reserveQuestDataLoadResult.Slot(), questID,
                                 success != 0);
}

static void RequestAndMaybeNotify(uint32_t questID) {
    const bool wasCached = (Quest::Cache::Peek(questID) != nullptr);
    void *userData = reinterpret_cast<void *>(static_cast<uintptr_t>(questID));
    Quest::Cache::Lookup(questID, reinterpret_cast<void *>(&QuestLoadCallback), userData);
    // Cache-hit-already-loaded path: engine returns the record without
    // invoking our callback. Fire the event ourselves so addons get the
    // notification regardless of cache state, matching modern semantics.
    if (wasCached) {
        Event::Custom::FireIdSuccess(_reserveQuestDataLoadResult.Slot(),
                                     static_cast<int>(questID), true);
    }
}

static const Game::Doc::Field kQuestIDArgs[] = {
    Game::Doc::Req("questID", "number"),
};
static const Game::Doc::Function kRequestLoadQuestByID{
    "Requests the data of a quest; QUEST_DATA_LOAD_RESULT fires when it is ready.", kQuestIDArgs,
    {}};

static int __fastcall Script_RequestLoadQuestByID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_QuestLog.RequestLoadQuestByID(questID)");
        return 0;
    }
    const int questID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (questID <= 0)
        return 0;
    RequestAndMaybeNotify(static_cast<uint32_t>(questID));
    return 0;
}

static const Game::Doc::Field kIsQuestDataCachedByIDRets[] = {
    Game::Doc::Req("isCached", "bool"),
};
static const Game::Doc::Function kIsQuestDataCachedByID{
    "Returns whether the data of a quest is loaded.", kQuestIDArgs, kIsQuestDataCachedByIDRets,
    nullptr, true};

static int __fastcall Script_IsQuestDataCachedByID(void *L) {
    const int questID =
        Game::Lua::IsNumber(L, 1) ? static_cast<int>(Game::Lua::ToNumber(L, 1)) : 0;
    const bool cached =
        (questID > 0) && (Quest::Cache::Peek(static_cast<uint32_t>(questID)) != nullptr);
    Game::Lua::PushBool(L, cached);
    return 1;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_QuestLog", "RequestLoadQuestByID",
                                     &Script_RequestLoadQuestByID, &kRequestLoadQuestByID);
    Game::Lua::RegisterTableFunction("C_QuestLog", "IsQuestDataCachedByID",
                                     &Script_IsQuestDataCachedByID, &kIsQuestDataCachedByID);
    // Event name reserved at file scope via `AutoReserve`.
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Quest::Data
