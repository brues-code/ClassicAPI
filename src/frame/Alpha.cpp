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

// Frame alpha as 3.x draws it: a frame's own alpha times its parent's.
//
// Vanilla keeps one alpha byte per frame (Offsets::OFF_FRAME_ALPHA) and copies
// it down: SetAlpha - and the XML `alpha` attribute, which runs the same setter
// - writes the value into every child frame that exists right then, replacing
// the child's own, and SetParent takes nothing from the new parent. So:
//   - a frame built or parented inside one that is already transparent draws
//     at full alpha. Zygor Guides Viewer's border glow is an alpha="0.0" frame
//     holding its glow textures in a child; on vanilla the glow shows at full
//     strength from login until its first flash sets the parent's alpha again;
//   - a child's own alpha is lost the moment its parent's changes;
//   - GetAlpha on a child reports whatever its parent last copied in, so an
//     animation fading a child starts from its parent's value.
// 3.x keeps each frame's own alpha and draws it multiplied by its parent's.
//
// Co-hooks of the setter (FUN_FRAME_SET_FRAME_ALPHA), SetParent
// (FUN_FRAME_SET_PARENT), the frame destructor and frame GetAlpha:
//   - SetAlpha and XML `alpha` record the frame's own alpha;
//   - the byte holds own x the parent's drawn alpha. When it changes, the
//     frame's regions recolor and each child is recomputed from its own alpha,
//     through the child's own vtable setter, as the engine's copy-down calls
//     it - so the Model family's override, which hands the byte to its model,
//     still runs;
//   - SetParent recomputes the frame and everything under it from the new
//     parent. The frame constructor calls it too, after installing the base
//     vtable and initializing the byte and both lists;
//   - GetAlpha returns the own alpha.
// A frame whose alpha was never set has own alpha 1.
//
// Engine code that reads the byte directly gets the drawn value. That's what
// the region recolors and the Model want. The chat bubble and nameplate fades
// compare it with their own targets. They're WorldFrame children, where drawn
// and own are the same while WorldFrame is opaque.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <unordered_map>

