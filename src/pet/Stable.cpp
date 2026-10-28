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
    Game::Doc::Req("isAtStableMaster", "boolean", "True while the stable window is open."),
};
const Game::Doc::Function kIsAtStableMaster{
    "Returns true while the stable window is open.",
    {}, kRets, "PetGlobals"};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsAtStableMaster", &Script_IsAtStableMaster,
                                      &kIsAtStableMaster);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Pet::Stable
