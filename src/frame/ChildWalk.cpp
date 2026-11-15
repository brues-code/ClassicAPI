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

// A frame's show and hide survive a child that moves frames from its OnShow
// or OnHide.
//
// When a frame becomes visible, 1.12's FUN_SIMPLEFRAME_SHOW shows each child
// in turn (the child's VTBL_FRAME_SHOW), and each child runs its OnShow
// right there, before the walk goes on. The walk reads the next child from
// the current node only after that call. A script that reparents the current
// child unlinks and frees its node, so the walk then reads a freed node: the
// client crashed in the walk, and once returned to address 0. Hiding
// (FUN_SIMPLEFRAME_HIDE, OnHide) walks the same way. 3.3.5 and 4.3.4 do the
// same (CSimpleFrame::ShowThis / HideThis); retail's Settings window and the
// addons it hosts reparent frames from these scripts. Auctionator's options
// page opens another page from its OnShow, and the window takes the first
// page out of its canvas with SetParent(nil) while the canvas is still being
// shown.
//
// Each walk is replaced at its first instruction: the children are copied
// first, and each one is shown or hidden only if it is still a child of the
// frame when its turn comes. A child that left is skipped, as the engine
// would never have reached it; a child that arrived was shown by its own
// SetParent. The call per child is the engine's own vtable slot, with the
// child in ECX, in the engine's order. The sites are checked before they are
// written, so a client whose code differs there is left alone.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Frame::ChildWalk {

namespace {

using Slot_t = void(__fastcall *)(void *self, void *edx);

void WalkChildren(void *frame, uintptr_t slot) {
    // Most frames have a few children; a long list goes to the heap.
    constexpr size_t kInline = 64;
    void *inlineList[kInline];
    std::vector<void *> heapList;
    size_t count = 0;
    Game::ForEachFrameListObject(frame, Offsets::OFF_FRAME_CHILD_LIST, [&](void *child) {
        if (count < kInline) {
            inlineList[count] = child;
        } else {
            if (heapList.empty())
                heapList.assign(inlineList, inlineList + kInline);
            heapList.push_back(child);
        }
        ++count;
    });

    void *const *children = heapList.empty() ? inlineList : heapList.data();
    for (size_t i = 0; i < count; ++i) {
        void *child = children[i];
        if (Game::Read<void *>(child, Offsets::OFF_REGION_PARENT) != frame)
            continue;
        Game::Read<Slot_t>(Game::Read<void *>(child, 0), slot)(child, nullptr);
    }
}

void __cdecl ShowChildren(void *frame) { WalkChildren(frame, Offsets::VTBL_FRAME_SHOW); }
void __cdecl HideChildren(void *frame) { WalkChildren(frame, Offsets::VTBL_FRAME_HIDE); }

constexpr uintptr_t addrShowResume = Offsets::SITE_SIMPLEFRAME_SHOW_CHILD_WALK_RESUME;
constexpr uintptr_t addrHideResume = Offsets::SITE_SIMPLEFRAME_HIDE_CHILD_WALK_RESUME;

// Reached by the `call` written over the walk's first instruction, with the
// frame in ESI. The call's return address is dropped: the stub leaves at the
// walk's end instead. EAX, ECX and EDX are free there.
__declspec(naked) void ShowStub() {
    __asm {
        add esp, 4
        push esi
        call ShowChildren
        add esp, 4
        mov eax, addrShowResume
        jmp eax
    }
}

__declspec(naked) void HideStub() {
    __asm {
        add esp, 4
        push esi
        call HideChildren
        add esp, 4
        mov eax, addrHideResume
        jmp eax
    }
}

// `mov ebx, [esi+0x300]`
constexpr uint8_t kWalkStart[] = {0x8B, 0x9E, 0x00, 0x03, 0x00, 0x00};
static_assert(Offsets::OFF_FRAME_CHILD_LIST == 0x300, "kWalkStart encodes the child list offset");

// Runs on every glue and in-game registration (each /reload too): the glue
// screens show and hide frames through the same code. A site already written
// is recognized and left as it is.
void Apply() {
    Common::ReplaceWithCall(Offsets::SITE_SIMPLEFRAME_SHOW_CHILD_WALK, kWalkStart, sizeof kWalkStart,
                            reinterpret_cast<const void *>(&ShowStub));
    Common::ReplaceWithCall(Offsets::SITE_SIMPLEFRAME_HIDE_CHILD_WALK, kWalkStart, sizeof kWalkStart,
                            reinterpret_cast<const void *>(&HideStub));
}

const Game::ModuleAutoRegister _autoreg{&Apply};
const Game::GlueModuleAutoRegister _glueAutoreg{&Apply};

} // namespace

} // namespace Frame::ChildWalk
