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

// `frame:SetParent(parent)` makes a shown frame visible under a visible
// parent, as 3.3.5's does.
//
// 1.12's CSimpleFrame::SetParent (FUN_FRAME_SET_PARENT) hides the frame
// first when it was visible, and shows it again afterwards only if it WAS
// visible and the new parent is visible or absent. 3.3.5's
// (CSimpleFrame.cpp:1677, wowdecomp) tests the shown flag instead:
// `if (shown && (!m_parent || m_parent->m_isVisible)) ShowThis()`. So a
// frame that is shown but invisible, because its old parent was hidden,
// stays invisible in 1.12 when it moves under a visible parent, and becomes
// visible in 3.3.5. Retail's Settings window reaches that case: it moves the
// key binding buttons onto an input blocker while a key is awaited, hides
// the blocker, and only then moves the buttons back to their row.
//
// The change is on Script SetParent only. The frame constructor calls
// FUN_FRAME_SET_PARENT directly, before the frame's XML and scripts are
// loaded, and the engine shows new frames itself later; showing them from
// there would change when their OnShow runs. After the script call, a frame
// that is shown, not visible, and under a visible parent (or none) gets the
// frame's own Show slot (VTBL_FRAME_SHOW), the call 3.3.5's ShowThis is, so
// a subclass such as a model builds its model as on any show.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Frame::ParentVisibility {

namespace {

using Script_t = int(__fastcall *)(void *L);
Script_t g_origSetParent = nullptr;

bool Flag(const void *frame, uintptr_t offset) { return Game::Read<uint32_t>(frame, offset) != 0; }

int __fastcall SetParent_h(void *L) {
    void *frame = Game::Lua::ResolveFrame(L, 1, /*raiseError*/ false);
    const int results = g_origSetParent(L);
    if (frame == nullptr || !Flag(frame, Offsets::OFF_FRAME_SHOWN) || Flag(frame, Offsets::OFF_FRAME_VISIBLE))
        return results;
    const void *parent = Game::Read<void *>(frame, Offsets::OFF_REGION_PARENT);
    if (parent != nullptr && !Flag(parent, Offsets::OFF_FRAME_VISIBLE))
        return results;
    using Slot_t = void(__fastcall *)(void *self, void *edx);
    Game::Read<Slot_t>(Game::Read<void *>(frame, 0), Offsets::VTBL_FRAME_SHOW)(frame, nullptr);
    return results;
}

const Game::HookAutoRegister _hook{Offsets::FUN_SCRIPT_REGION_SET_PARENT,
                                   reinterpret_cast<void *>(&SetParent_h),
                                   reinterpret_cast<void **>(&g_origSetParent)};

} // namespace

} // namespace Frame::ParentVisibility
