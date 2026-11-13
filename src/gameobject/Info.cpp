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

// `C_GameObjectInfo.GetGameObjectInfoByID(gameObjectID)` -> table | nil
// `C_GameObjectInfo.RequestLoadGameObjectByID(gameObjectID)` -> bool
//
// The gameobject analog of `C_CreatureInfo`. Reads the client-side
// gameobject cache (gameobjectcache.wdb, fed by
// SMSG_GAMEOBJECT_QUERY_RESPONSE) by GO entry ID:
//
//   {
//     gameObjectID = <id>,
//     name         = "Mesa Elevator",
//     type         = 11,      -- GameObjectType (0=door,3=chest,…)
//     displayID    = 360,     -- model display ID
//   }
//
// `GetGameObjectInfoByID` is a synchronous peek (nil if not cached);
// `RequestLoadGameObjectByID` fetches an uncached GO and fires
// `GAMEOBJECT_DATA_LOAD_RESULT(gameObjectID, success)` when it lands.
//
// The GO cache is a sibling class to the creature cache (its own
// `_GetRecord` and response parser), but it rides the same shared
// `Cache::QueryLoad` dispatcher. Field offsets: name@+0x08 verified
// (FUN_0062dc10 reads `[block+0x08]`), type@+0x00 / displayID@+0x04 from
// the wire/WDB field order. See Offsets.h `OFF_GAMEOBJECT_*`.

#include "Game.h"
#include "Offsets.h"
#include "cache/QueryLoad.h"
#include "event/Custom.h"

#include <cstdint>

namespace GameObject::Info {

namespace {

constexpr const char *kGameObjectDataLoadResult = "GAMEOBJECT_DATA_LOAD_RESULT";
const Game::Doc::Field kLoadResultPayload[] = {
    Game::Doc::Req("gameObjectID", "number"),
    Game::Doc::Opt("success", "number", nullptr,
                   "1 when the data loaded, nil when the server did not send it."),
};
const Game::Doc::Event kLoadResultDoc{
    "GameObjectInfo",
    "Fires when a game object asked for with C_GameObjectInfo.RequestLoadGameObjectByID "
    "loads or fails to load.",
    kLoadResultPayload};
const Event::Custom::AutoReserve _reserveLoadResult{kGameObjectDataLoadResult,
                                                    &kLoadResultDoc};

using GetCacheRecord_t = const uint8_t *(__thiscall *)(void *cache, uint32_t id,
                                                       const uint64_t *guid, void *callback,
                                                       void *userData, char dedup);

// Peek the GO cache (no network query).
const uint8_t *PeekGameObject(uint32_t goID) {
    auto fn = reinterpret_cast<GetCacheRecord_t>(Offsets::FUN_GAMEOBJECT_GET_RECORD);
    auto *cache = reinterpret_cast<void *>(Offsets::VAR_GAMEOBJECT_CACHE);
    const uint64_t zeroGuid = 0;
    return fn(cache, goID, &zeroGuid, nullptr, nullptr, 0);
}

int __fastcall Script_GetGameObjectInfoByID(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0; // nil
    const int goID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (goID <= 0)
        return 0;

    const uint8_t *rec = PeekGameObject(static_cast<uint32_t>(goID));
    if (rec == nullptr)
        return 0; // not cached -> nil

    const uint32_t type = Game::Read<uint32_t>(rec, Offsets::OFF_GAMEOBJECT_TYPE);
    const uint32_t displayID =
        Game::Read<uint32_t>(rec, Offsets::OFF_GAMEOBJECT_DISPLAYID);
    const char *name = Game::Read<const char *>(rec, Offsets::OFF_GAMEOBJECT_NAME);

    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "gameObjectID", goID);
    Game::Lua::SetFieldString(L, "name", name); // NULL coerced to ""
    Game::Lua::SetFieldNumber(L, "type", type);
    Game::Lua::SetFieldNumber(L, "displayID", displayID);
    return 1;
}

int __fastcall Script_RequestLoadGameObjectByID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const int goID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (goID <= 0) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    auto *cache = reinterpret_cast<void *>(Offsets::VAR_GAMEOBJECT_CACHE);
    const bool accepted = Cache::QueryLoad::RequestLoad(cache, static_cast<uint32_t>(goID));
    Game::Lua::PushBool(L, accepted);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kIDArgs[] = {
    Game::Doc::Req("gameObjectID", "number"),
};

const Game::Doc::Field kDataFields[] = {
    Game::Doc::Req("gameObjectID", "number"),
    Game::Doc::Req("name", "string"),
    Game::Doc::Req("type", "number", "The game object type, such as 0 for a door or 3 for a "
                                     "chest."),
    Game::Doc::Req("displayID", "number"),
};
const Game::Doc::Structure kGameObjectData{"GameObjectData", "GameObjectInfo", kDataFields,
                                           "A game object as the game object cache holds it."};
const Game::Doc::Field kInfoRets[] = {
    Game::Doc::Opt("gameObjectInfo", "GameObjectData", nullptr,
                   "Nil when the game object is not cached."),
};
const Game::Doc::Function kGetGameObjectInfoByID{
    "Returns a cached game object's name, type and model; it never asks the server.",
    kIDArgs, kInfoRets, nullptr, true};

const Game::Doc::Field kRequestRets[] = {
    Game::Doc::Req("accepted", "bool", "False for bad input or too many pending requests."),
};
const Game::Doc::Function kRequestLoadGameObjectByID{
    "Asks the server for a game object that is not cached; GAMEOBJECT_DATA_LOAD_RESULT fires "
    "with the answer.",
    kIDArgs, kRequestRets, nullptr, true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_GameObjectInfo", "GetGameObjectInfoByID",
                                     &Script_GetGameObjectInfoByID, &kGetGameObjectInfoByID);
    Game::Lua::RegisterTableFunction("C_GameObjectInfo", "RequestLoadGameObjectByID",
                                     &Script_RequestLoadGameObjectByID,
                                     &kRequestLoadGameObjectByID);
    Cache::QueryLoad::Register(reinterpret_cast<void *>(Offsets::VAR_GAMEOBJECT_CACHE),
                               &_reserveLoadResult, Offsets::FUN_GAMEOBJECT_GET_RECORD);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace GameObject::Info
