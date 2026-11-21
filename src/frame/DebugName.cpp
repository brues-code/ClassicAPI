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

// `self:GetDebugName([preferParentKey])` - the name the debug tools print
// for a script object, as the 3.80.2 client builds it (FUN_140592e00, behind
// the method body FUN_140a96ff0):
//   - a named object gives its global name, unless preferParentKey is set and
//     it has a parent;
//   - otherwise the parent's debug name, a dot, and the key the parent's Lua
//     table holds the object under ("CharacterFrame.TitleBg"). The key is
//     found by a search of that table (FUN_140592bb0, Frame::ParentKey::Find),
//     not a stored field;
//   - with no key, the address of the object's Lua table in lowercase hex
//     ("SomeFrame.1a2b3c4d"); a named object with no key (reachable only
//     with preferParentKey) gives "Parent, Name" instead;
//   - an object with no parent gives its address; a parent whose debug name
//     is "" gives "".
// preferParentKey is the same at every level. 3.80.2 registers the method on
// every script object with a parent; here that is regions and our own
// script objects, each with its own parent (Animation::ParentOf).

#include "frame/DebugName.h"

#include "Game.h"
#include "Offsets.h"
#include "animation/Object.h"
#include "frame/ParentKey.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace Frame::DebugName {

namespace {

// The address of the object's Lua table, as 3.80.2 prints it ("%llx" of
// lua_topointer; "%x" is the same text for a 32-bit pointer).
std::string Address(void *L, void *object) {
    Game::Lua::PushObject(L, object);
    const void *table = Game::Lua::ToPointer(L, -1);
    Game::Lua::SetTop(L, -2);
    char text[16];
    std::snprintf(text, sizeof(text), "%x",
                  static_cast<unsigned>(reinterpret_cast<uintptr_t>(table)));
    return text;
}

std::string Build(void *L, void *object, bool preferParentKey) {
    const char *name = Game::Lua::ObjectName(object);
    void *parent = Animation::ParentOf(object);
    if (name != nullptr && (!preferParentKey || parent == nullptr))
        return name;

    std::string key;
    const bool hasKey = ParentKey::Find(L, object, key);
    if (!hasKey)
        key = Address(L, object);
    if (parent == nullptr)
        return key;

    std::string parentName = Build(L, parent, preferParentKey);
    if (parentName.empty())
        return {};
    if (!hasKey && name != nullptr)
        return parentName + ", " + name;
    return parentName + "." + key;
}

} // namespace

int __fastcall Script_GetDebugName(void *L) {
    void *self = ParentKey::ResolveSelf(L);
    if (self == nullptr)
        return 0;
    const bool preferParentKey = Game::Lua::ToBoolean(L, 2) != 0;
    Game::Lua::PushString(L, Build(L, self, preferParentKey).c_str());
    return 1;
}

namespace {

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"GetDebugName", &Script_GetDebugName},
};

const Game::Doc::Field kArgs[] = {
    Game::Doc::Opt("preferParentKey", "bool", "false",
                   "Uses the parent-key path even when the object has a global name."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("debugName", "string"),
};
const Game::Doc::Function kGetDebugName{
    "Returns the object's global name, or its parent's debug name and the key or address "
    "the parent holds it under; the address alone for an unnamed object with no parent.",
    kArgs, kRets};
const Game::Doc::Method g_methodDocs[] = {
    {"GetDebugName", &kGetDebugName},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_REGION_METHOD_REGISTRY), g_methods, g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::DebugName
