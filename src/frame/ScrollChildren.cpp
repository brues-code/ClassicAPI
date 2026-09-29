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

// A ScrollFrame keeps clipping its scroll child when the ScrollFrame, or any
// frame above it, moves to a new parent.
//
// A frame a ScrollFrame draws carries a "scrolled" flag: the strata pass
// leaves it out, and the ScrollFrame draws it inside a viewport cut to its
// own rect. SetScrollChild sets the flag on the child and everything under
// it, and marks the child itself as the scroll child. SetParent sets a moved
// frame's flag from its new parent through the same setter, which passes the
// value on down to every child frame. It doesn't stop at a ScrollFrame, so a
// frame holding a ScrollFrame, moved under a parent that isn't scrolled,
// clears the flag on the ScrollFrame's scroll child and everything in it.
// The strata pass then draws them whole, past the ScrollFrame's edges.
//
// AceGUI-3.0 builds each widget under UIParent and then moves it into its
// container, so the content of every AceGUI ScrollFrame lost its clipping
// the moment it was used. AceConfigDialog-3.0's option pages in the
// Interface Options window drew their widgets below the window, off the
// bottom of the screen: on Zygor Guides Viewer 2.0's "Display" page, 61
// frames under the scroll child had lost the flag. 3.3.5 draws the same
// pages clipped to their box.
//
// The hook keeps a scroll child scrolled whenever the setter is only passing
// a value down: SetParent's call and the setter's own recursion, which comes
// back through the hooked entry, so a scroll child at any depth is caught.
// SetScrollChild's own calls, which make a frame the scroll child or stop it
// being one, go through unchanged.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Frame::ScrollChildren {

namespace {

// `scrollChild`: 1 / 0 set or clear the scroll-child mark (SetScrollChild);
// -1 leaves it as it is (SetParent and the recursion).
using SetScrolled_t = void(__thiscall *)(void *frame, int scrolled, int scrollChild);
SetScrolled_t g_origSetScrolled = nullptr;

void __fastcall SetScrolled_h(void *frame, void * /*edx*/, int scrolled, int scrollChild) {
    if (scrollChild < 0 &&
        (Game::Read<uint32_t>(frame, Offsets::OFF_FRAME_FLAGS) & Offsets::FRAME_FLAG_SCROLL_CHILD) != 0)
        scrolled = 1; // its ScrollFrame draws it, whatever its ancestors are
    g_origSetScrolled(frame, scrolled, scrollChild);
}

const Game::HookAutoRegister _hook{Offsets::FUN_FRAME_SET_SCROLLED,
                                   reinterpret_cast<void *>(&SetScrolled_h),
                                   reinterpret_cast<void **>(&g_origSetScrolled)};

} // namespace

} // namespace Frame::ScrollChildren
