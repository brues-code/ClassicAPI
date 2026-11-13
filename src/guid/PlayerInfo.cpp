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
#include "guid/Guid.h"

#include <cstdint>

namespace Guid::PlayerInfoBindings {

namespace {

// Shared body for every `C_PlayerInfo.GUIDIs*` function. Each
// registered entry stores the `Guid::Type` it tests for; we
// classify the input and push the boolean result.
//
// Lua C closures (`lua_pushcclosure` with upvalues) are the natural
// fit here, but we don't currently expose `RegisterTableFunction`
// with an upvalue-carrying variant. Cheaper to just register one
// thin wrapper per type — they're literally a single test each
// after the shared parse step.
bool TestType(void *L, Type expected) {
    if (!Game::Lua::IsString(L, 1))
        return false;
    uint64_t guid = 0;
    if (!Parse(Game::Lua::ToString(L, 1), &guid))
        return false;
    return Classify(guid) == expected;
}

int PushTypeResult(void *L, Type expected) {
    Game::Lua::PushBool(L, TestType(L, expected));
    return 1;
}

// `C_PlayerInfo.GUIDIsPlayer(guid)` — modern API, returns true iff
// `guid` is a player GUID. Vanilla 1.12 GUIDs encode the type in
// the high 16 bits — players have `0x0000` (low 32 bits = player
// ID). The `guid == 0` sentinel returns false.
int __fastcall Script_GUIDIsPlayer(void *L) {
    return PushTypeResult(L, Type::Player);
}

// `C_PlayerInfo.GUIDIsCreature(guid)` — true iff the GUID is an
// NPC / mob (high prefix `0xF130`). Not a stock modern function
// in `C_PlayerInfo`, but the natural sibling to `GUIDIsPlayer`
// for code that's deciding "is this combat-log row a player or an
// NPC kill?".
int __fastcall Script_GUIDIsCreature(void *L) {
    return PushTypeResult(L, Type::Creature);
}

// `C_PlayerInfo.GUIDIsPet(guid)` — true iff the GUID is a pet
// (high prefix `0xF140`). Distinguishes warlock/hunter pets and
// mage water elementals from their owners.
int __fastcall Script_GUIDIsPet(void *L) {
    return PushTypeResult(L, Type::Pet);
}

// `C_PlayerInfo.GUIDIsGameObject(guid)` — true iff the GUID is a
// game object (high prefix `0xF110`): herb nodes, chests, fishing
// bobbers, doors, etc.
int __fastcall Script_GUIDIsGameObject(void *L) {
    return PushTypeResult(L, Type::GameObject);
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kGuidArgs[] = {
    Game::Doc::Req("guid", "WOWGUID"),
};
const Game::Doc::Field kIsPlayerRets[] = {
    Game::Doc::Req("isPlayer", "bool"),
};
const Game::Doc::Field kIsCreatureRets[] = {
    Game::Doc::Req("isCreature", "bool"),
};
const Game::Doc::Field kIsPetRets[] = {
    Game::Doc::Req("isPet", "bool"),
};
const Game::Doc::Field kIsGameObjectRets[] = {
    Game::Doc::Req("isGameObject", "bool"),
};
const Game::Doc::Function kGUIDIsPlayer{
    "Returns whether a GUID belongs to a player.", kGuidArgs, kIsPlayerRets};
const Game::Doc::Function kGUIDIsCreature{
    "Returns whether a GUID belongs to a creature; pets do not count.", kGuidArgs,
    kIsCreatureRets, nullptr, true};
const Game::Doc::Function kGUIDIsPet{
    "Returns whether a GUID belongs to a pet.", kGuidArgs, kIsPetRets, nullptr, true};
const Game::Doc::Function kGUIDIsGameObject{
    "Returns whether a GUID belongs to a game object, such as a herb, chest or door.",
    kGuidArgs, kIsGameObjectRets, nullptr, true};

void Register() {
    Game::Lua::RegisterTableFunction("C_PlayerInfo", "GUIDIsPlayer",
                                     &Script_GUIDIsPlayer, &kGUIDIsPlayer);
    Game::Lua::RegisterTableFunction("C_PlayerInfo", "GUIDIsCreature",
                                     &Script_GUIDIsCreature, &kGUIDIsCreature);
    Game::Lua::RegisterTableFunction("C_PlayerInfo", "GUIDIsPet",
                                     &Script_GUIDIsPet, &kGUIDIsPet);
    Game::Lua::RegisterTableFunction("C_PlayerInfo", "GUIDIsGameObject",
                                     &Script_GUIDIsGameObject, &kGUIDIsGameObject);
}

} // namespace

static const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace Guid::PlayerInfoBindings
