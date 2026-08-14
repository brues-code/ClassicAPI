// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

// Vanilla 1.12 has no RunMacro or RunMacroText global. Raw macro text is
// dispatched one line at a time through FrameXML's ChatEdit_ParseText using a
// small throwaway edit-box table. Saved macros are resolved with the stock
// GetMacroIndexByName/GetMacroInfo globals before entering that same path.

#include "macro/Execute.h"

#include "Game.h"

#include <cstddef>
#include <string>

namespace Macro::Execute {

namespace {

int __fastcall Script_GetText(void *L) {
    Game::Lua::PushValue(L, Game::Lua::UpvalueIndex(1));
    return 1;
}

int __fastcall Script_Noop(void *) { return 0; }

int __fastcall Script_Index(void *L) {
    Game::Lua::PushCClosure(L, &Script_Noop, 0);
    return 1;
}

void Line(void *L, const char *line, size_t length) {
    if (length == 0)
        return;
    if (line[length - 1] == '\r')
        --length;
    if (length == 0)
        return;

    const int top = Game::Lua::GetTop(L);

    Game::Lua::NewTable(L);
    const int objectIndex = Game::Lua::GetTop(L);

    Game::Lua::PushString(L, "GetText");
    Game::Lua::PushLString(L, line, static_cast<unsigned int>(length));
    Game::Lua::PushCClosure(L, &Script_GetText, 1);
    Game::Lua::SetTable(L, objectIndex);

    Game::Lua::NewTable(L);
    const int metatableIndex = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, "__index");
    Game::Lua::PushCClosure(L, &Script_Index, 0);
    Game::Lua::SetTable(L, metatableIndex);

    if (Game::Lua::PushGlobalFunction(L, "setmetatable")) {
        Game::Lua::PushValue(L, objectIndex);
        Game::Lua::PushValue(L, metatableIndex);
        Game::Lua::PCall(L, 2, 0, 0);
    }

    if (Game::Lua::PushGlobalFunction(L, "ChatEdit_ParseText")) {
        Game::Lua::PushValue(L, objectIndex);
        Game::Lua::PushNumber(L, 1);
        Game::Lua::PCall(L, 2, 0, 0);
    }

    Game::Lua::SetTop(L, top);
}

bool ParseIndex(const char *value, int *index) {
    if (value == nullptr || *value == '\0')
        return false;

    int result = 0;
    for (const char *p = value; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9')
            return false;

        const int digit = *p - '0';
        if (result > (0x7FFFFFFF - digit) / 10)
            return false;

        result = result * 10 + digit;
    }

    if (result <= 0)
        return false;

    *index = result;
    return true;
}

bool CallRunMacro(void *L, const char *macro) {
    const int top = Game::Lua::GetTop(L);
    if (!Game::Lua::PushGlobalFunction(L, "RunMacro")) {
        Game::Lua::SetTop(L, top);
        return false;
    }

    int index = 0;
    if (ParseIndex(macro, &index))
        Game::Lua::PushNumber(L, index);
    else
        Game::Lua::PushString(L, macro);

    const bool ok = Game::Lua::PCall(L, 1, 0, 0) == 0;
    Game::Lua::SetTop(L, top);

    return ok;
}

int ResolveIndex(void *L, const char *macro) {
    int index = 0;
    if (ParseIndex(macro, &index))
        return index;

    const int top = Game::Lua::GetTop(L);
    if (!Game::Lua::PushGlobalFunction(L, "GetMacroIndexByName")) {
        Game::Lua::SetTop(L, top);
        return 0;
    }

    Game::Lua::PushString(L, macro);
    if (Game::Lua::PCall(L, 1, 1, 0) == 0 && Game::Lua::IsNumber(L, -1))
        index = static_cast<int>(Game::Lua::ToNumber(L, -1));

    Game::Lua::SetTop(L, top);

    return index;
}

bool GetBody(void *L, int index, std::string *body) {
    const int top = Game::Lua::GetTop(L);
    if (!Game::Lua::PushGlobalFunction(L, "GetMacroInfo")) {
        Game::Lua::SetTop(L, top);
        return false;
    }

    Game::Lua::PushNumber(L, index);
    const bool called = Game::Lua::PCall(L, 1, 3, 0) == 0;
    const bool found = called && Game::Lua::IsString(L, -1);
    if (found)
        *body = Game::Lua::ToString(L, -1);

    Game::Lua::SetTop(L, top);

    return found;
}

} // namespace

void Text(void *L, const char *text) {
    if (L == nullptr || text == nullptr)
        return;

    for (const char *line = text; *line != '\0';) {
        const char *end = line;
        while (*end != '\0' && *end != '\n')
            ++end;
        Line(L, line, static_cast<size_t>(end - line));
        line = *end == '\n' ? end + 1 : end;
    }
}

bool Saved(void *L, const char *macro) {
    if (L == nullptr || macro == nullptr || *macro == '\0')
        return false;

    if (CallRunMacro(L, macro))
        return true;

    const int index = ResolveIndex(L, macro);
    if (index <= 0)
        return false;

    std::string body;
    if (!GetBody(L, index, &body))
        return false;

    Text(L, body.c_str());
    return true;
}

} // namespace Macro::Execute
