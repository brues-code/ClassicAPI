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

#include "xml/FrameLoad.h"

#include "Game.h"
#include "xml/Inherits.h"
#include "Offsets.h"

#include <cstdint>

namespace Xml {

namespace {

using GetAttr_t = const char *(__thiscall *)(const void *node, const char *name);
using TemplateLookup_t = const void *(__fastcall *)(const char *name);
using SStrCmpI_t = int(__stdcall *)(const char *a, const char *b, int n);
using StatusAdd_t = void(__cdecl *)(void *status, int level, const char *fmt, ...);

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
           reinterpret_cast<SStrCmpI_t>(Offsets::FUN_SSTR_CMP_I)(t, tag, 0x7FFFFFFF) == 0;
}

const char *Attr(const void *node, const char *name) {
    return reinterpret_cast<GetAttr_t>(Offsets::FUN_XML_NODE_GET_ATTRIBUTE)(node, name);
}

const void *Template(const char *name) {
    return reinterpret_cast<TemplateLookup_t>(Offsets::FUN_XML_TEMPLATE_LOOKUP)(name);
}

void LoadNode(void *object, const void *node, void *status) {
    using NodeLoader_t = uintptr_t(__thiscall *)(void *self, const void *node, void *status);
    void *self = static_cast<uint8_t *>(object) + Offsets::OFF_XML_LOADER_SUBOBJECT;
    const void *vtable = Game::Read<const void *>(self, 0);
    Game::Read<NodeLoader_t>(vtable, Offsets::VTBL_XML_NODE_LOADER)(self, node, status);
}

void Warn(void *status, const char *fmt, const char *a, const char *b) {
    if (status == nullptr)
        return;
    const void *vtable = Game::Read<const void *>(status, 0);
    Game::Read<StatusAdd_t>(vtable, Offsets::VTBL_XML_STATUS_ADD)(
        status, Offsets::XML_STATUS_WARNING, fmt, a, b);
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

namespace FrameLoad {

namespace {

// Chained at static-init time by each `AutoSubscribe` constructor, before
// `DllMain`; zero-initialized ahead of any dynamic init.
AutoSubscribe *g_head = nullptr;
AutoSubscribeBefore *g_beforeHead = nullptr;

using Loader_t = uintptr_t(__fastcall *)(void *self, void *edx, const void *node, void *status);
Loader_t g_orig = nullptr;

// `__thiscall` modelled as `__fastcall` with a dummy edx.
uintptr_t __fastcall Hook(void *self, void *edx, const void *node, void *status) {
    if (self == nullptr || node == nullptr)
        return Inherits::LoadFrame(g_orig, self, edx, node, status);
    void *object = static_cast<uint8_t *>(self) - Offsets::OFF_XML_LOADER_SUBOBJECT;
    for (AutoSubscribeBefore *s = g_beforeHead; s != nullptr; s = s->next)
        s->fn(object, node, status);
    const uintptr_t r = Inherits::LoadFrame(g_orig, self, edx, node, status);
    for (AutoSubscribe *s = g_head; s != nullptr; s = s->next)
        s->fn(object, node, status);
    return r;
}

const Game::HookAutoRegister _hook{
    Offsets::FUN_XML_FRAME_LOAD, reinterpret_cast<void *>(&Hook),
    reinterpret_cast<void **>(&g_orig)};

} // namespace

AutoSubscribe::AutoSubscribe(Observer f) : fn(f), next(g_head) { g_head = this; }

AutoSubscribeBefore::AutoSubscribeBefore(Observer f) : fn(f), next(g_beforeHead) {
    g_beforeHead = this;
}

} // namespace FrameLoad

} // namespace Xml
