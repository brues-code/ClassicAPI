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

namespace Event::Util {

static int __fastcall Script_IsEventValid(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_STRING) {
        Game::Lua::Error(L, "Usage: C_EventUtils.IsEventValid(eventName)");
        return 0;
    }
    const char *needle = Game::Lua::ToString(L, 1);
    if (needle == nullptr || needle[0] == 0) {
        Game::Lua::PushBool(L, 0);
        return 1;
    }
    Game::Lua::PushBoolean(L, Event::Custom::LookupByName(needle) >= 0);
    return 1;
}

static const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("eventName", "string"),
};
static const Game::Doc::Field kRets[] = {
    Game::Doc::Req("valid", "bool", "True when frame:RegisterEvent accepts the name."),
};
static const Game::Doc::Function kIsEventValid{"Returns whether an event by that name exists.",
                                               kArgs, kRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_EventUtils", "IsEventValid", &Script_IsEventValid,
                                     &kIsEventValid);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Event::Util
