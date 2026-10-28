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

// `ExpandAllFactionHeaders()` / `CollapseAllFactionHeaders()` — 3.3.5 and
// 4.3.4 call CGReputationInfo::CollapseHeader(m_numFactions, collapse): an
// index equal to the entry count sets every header. 1.12's CollapseHeader
// (FUN_REPUTATION_COLLAPSE_HEADER) has the same out-of-range branch, so this
// is the same call with 1.12's count.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Faction::Headers {

namespace {

void CollapseAll(int collapse) {
    using CollapseHeader_t = void(__fastcall *)(uint32_t entry, int collapse);
    reinterpret_cast<CollapseHeader_t>(static_cast<uintptr_t>(Offsets::FUN_REPUTATION_COLLAPSE_HEADER))(
        Game::Read<uint32_t>(Offsets::VAR_FACTION_VISIBLE_MAX_INDEX), collapse);
}

int __fastcall Script_ExpandAllFactionHeaders(void *) {
    CollapseAll(0);
    return 0;
}

int __fastcall Script_CollapseAllFactionHeaders(void *) {
    CollapseAll(1);
    return 0;
}

const Game::Doc::Function kExpandAll{"Expands every header of the reputation list.", {}, {},
                                     "ReputationGlobals"};
const Game::Doc::Function kCollapseAll{"Collapses every header of the reputation list.", {}, {},
                                       "ReputationGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("ExpandAllFactionHeaders", &Script_ExpandAllFactionHeaders,
                                      &kExpandAll);
    Game::Lua::RegisterGlobalFunction("CollapseAllFactionHeaders", &Script_CollapseAllFactionHeaders,
                                      &kCollapseAll);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Faction::Headers
