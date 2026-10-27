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

// `region:GetDebugName([preferParentKey])` — the name the debug tools print
// for a region. Retail's rules, as its documented examples show them:
//   - a named region gives its global name;
//   - an unnamed region gives its parent's debug name, a dot, and the key the
//     parent's table holds it under ("CharacterFrame.TitleBg"), or its address
//     in hex when the parent holds it under no key ("SomeFrame.156f1390860");
//   - an unnamed region with no parent gives "".
// The key comes from the parent's Lua table, not from a recorded parentKey:
// retail resolves a plain `CharacterFrame.b = frame` once b is parented.
// `preferParentKey` puts the parent-key path ahead of a global name, all the
// way up the chain.

#include "Game.h"
#include "Offsets.h"

#include <cstdio>
#include <string>

namespace Frame::DebugName {

namespace {

// The string key under which the table at `parentIdx` holds the table at
// `childIdx`, or "" when it holds it under none. Both are object tables, so
// identity is pointer equality.
std::string FindKey(void *L, int parentIdx, int childIdx) {
    const void *child = Game::Lua::ToPointer(L, childIdx);
    Game::Lua::PushNil(L);
    while (Game::Lua::Next(L, parentIdx) != 0) {
        if (Game::Lua::Type(L, -2) == Game::Lua::TYPE_STRING &&
            Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE &&
            Game::Lua::ToPointer(L, -1) == child) {
            std::string key = Game::Lua::ToString(L, -2);
            Game::Lua::SetTop(L, -3); // value and key
            return key;
        }
        Game::Lua::SetTop(L, -2); // value; keep the key for the next step
    }
    return {};
}

std::string Build(void *L, void *object, bool preferParentKey) {
    const char *name = Game::Lua::ObjectName(object);
    const bool named = name != nullptr && *name != '\0';
    if (named && !preferParentKey)
        return name;

    void *parent = Game::Read<void *>(object, Offsets::OFF_REGION_PARENT);
    if (parent == nullptr)
        return named ? name : "";
    std::string parentName = Build(L, parent, preferParentKey);
    if (parentName.empty())
        return named ? name : "";

    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushObject(L, parent);
    Game::Lua::PushObject(L, object);
    std::string key = FindKey(L, top + 1, top + 2);
    Game::Lua::SetTop(L, top);

    if (!key.empty())
        return parentName + "." + key;
    if (named)
        return name;
    char address[16];
    std::snprintf(address, sizeof(address), "%x",
                  static_cast<unsigned>(reinterpret_cast<uintptr_t>(object)));
    return parentName + "." + address;
}

int __fastcall Script_GetDebugName(void *L) {
    void *self = Game::Lua::ResolveRegion(L);
    if (self == nullptr)
        return 0;
    const bool preferParentKey = Game::Lua::ToBoolean(L, 2) != 0;
    Game::Lua::PushString(L, Build(L, self, preferParentKey).c_str());
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"GetDebugName", &Script_GetDebugName},
};

const Game::Doc::Field kArgs[] = {
    Game::Doc::Opt("preferParentKey", "bool", "false",
                   "Uses the parent-key path even when the region has a global name."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("debugName", "string"),
};
const Game::Doc::Function kGetDebugName{
    "Returns the region's global name, or its parent's debug name and the key or address "
    "the parent holds it under; empty for an unnamed region with no parent.",
    kArgs, kRets};
const Game::Doc::Method g_methodDocs[] = {
    {"GetDebugName", &kGetDebugName},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_REGION_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::DebugName