namespace Frame::Alpha {

namespace {

using SetFrameAlpha_t = void(__thiscall *)(void *frame, uint32_t alpha);
using SetParent_t = void(__thiscall *)(void *frame, void *parent);
using Destructor_t = void(__thiscall *)(void *frame);
using Recolor_t = void(__thiscall *)(void *region, int arg);
using ScriptFn_t = int(__fastcall *)(void *L);

SetFrameAlpha_t g_origSetAlpha = nullptr;
SetParent_t g_origSetParent = nullptr;
Destructor_t g_origDestructor = nullptr;
ScriptFn_t g_origGetAlpha = nullptr;

// Own alpha of every live frame that set one other than 255. The destructor
// hook drops a frame's entry; the reload cleanup drops everything left.
std::unordered_map<const void *, uint8_t> g_own;

// Set while a frame is being given its drawn alpha through its vtable setter.
bool g_propagating = false;
int g_depth = 0;

// Deeper than any real frame tree; stops a corrupt child list from recursing.
constexpr int kMaxDepth = 64;

uint8_t *DrawnByte(void *frame) {
    return static_cast<uint8_t *>(frame) + Offsets::OFF_FRAME_ALPHA;
}

uint8_t Own(const void *frame) {
    const auto it = g_own.find(frame);
    return it == g_own.end() ? 255 : it->second;
}

void SetOwn(const void *frame, uint8_t alpha) {
    if (alpha == 255)
        g_own.erase(frame);
    else
        g_own[frame] = alpha;
}

uint8_t Mul(uint8_t a, uint8_t b) {
    return static_cast<uint8_t>((static_cast<uint32_t>(a) * b + 127) / 255);
}

uint8_t DrawnFor(void *frame, uint8_t own) {
    void *parent = Game::Read<void *>(frame, Offsets::OFF_REGION_PARENT);
    return parent != nullptr ? Mul(own, *DrawnByte(parent)) : own;
}

// Walks one of the frame's intrusive lists (node+4 next, node+8 object; a NULL
// or low-bit-1 link ends it), as the engine's setter does.
template <typename Fn> void ForEachLink(void *frame, uintptr_t listOffset, Fn fn) {
    uintptr_t link = Game::Read<uintptr_t>(frame, listOffset);
    while (link != 0 && (link & 1) == 0) {
        void *obj = *reinterpret_cast<void *const *>(link + 8);
        const uintptr_t next = *reinterpret_cast<const uintptr_t *>(link + 4);
        if (obj != nullptr)
            fn(obj);
        link = next;
    }
}

// Gives `frame` its drawn alpha through its own vtable setter.
void ApplyDrawn(void *frame, uint8_t drawn) {
    if (*DrawnByte(frame) == drawn || g_depth >= kMaxDepth)
        return;
    const bool saved = g_propagating;
    g_propagating = true;
    ++g_depth;
    void **vtable = *static_cast<void ***>(frame);
    reinterpret_cast<SetFrameAlpha_t>(
        vtable[Offsets::VTBL_FRAME_SET_FRAME_ALPHA / sizeof(void *)])(frame, drawn);
    --g_depth;
    g_propagating = saved;
}

// The engine setter's body with `drawn` in place of the copied value: an
// unchanged byte returns early, otherwise the regions recolor and each child
// is redrawn from its own alpha. SetFrameAlpha_h runs it in place of the
// original, which would copy the value down.
void Draw(void *frame, uint8_t drawn) {
    uint8_t *byte = DrawnByte(frame);
    if (*byte == drawn)
        return;
    *byte = drawn;
    ForEachLink(frame, Offsets::OFF_FRAME_REGION_LIST, [](void *region) {
        void **vtable = *static_cast<void ***>(region);
        reinterpret_cast<Recolor_t>(
            vtable[Offsets::VTBL_REGION_RECOLOR / sizeof(void *)])(region, 0);
    });
    ForEachLink(frame, Offsets::OFF_FRAME_CHILD_LIST,
                [drawn](void *child) { ApplyDrawn(child, Mul(Own(child), drawn)); });
}

void __fastcall SetFrameAlpha_h(void *frame, void * /*edx*/, uint32_t alpha) {
    const uint8_t value = static_cast<uint8_t>(alpha);
    if (g_propagating) {
        // Reached from ApplyDrawn: `value` is already the drawn alpha.
        Draw(frame, value);
        return;
    }
    SetOwn(frame, value);
    Draw(frame, DrawnFor(frame, value));
}

void __fastcall SetParent_h(void *frame, void * /*edx*/, void *parent) {
    g_origSetParent(frame, parent);
    ApplyDrawn(frame, DrawnFor(frame, Own(frame)));
}

void __fastcall Destructor_h(void *frame, void * /*edx*/) {
    g_own.erase(frame);
    g_origDestructor(frame);
}

// frame:GetAlpha() - the own alpha, not the drawn one.
int __fastcall GetAlpha_h(void *L) {
    const int n = g_origGetAlpha(L);
    if (n != 1)
        return n;
    const void *frame = Game::Lua::ResolveObject(L, 1);
    if (frame == nullptr)
        return n;
    Game::Lua::SetTop(L, -2);
    Game::Lua::PushNumber(L, Own(frame) / 255.0);
    return 1;
}

void PrepareForReload() {
    g_own.clear();
}

const Game::HookAutoRegister _setAlphaHook{Offsets::FUN_FRAME_SET_FRAME_ALPHA,
                                           reinterpret_cast<void *>(&SetFrameAlpha_h),
                                           reinterpret_cast<void **>(&g_origSetAlpha)};
const Game::HookAutoRegister _setParentHook{Offsets::FUN_FRAME_SET_PARENT,
                                            reinterpret_cast<void *>(&SetParent_h),
                                            reinterpret_cast<void **>(&g_origSetParent)};
const Game::HookAutoRegister _destructorHook{Offsets::FUN_FRAME_DESTRUCTOR,
                                             reinterpret_cast<void *>(&Destructor_h),
                                             reinterpret_cast<void **>(&g_origDestructor)};
const Game::HookAutoRegister _getAlphaHook{Offsets::FUN_SCRIPT_FRAME_GETALPHA,
                                           reinterpret_cast<void *>(&GetAlpha_h),
                                           reinterpret_cast<void **>(&g_origGetAlpha)};
const Game::ReloadAutoRegister _reload{&PrepareForReload};

} // namespace

} // namespace Frame::Alpha
