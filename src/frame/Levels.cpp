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

// A frame moved under a new parent takes the frames under it along in frame
// level, so the whole subtree sits above its new parent.
//
// Vanilla's SetParent gives the frame the new parent's strata (the strata
// setter recurses into its children) and the parent's level + 1, but it calls
// the level setter without the setter's children flag, so the frames under it
// keep the levels they had. A panel built in one place and moved into a raised
// window then draws its contents behind that window. Zygor Guides Viewer 2.0's
// options panel, shown in the Interface Options window that
// LibBetterBlizzOptions-1.0 raises to FULLSCREEN_DIALOG, sat at level 7 with
// its Ace3 widgets at levels 1-4, behind the window's own level 5. 3.3.5
// shows the same Ace3 panels, moved the same way, above their window.
//
// The engine's level setter already moves a frame's children when asked: each
// child of the same strata goes up or down by the same amount, recursively,
// so the layering inside the subtree is kept. The hook asks for that on
// SetParent's two calls only (with a parent, and with none), recognized by
// their return addresses. Script SetFrameLevel and the setter's other callers
// behave as before.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <intrin.h> // _ReturnAddress

namespace Frame::Levels {

namespace {

using SetFrameLevel_t = void(__thiscall *)(void *frame, int level, int children);
SetFrameLevel_t g_origSetFrameLevel = nullptr;

void __fastcall SetFrameLevel_h(void *frame, void * /*edx*/, int level, int children) {
    const uintptr_t from = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (from == Offsets::RET_SETPARENT_SET_FRAME_LEVEL || from == Offsets::RET_SETPARENT_NIL_SET_FRAME_LEVEL)
        children = 1;
    g_origSetFrameLevel(frame, level, children);
}

const Game::HookAutoRegister _hook{Offsets::FUN_FRAME_SET_FRAME_LEVEL,
                                   reinterpret_cast<void *>(&SetFrameLevel_h),
                                   reinterpret_cast<void **>(&g_origSetFrameLevel)};

} // namespace

} // namespace Frame::Levels
