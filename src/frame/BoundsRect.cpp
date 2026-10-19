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

// `frame:GetBoundsRect()` -> left, bottom, width, height: the smallest rect
// that holds the frame, its shown regions and, recursively, its shown child
// frames, in the frame's own units. Nothing when that rect is empty.
//
// 4.3.4's CSimpleFrame::GetBoundsRect grows a rect seeded {FLT_MAX, FLT_MAX,
// 0, 0} by the frame's rect, each shown region's and each shown child's
// bounds, resolving any pending layout first (`if (IsResizePending())
// Resize(1)`). It is virtual: CSimpleScrollFrame's override leaves out its
// scroll child, whose content is drawn clipped. Script_GetBoundsRect then
// converts the rect as GetLeft does.
//
// 1.12 has the same walk without the override and without the resolve
// (0x00786F80, used only for the scroll range), so it can't be called for
// this. The walk below is 4.3.4's, on the engine's own pieces: the rect
// reader, the pending-layout test and resolve GetLeft uses, and the region
// and child lists with their shown flags.

#include "Game.h"
#include "Offsets.h"

#include <cfloat>
#include <cstdint>

namespace Frame::BoundsRect {

namespace {

// bottom, left, top, right in layout units - FUN_REGION_GET_RECT's order.
struct Rect {
    float bottom, left, top, right;
};

using GetRect_t = int(__fastcall *)(void *layout, void *edx, Rect *out);
using LayoutDirty_t = int(__fastcall *)(void *layout);
using Resize_t = void(__fastcall *)(void *layout, void *edx, int force);

void *LayoutOf(void *region) { return static_cast<uint8_t *>(region) + Offsets::OFF_REGION_ANCHOR; }

// Grows `rect` by a region's own rect, resolving a pending layout first.
void AddRegionRect(void *region, Rect &rect) {
    void *layout = LayoutOf(region);
    if (reinterpret_cast<LayoutDirty_t>(Offsets::FUN_REGION_LAYOUT_DIRTY)(layout) != 0)
        reinterpret_cast<Resize_t>(Offsets::FUN_LAYOUT_RESIZE)(layout, nullptr, 1);
    Rect own{};
    if (reinterpret_cast<GetRect_t>(Offsets::FUN_REGION_GET_RECT)(layout, nullptr, &own) == 0)
        return;
    rect.left = own.left < rect.left ? own.left : rect.left;
    rect.right = own.right > rect.right ? own.right : rect.right;
    rect.bottom = own.bottom < rect.bottom ? own.bottom : rect.bottom;
    rect.top = own.top > rect.top ? own.top : rect.top;
}

void AddBounds(void *frame, Rect &rect) {
    AddRegionRect(frame, rect);
    Game::ForEachFrameListObject(frame, Offsets::OFF_FRAME_REGION_LIST, [&rect](void *region) {
        if (Game::Read<int>(region, Offsets::OFF_REGION_DESIRED_SHOWN) != 0)
            AddRegionRect(region, rect);
    });
    // CSimpleScrollFrame's override: everything but the scroll child.
    void *scrollChild = Game::Lua::IsA(frame, Offsets::VAR_SCROLLFRAME_LUA_TYPE_ID)
                            ? Game::Read<void *>(frame, Offsets::OFF_SCROLLFRAME_SCROLL_CHILD)
                            : nullptr;
    Game::ForEachFrameListObject(frame, Offsets::OFF_FRAME_CHILD_LIST, [&rect, scrollChild](void *child) {
        if (child != scrollChild && Game::Read<int>(child, Offsets::OFF_FRAME_SHOWN) != 0)
            AddBounds(child, rect);
    });
}

int __fastcall Script_GetBoundsRect(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    Rect rect{FLT_MAX, FLT_MAX, 0.0f, 0.0f};
    AddBounds(frame, rect);
    if (!(rect.top > rect.bottom && rect.right > rect.left))
        return 0;
    // Script GetLeft's conversion: pixels, then out of the frame's scale.
    const float scale = Game::Read<float>(frame, Offsets::OFF_REGION_EFFECTIVE_SCALE);
    Game::Lua::PushNumber(L, Game::InternalToPixel(rect.left) / scale);
    Game::Lua::PushNumber(L, Game::InternalToPixel(rect.bottom) / scale);
    Game::Lua::PushNumber(L, Game::InternalToPixel(rect.right - rect.left) / scale);
    Game::Lua::PushNumber(L, Game::InternalToPixel(rect.top - rect.bottom) / scale);
    return 4;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"GetBoundsRect", &Script_GetBoundsRect},
};

const Game::Doc::Field kRets[] = {
    Game::Doc::Req("left", "number"),
    Game::Doc::Req("bottom", "number"),
    Game::Doc::Req("width", "number"),
    Game::Doc::Req("height", "number"),
};
const Game::Doc::Function kGetBoundsRect{
    "Returns the smallest rect that holds the frame, its shown regions and its shown child frames. "
    "Returns nothing when that rect is empty.",
    {}, kRets};

const Game::Doc::Method g_methodDocs[] = {
    {"GetBoundsRect", &kGetBoundsRect},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::BoundsRect
