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

#include "texture/CornerStore.h"

#include "Game.h"
#include "Offsets.h"

namespace Texture::CornerStore {

namespace {

using Frame::Layout::Rect;

// __thiscall(region, const Rect *rect); dummy-EDX form.
using StoreCorners_t = void(__fastcall *)(void *region, void *edx, Rect *rect);
StoreCorners_t g_origStore = nullptr;

void __fastcall StoreCorners_h(void *region, void *edx, Rect *rect) {
    g_origStore(region, edx, rect);
    for (const AutoSubscribe *s = AutoSubscribe::head; s != nullptr; s = s->next)
        s->fn(region);
}

const Game::HookAutoRegister _hook{Offsets::FUN_REGION_STORE_CORNERS, reinterpret_cast<void *>(&StoreCorners_h),
                                   reinterpret_cast<void **>(&g_origStore)};

} // namespace

void CropInput(void *region, Rect &rect) {
    using Crop_t = void(__thiscall *)(void *region, Rect *rect);
    if (Game::Read<int>(region, Offsets::OFF_REGION_TEXCOORD_MODIFIES_RECT) != 0)
        reinterpret_cast<Crop_t>(Offsets::FUN_REGION_TEXCOORD_CROP)(region, &rect);
}

// The engine's own store, so the listeners above run as they do on a layout.
void StoreFromRect(void *texture) {
    Rect rect = Frame::Layout::RectOf(texture);
    CropInput(texture, rect);
    reinterpret_cast<StoreCorners_t>(Offsets::FUN_REGION_STORE_CORNERS)(texture, nullptr, &rect);
}

} // namespace Texture::CornerStore
