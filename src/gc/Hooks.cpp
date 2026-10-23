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

#include "gc/Hooks.h"

#include "Game.h"
#include "Offsets.h"

#include <cstddef>
#include <cstdint>

namespace Gc::Hooks {

namespace {

// Chained at static-init time by each `AutoSubscribe` constructor, before
// `DllMain`; zero-initialized ahead of any dynamic init.
AutoSubscribe *g_head = nullptr;

void NotifyCreate(const void *object) {
    for (AutoSubscribe *s = g_head; s != nullptr; s = s->next)
        if (s->onCreate != nullptr)
            s->onCreate(object);
}

using Link_t = void(__fastcall *)(void *L, void *object, uint8_t tt);
Link_t g_link = nullptr;

void __fastcall Link_h(void *L, void *object, uint8_t tt) {
    g_link(L, object, tt);
    NotifyCreate(object);
}

using NewLStr_t = void *(__fastcall *)(void *L, const char *str, size_t len, uint32_t hash);
NewLStr_t g_newLStr = nullptr;

void *__fastcall NewLStr_h(void *L, const char *str, size_t len, uint32_t hash) {
    void *s = g_newLStr(L, str, len, hash);
    NotifyCreate(s);
    return s;
}

using NewUdata_t = void *(__fastcall *)(void *L, size_t size);
NewUdata_t g_newUdata = nullptr;

void *__fastcall NewUdata_h(void *L, size_t size) {
    void *u = g_newUdata(L, size);
    NotifyCreate(u);
    return u;
}

using FreeObj_t = void(__fastcall *)(void *L, void *object);
FreeObj_t g_freeObj = nullptr;

void __fastcall FreeObj_h(void *L, void *object) {
    for (AutoSubscribe *s = g_head; s != nullptr; s = s->next)
        if (s->onFree != nullptr)
            s->onFree(object);
    g_freeObj(L, object);
}

const Game::HookAutoRegister _linkHook{
    Offsets::FUN_LUA_C_LINK, reinterpret_cast<void *>(&Link_h),
    reinterpret_cast<void **>(&g_link)};
const Game::HookAutoRegister _newLStrHook{
    Offsets::FUN_LUA_S_NEWLSTR, reinterpret_cast<void *>(&NewLStr_h),
    reinterpret_cast<void **>(&g_newLStr)};
const Game::HookAutoRegister _newUdataHook{
    Offsets::FUN_LUA_S_NEWUDATA, reinterpret_cast<void *>(&NewUdata_h),
    reinterpret_cast<void **>(&g_newUdata)};
const Game::HookAutoRegister _freeObjHook{
    Offsets::FUN_LUA_C_FREEOBJ, reinterpret_cast<void *>(&FreeObj_h),
    reinterpret_cast<void **>(&g_freeObj)};

} // namespace

AutoSubscribe::AutoSubscribe(Callback create, Callback free)
    : onCreate(create), onFree(free), next(g_head) {
    g_head = this;
}

} // namespace Gc::Hooks
