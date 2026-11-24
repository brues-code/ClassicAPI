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

// When a texture or a font string becomes visible, and when it stops.
//
// 3.80.2 sets a region's visible bit when it is shown and its owner frame is
// visible, from the region's own Show and from the owner's show walk
// (FUN_140577070, FUN_140589B10), and fires OnShow there; hide is the same in
// reverse (docs/notes/region-mouse-3802.md).
//
// 1.12 keeps the same state as the region's registration in its owner's
// layer list: FUN_REGION_SHOW registers a region that is shown with its owner
// visible and sets OFF_REGION_ACTUALLY_SHOWN; FUN_REGION_HIDE unregisters it.
// Every change goes through them: the region's Show and Hide, the owner's
// show and hide walks, the XML `hidden` attribute and
// FUN_REGION_SET_PARENT_AND_LAYER. The last one unregisters and registers again
// when the layer or the owner changes (SetDrawLayer, SetParent), which leaves
// a region that stays visible visible; so a change inside it is reported once,
// at its end, and only when the region's state differs from its start.
//
// Listeners run after the engine's call, with the new state in place. A
// region being destroyed passes through FUN_REGION_SET_PARENT_AND_LAYER too;
// Frame::RegionDestruction listeners run before that, so a module that
// forgets the region there hears nothing of it here.

#include "frame/RegionVisibility.h"

#include "Game.h"
#include "Offsets.h"

namespace Frame::RegionVisibility {

namespace {

using Change_t = void(__fastcall *)(void *region);
using SetParentAndLayer_t = void(__fastcall *)(void *region, void *edx, void *parent, int layer, int show);

Change_t g_origShow = nullptr;
Change_t g_origHide = nullptr;
SetParentAndLayer_t g_origSetParentAndLayer = nullptr;

// Calls inside FUN_REGION_SET_PARENT_AND_LAYER, which reports for itself.
int g_inSetParent = 0;

void Notify(void *region, bool visible) {
    for (const AutoSubscribe *s = AutoSubscribe::head; s != nullptr; s = s->next)
        s->fn(region, visible);
}

void __fastcall Show_h(void *region) {
    const bool before = Visible(region);
    g_origShow(region);
    if (g_inSetParent == 0 && !before && Visible(region))
        Notify(region, true);
}

void __fastcall Hide_h(void *region) {
    const bool before = Visible(region);
    g_origHide(region);
    if (g_inSetParent == 0 && before && !Visible(region))
        Notify(region, false);
}

void __fastcall SetParentAndLayer_h(void *region, void *edx, void *parent, int layer, int show) {
    const bool before = Visible(region);
    ++g_inSetParent;
    g_origSetParentAndLayer(region, edx, parent, layer, show);
    --g_inSetParent;
    const bool after = Visible(region);
    if (g_inSetParent == 0 && before != after)
        Notify(region, after);
}

const Game::HookAutoRegister _showHook{Offsets::FUN_REGION_SHOW, reinterpret_cast<void *>(&Show_h),
                                       reinterpret_cast<void **>(&g_origShow)};
const Game::HookAutoRegister _hideHook{Offsets::FUN_REGION_HIDE, reinterpret_cast<void *>(&Hide_h),
                                       reinterpret_cast<void **>(&g_origHide)};
const Game::HookAutoRegister _setParentHook{Offsets::FUN_REGION_SET_PARENT_AND_LAYER,
                                            reinterpret_cast<void *>(&SetParentAndLayer_h),
                                            reinterpret_cast<void **>(&g_origSetParentAndLayer)};

} // namespace

bool Visible(const void *region) { return Game::Read<int>(region, Offsets::OFF_REGION_ACTUALLY_SHOWN) != 0; }

} // namespace Frame::RegionVisibility
