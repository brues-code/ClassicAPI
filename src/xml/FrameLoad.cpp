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

#include "storm/HashTable.h"
#include "storm/String.h"
#include "xml/FrameLoad.h"

#include "Game.h"
#include "xml/Inherits.h"
#include "xml/Intrinsics.h"
#include "Offsets.h"

#include <cstdint>

namespace Xml {

namespace {

using GetAttr_t = const char *(__thiscall *)(const void *node, const char *name);
using TemplateLookup_t = const void *(__fastcall *)(const char *name);
using StatusAdd_t = void(__cdecl *)(void *status, int level, const char *fmt, ...);

void Report(void *status, int level, const char *fmt, const char *a, const char *b, const char *c) {
    if (status == nullptr)
        return;
    const void *vtable = Game::Read<const void *>(status, 0);
    Game::Read<StatusAdd_t>(vtable, Offsets::VTBL_XML_STATUS_ADD)(status, level, fmt, a, b, c);
}

} // namespace

const void *FirstChild(const void *node) {
    return Game::Read<const void *>(node, Offsets::OFF_XML_NODE_CHILD);
}

const void *NextSibling(const void *node) {
    return Game::Read<const void *>(node, Offsets::OFF_XML_NODE_SIBLING);
}

const char *Tag(const void *node) {
    return Game::Read<const char *>(node, Offsets::OFF_XML_NODE_TAG);
}

bool TagIs(const void *node, const char *tag) {
    const char *t = Tag(node);
    return t != nullptr &&
           Game::StrCmpI(t, tag) == 0;
}

const char *Attr(const void *node, const char *name) {
    return reinterpret_cast<GetAttr_t>(Offsets::FUN_XML_NODE_GET_ATTRIBUTE)(node, name);
}

const void *Template(const char *name) {
    return reinterpret_cast<TemplateLookup_t>(Offsets::FUN_XML_TEMPLATE_LOOKUP)(name);
}

// FUN_XML_TEMPLATE_LOOKUP's own walk — hash, masked bucket, chain compare on
// hash then SStrCmpI — returning the node's context instead of its definition.
const char *TemplateContext(const char *name) {
    const uint8_t *node = Storm::HashTable::FindStri(Offsets::VAR_XML_TEMPLATE_TABLE, name);
    return node != nullptr ? Game::Read<const char *>(node, Offsets::OFF_XML_TEMPLATE_NODE_CONTEXT) : nullptr;
}

void LoadNode(void *object, const void *node, void *status) {
    using NodeLoader_t = uintptr_t(__thiscall *)(void *self, const void *node, void *status);
    void *self = static_cast<uint8_t *>(object) + Offsets::OFF_XML_LOADER_SUBOBJECT;
    const void *vtable = Game::Read<const void *>(self, 0);
    Game::Read<NodeLoader_t>(vtable, Offsets::VTBL_XML_NODE_LOADER)(self, node, status);
}

bool FloatAttr(const void *node, const char *name, float &out) {
    const char *value = Attr(node, name);
    if (value == nullptr || *value == '\0')
        return false;
    out = Storm::ToFloat(value);
    return true;
}

void ReadColor(const void *node, uint32_t &bgra, void *status) {
    using ReadColor_t = int(__fastcall *)(const void *node, uint32_t *bgra, void *status);
    reinterpret_cast<ReadColor_t>(Offsets::FUN_XML_READ_COLOR)(node, &bgra, status);
}

void Warn(void *status, const char *fmt, const char *a, const char *b, const char *c) {
    Report(status, Offsets::XML_STATUS_WARNING, fmt, a, b, c);
}

void Error(void *status, const char *fmt, const char *a, const char *b, const char *c) {
    Report(status, Offsets::XML_STATUS_ERROR, fmt, a, b, c);
}

bool PushWrapper(void *L, void *object) {
    if (Game::Read<int>(object, Offsets::OFF_COBJECT_LUA_REFCOUNT) == 0 &&
        Game::Read<const char *>(object, Offsets::OFF_SCRIPT_REGION_NAME) != nullptr)
        return false;
    const int ref = Game::Lua::ObjectLuaRef(object);
    if (ref <= 0)
        return false;
    Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, ref); // [wrapper]
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::SetTop(L, -2);
        return false;
    }
    Game::Lua::RawGetI(L, -1, 0); // [wrapper, wrapper[0]]
    const bool same = Game::Lua::Type(L, -1) == Game::Lua::TYPE_LIGHTUSERDATA &&
                      Game::Lua::ToPointer(L, -1) == object;
    Game::Lua::SetTop(L, -2); // [wrapper]
    if (!same) {
        Game::Lua::SetTop(L, -2);
        return false;
    }
    return true;
}

void AppendToArray(void *L, int parentIdx, const char *key) {
    const int value = Game::Lua::GetTop(L);        // [.., value]
    Game::Lua::PushString(L, key);
    Game::Lua::RawGet(L, parentIdx);               // [.., value, array?]
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::SetTop(L, value);
        Game::Lua::NewTable(L);                    // [.., value, array]
        Game::Lua::PushString(L, key);
        Game::Lua::PushValue(L, -2);
        Game::Lua::RawSet(L, parentIdx);           // parent[key] = array
    }
    int n = 0;
    for (;;) {
        Game::Lua::RawGetI(L, value + 1, n + 1);
        const bool present = Game::Lua::Type(L, -1) != Game::Lua::TYPE_NIL;
        Game::Lua::SetTop(L, value + 1);
        if (!present)
            break;
        ++n;
    }
    Game::Lua::PushNumber(L, n + 1);
    Game::Lua::PushValue(L, value);                // [.., value, array, n+1, value]
    Game::Lua::RawSet(L, value + 1);               // array[n+1] = value
    Game::Lua::SetTop(L, value - 1);
}

namespace FrameLoad {

namespace {

// Chained at static-init time by each `AutoSubscribe` constructor, before
// `DllMain`; zero-initialized ahead of any dynamic init.

using Loader_t = uintptr_t(__fastcall *)(void *self, void *edx, const void *node, void *status);
Loader_t g_orig = nullptr;

// `__thiscall` modelled as `__fastcall` with a dummy edx.
uintptr_t __fastcall Hook(void *self, void *edx, const void *node, void *status) {
    if (self == nullptr || node == nullptr)
        return Inherits::LoadFrame(g_orig, self, edx, node, status);
    void *object = static_cast<uint8_t *>(self) - Offsets::OFF_XML_LOADER_SUBOBJECT;
    for (AutoSubscribeBefore *s = AutoSubscribeBefore::head; s != nullptr; s = s->next)
        s->fn(object, node, status);
    const Intrinsics::LoadScope intrinsic(object, node, status);
    const uintptr_t r = Inherits::LoadFrame(g_orig, self, edx, node, status);
    for (AutoSubscribe *s = AutoSubscribe::head; s != nullptr; s = s->next)
        s->fn(object, node, status);
    return r;
}

const Game::HookAutoRegister _hook{
    Offsets::FUN_XML_FRAME_LOAD, reinterpret_cast<void *>(&Hook),
    reinterpret_cast<void **>(&g_orig)};

} // namespace

} // namespace FrameLoad

} // namespace Xml
