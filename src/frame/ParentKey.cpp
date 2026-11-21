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

// The parent-key methods, as the 3.80.2 client does them:
//   - GetParentKey (0x140a972d0): the search's key, or nil (no key, no
//     parent). Always one value.
//   - SetParentKey (0x140a973e0 -> FUN_140592a10): parentKey is required, a
//     string or a number (made text); clearOtherKeys (optional, toboolean)
//     first removes every key, as ClearParentKey does. Then
//     `parent[parentKey] = self` with lua_settable, so a parent __newindex
//     runs. An empty key or no parent writes nothing, with no error. Old
//     keys stay without clearOtherKeys. No return values.
//   - ClearParentKey (0x140a96e20 -> FUN_140592af0): sets parent[key] = nil
//     for the search's key until the search finds none, so every string key
//     goes; number keys (parentArray entries) stay. No return values.
// SetParent (0x140a9e3f0) and the destroy paths touch no key: after a
// reparent the old parent still holds the object. The XML parentKey binders
// (Xml::ParentKey, Animation::Xml) write the same way, without a clear.

#include "frame/ParentKey.h"

#include "Game.h"
#include "Offsets.h"
#include "animation/Object.h"

#include <utility>

namespace Frame::ParentKey {

namespace {

bool StartsUpper(const std::string &key) { return !key.empty() && key[0] >= 'A' && key[0] <= 'Z'; }

// parent[key] = the value on top of the stack (popped), with lua_settable.
void SetInParent(void *L, void *parent, const std::string &key) {
    Game::Lua::PushObject(L, parent);   // [value, parent]
    Game::Lua::PushString(L, key.c_str()); // [value, parent, key]
    Game::Lua::PushValue(L, -3);        // [value, parent, key, value]
    Game::Lua::SetTable(L, -3);         // [value, parent]
    Game::Lua::SetTop(L, -3);
}

void ClearAll(void *L, void *object) {
    void *parent = Animation::ParentOf(object);
    if (parent == nullptr)
        return;
    std::string key;
    while (Find(L, object, key)) {
        Game::Lua::PushNil(L);
        SetInParent(L, parent, key);
    }
}

} // namespace

bool Find(void *L, void *object, std::string &key) {
    void *parent = Animation::ParentOf(object);
    if (parent == nullptr)
        return false;
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushObject(L, parent);
    Game::Lua::PushObject(L, object);
    const void *child = Game::Lua::ToPointer(L, top + 2);
    bool found = false;
    Game::Lua::PushNil(L);
    while (Game::Lua::Next(L, top + 1) != 0) {
        if (Game::Lua::Type(L, -2) == Game::Lua::TYPE_STRING &&
            Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE &&
            Game::Lua::ToPointer(L, -1) == child) {
            std::string candidate = Game::Lua::ToString(L, -2);
            if (!found || (StartsUpper(candidate) && !StartsUpper(key))) {
                key = std::move(candidate);
                found = true;
            }
        }
        Game::Lua::SetTop(L, -2); // value; keep the key for the next step
    }
    Game::Lua::SetTop(L, top);
    return found;
}

void *ResolveSelf(void *L) {
    void *self = Game::Lua::Type(L, 1) == Game::Lua::TYPE_TABLE ? Game::Lua::ResolveObject(L, 1)
                                                                 : nullptr;
    return Animation::IsOurs(self) ? self : Game::Lua::ResolveRegion(L);
}

int __fastcall Script_GetParentKey(void *L) {
    void *self = ResolveSelf(L);
    if (self == nullptr)
        return 0;
    std::string key;
    if (Find(L, self, key))
        Game::Lua::PushString(L, key.c_str());
    else
        Game::Lua::PushNil(L);
    return 1;
}

int __fastcall Script_SetParentKey(void *L) {
    void *self = ResolveSelf(L);
    if (self == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: self:SetParentKey(parentKey [, clearOtherKeys])");
        return 0;
    }
    const std::string key = Game::Lua::ToString(L, 2);
    if (Game::Lua::ToBoolean(L, 3) != 0)
        ClearAll(L, self);
    void *parent = Animation::ParentOf(self);
    if (key.empty() || parent == nullptr)
        return 0;
    Game::Lua::PushObject(L, self);
    SetInParent(L, parent, key);
    return 0;
}

int __fastcall Script_ClearParentKey(void *L) {
    void *self = ResolveSelf(L);
    if (self != nullptr)
        ClearAll(L, self);
    return 0;
}

namespace {

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"GetParentKey", &Script_GetParentKey},
    {"SetParentKey", &Script_SetParentKey},
    {"ClearParentKey", &Script_ClearParentKey},
};

const Game::Doc::Field kGetRets[] = {
    Game::Doc::Opt("parentKey", "string", nullptr,
                   "The key that holds the object in its parent's table; nil when none does."),
};
const Game::Doc::Field kSetArgs[] = {
    Game::Doc::Req("parentKey", "string"),
    Game::Doc::Opt("clearOtherKeys", "bool", "false",
                   "First removes every other key that holds the object in its parent's table."),
};
const Game::Doc::Function kGetParentKey{
    "Returns the key under which the object's parent holds it.", {}, kGetRets};
const Game::Doc::Function kSetParentKey{
    "Puts the object in its parent's table under parentKey.", kSetArgs, {}};
const Game::Doc::Function kClearParentKey{
    "Removes every key that holds the object in its parent's table.", {}, {}};
const Game::Doc::Method g_methodDocs[] = {
    {"GetParentKey", &kGetParentKey},
    {"SetParentKey", &kSetParentKey},
    {"ClearParentKey", &kClearParentKey},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_REGION_METHOD_REGISTRY), g_methods, g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::ParentKey
