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

#include "Text.h"

#include "Data.h"
#include "Game.h"
#include "event/Custom.h"

namespace EquipmentSet::Text {

std::string Global(const char *name, const char *fallback) {
    void *L = Game::Lua::State();
    if (L == nullptr)
        return fallback;
    // Raw read: callers include the world tick, where no `__index` may run.
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, name);
    Game::Lua::RawGet(L, Game::Lua::GLOBALS_INDEX);
    std::string out = fallback;
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_STRING) {
        const char *s = Game::Lua::ToString(L, -1);
        if (s != nullptr)
            out = s;
    }
    Game::Lua::SetTop(L, top);
    return out;
}

std::string FormatS(const std::string &format, const std::string &arg) {
    const size_t at = format.find("%s");
    if (at == std::string::npos)
        return format;
    return format.substr(0, at) + arg + format.substr(at + 2);
}

void UIError(const char *globalName, const char *fallback, const char *setName) {
    const int evt = Event::Custom::LookupByName("UI_ERROR_MESSAGE");
    if (evt < 0)
        return;
    std::string text = Global(globalName, fallback);
    if (setName != nullptr)
        text = FormatS(text, setName);
    Event::Custom::Fire(evt, "%s", text.c_str());
}

std::string SetList(uint64_t itemGuid) {
    std::string list;
    const std::string delimiter = Global("PLAYER_LIST_DELIMITER", ", ");
    for (const Set *s : Data::SetsContainingItem(itemGuid)) {
        if (!list.empty())
            list += delimiter;
        list += s->name;
    }
    return list;
}

} // namespace EquipmentSet::Text
