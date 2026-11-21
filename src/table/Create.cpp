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
#include "lua/Arg.h"

namespace Table::Create {

// `table.create(arraySizeHint [, nodeSizeHint])` as the 3.80.2 client has it
// (0x140D32B50, wrapper 0x140D32A50; registered with table.count, freeze,
// isfrozen and C_TableUtil.FindIndexedMismatch by 0x140D32F40 in both the game
// and glue states). Both hints are int32 arguments (0x140A91460); the node
// hint defaults to 0. Each is clamped to 0..0x100000 (a negative one is 0),
// then lua_createtable. The new table is the one return.
static constexpr int kMaxHint = 0x100000;
static constexpr const char *kUsage = "Usage: local table = table.create(arraySizeHint [, nodeSizeHint])";

static int Clamp(int32_t hint) {
    return hint < 0 ? 0 : (hint > kMaxHint ? kMaxHint : hint);
}

static int __fastcall Script_table_create(void *L) {
    int32_t arraySize = 0;
    int32_t nodeSize = 0;
    Game::Lua::Arg::Int32(L, 1, kUsage, arraySize);
    Game::Lua::Arg::Int32(L, 2, kUsage, nodeSize, true);
    Game::Lua::CreateTable(L, Clamp(arraySize), Clamp(nodeSize));
    return 1;
}

static const Game::Doc::Field kCreateArgs[] = {
    Game::Doc::Req("arraySizeHint", "number"),
    Game::Doc::Opt("nodeSizeHint", "number", "0"),
};
static const Game::Doc::Field kCreateRets[] = {
    Game::Doc::Req("table", "table"),
};
static const Game::Doc::Function kCreate{
    "Returns a new empty table with room reserved for array entries and keyed entries.", kCreateArgs,
    kCreateRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("table", "create", &Script_table_create, &kCreate);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
static const Game::GlueModuleAutoRegister _glueAutoreg{&RegisterLuaFunctions};

} // namespace Table::Create
