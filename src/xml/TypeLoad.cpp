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

#include "xml/TypeLoad.h"

#include "Game.h"
#include "Offsets.h"
#include "xml/Mixin.h"

#include <cstdint>

namespace Xml::TypeLoad {

namespace {

using Loader_t = uintptr_t(__fastcall *)(void *self, void *edx, const void *node, void *status);

struct Site {
    uintptr_t address;
    bool region; // a texture or font string: the load runs in its mixin scope
};

constexpr Site kSites[LOADER_COUNT] = {
    {Offsets::FUN_XML_TEXTURE_LOAD, true},  {Offsets::FUN_XML_FONTSTRING_LOAD, true},
    {Offsets::FUN_XML_BUTTON_LOAD, false},  {Offsets::FUN_XML_EDITBOX_LOAD, false},
    {Offsets::FUN_XML_STATUSBAR_LOAD, false},
};

Loader_t g_orig[LOADER_COUNT] = {};
AutoSubscribe *g_head[LOADER_COUNT] = {};

template <int I> uintptr_t __fastcall Hook(void *self, void *edx, const void *node, void *status) {
    if (self == nullptr || node == nullptr)
        return g_orig[I](self, edx, node, status);
    void *object = static_cast<uint8_t *>(self) - Offsets::OFF_XML_LOADER_SUBOBJECT;
    if (kSites[I].region)
        Xml::Mixin::Begin(object, node, status, false);
    const uintptr_t r = g_orig[I](self, edx, node, status);
    if (kSites[I].region)
        Xml::Mixin::End(object);
    for (AutoSubscribe *s = g_head[I]; s != nullptr; s = s->next)
        s->fn(object, node, status);
    return r;
}

#define TYPE_LOAD_HOOK(I) {kSites[I].address, reinterpret_cast<void *>(&Hook<I>), reinterpret_cast<void **>(&g_orig[I])}

// Each element registers its own address, so each is built in place.
const Game::HookAutoRegister _hooks[LOADER_COUNT] = {
    TYPE_LOAD_HOOK(TEXTURE), TYPE_LOAD_HOOK(FONT_STRING), TYPE_LOAD_HOOK(BUTTON),
    TYPE_LOAD_HOOK(EDIT_BOX), TYPE_LOAD_HOOK(STATUS_BAR),
};

#undef TYPE_LOAD_HOOK

} // namespace

AutoSubscribe::AutoSubscribe(Loader loader, Observer f) : fn(f), next(g_head[loader]) { g_head[loader] = this; }

} // namespace Xml::TypeLoad
