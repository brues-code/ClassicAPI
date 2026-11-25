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

// `IsAtStableMaster()` — true while the stable window is open.
//
// `C_StableInfo.GetStablePetInfo(index)` — retail's PetInfo table (the 3.80.2
// client has only the global). The engine's own GetStablePetInfo reads the
// pet, with its index rule (0 or less the summoned pet, 1..N a stabled one);
// its five values fill the table, and the fields 1.12 keeps no data for get
// retail's defaults.
//
// 4.3.4's Script_IsAtStableMaster pushes whether CGStableInfo::m_stableMaster
// is set. 1.12 keeps the same GUID at VAR_STABLE_MASTER_GUID, with the same
// lifetime: set by the list message, cleared by the close.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Pet::Stable {

namespace {

int __fastcall Script_IsAtStableMaster(void *L) {
    Game::Lua::PushBool(L, Game::Read<uint64_t>(Offsets::VAR_STABLE_MASTER_GUID) != 0);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kRets[] = {
    Game::Doc::Req("isAtStableMaster", "bool", "True while the stable window is open."),
};
const Game::Doc::Function kIsAtStableMaster{
    "Returns true while the stable window is open.",
    {}, kRets, "PetGlobals"};

int __fastcall Script_GetStablePetInfo(void *L) {
    const int top = Game::Lua::GetTop(L);
    reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_GET_STABLE_PET_INFO)(L);
    // An empty slot, or a creature not cached yet: nil icon.
    if (Game::Lua::GetTop(L) < top + 5 || Game::Lua::Type(L, top + 1) <= Game::Lua::TYPE_NIL) {
        Game::Lua::SetTop(L, top);
        return 0;
    }
    Game::Lua::NewTable(L);
    const char *const keys[] = {"icon", "name", "level", "familyName", "loyaltyName"};
    for (int i = 0; i < 5; ++i) {
        Game::Lua::PushString(L, keys[i]);
        Game::Lua::PushValue(L, top + 1 + i);
        Game::Lua::SetTable(L, -3);
    }
    Game::Lua::SetFieldNumber(L, "slotID", Game::Lua::ToNumber(L, 1));
    Game::Lua::SetFieldString(L, "specialization", "");
    Game::Lua::SetFieldString(L, "type", "");
    Game::Lua::PushString(L, "petAbilities");
    Game::Lua::NewTable(L);
    Game::Lua::SetTable(L, -3);
    Game::Lua::PushString(L, "specAbilities");
    Game::Lua::NewTable(L);
    Game::Lua::SetTable(L, -3);
    Game::Lua::SetFieldNumber(L, "displayID", 0);
    Game::Lua::SetFieldBool(L, "isFavorite", false);
    Game::Lua::SetFieldBool(L, "isExotic", false);
    Game::Lua::SetFieldNumber(L, "uiModelSceneID", 718);
    Game::Lua::SetFieldNumber(L, "petNumber", 0);
    Game::Lua::SetFieldNumber(L, "creatureID", 0);
    Game::Lua::SetFieldNumber(L, "specID", 0);
    Game::Lua::SetFieldNumber(L, "loyaltyLevel", 0);
    Game::Lua::SetFieldNumber(L, "happinessLevel", 0);
    Game::Lua::SetFieldNumber(L, "experience", 0);
    Game::Lua::SetFieldNumber(L, "experienceNeeded", 0);
    Game::Lua::Replace(L, top + 1);
    Game::Lua::SetTop(L, top + 1);
    return 1;
}

const Game::Doc::Field kPetInfoArgs[] = {
    Game::Doc::Req("index", "luaIndex", "0 or less for the summoned pet, 1 and up for a stabled one."),
};
const Game::Doc::Field kPetInfoRets[] = {
    Game::Doc::Opt("petInfo", "PetInfo", nullptr,
                   "icon, name, level, familyName and loyaltyName, with retail's defaults for the rest; "
                   "nil for an empty slot or a pet whose creature is not cached yet."),
};
const Game::Doc::Function kGetStablePetInfo{"Returns a stabled or the summoned pet.", kPetInfoArgs,
                                            kPetInfoRets};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_StableInfo", "GetStablePetInfo", &Script_GetStablePetInfo,
                                     &kGetStablePetInfo);
    Game::Lua::RegisterGlobalFunction("IsAtStableMaster", &Script_IsAtStableMaster,
                                      &kIsAtStableMaster);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Pet::Stable
