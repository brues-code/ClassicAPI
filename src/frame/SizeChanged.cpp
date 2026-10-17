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

// OnSizeChanged receives the frame's new width and height in the units
// GetWidth / GetHeight return.
//
// The engine fires the script from one place, the size-changed vmethod
// FUN_FRAME_ON_SIZE_CHANGED, and passes it the size of the frame's screen
// rect as it is, in layout units: a 20 x 10 frame's handler saw (0.0092,
// 0.0046). 3.3.5 hands the vmethod the rect size divided by the frame's
// layout scale (CSimpleFrame::OnFrameSizeChanged, 0x0048FA20), and its
// RunOnSizeChangedScript (0x0048F070) then converts both values with the
// chain 1.12's own GetWidth (0x007A1E00) uses on a stored width:
// FUN_0041AE40(FUN_0041AD70() * DAT_007FFD68 * v), which is
// Game::InternalToPixel. The hook does both: it converts, then divides by the
// frame's effective scale (OFF_REGION_EFFECTIVE_SCALE), as GetLeft does. In
// game, converting alone gave (18.2, 9.1) at a 0.91 UI scale, against
// GetWidth / GetHeight's (20, 10); with the division, (19.99993, 9.99997).
//
// The vmethod does nothing else with the values, so the frame's layout is
// unaffected; only the script's arguments (and the arg1 / arg2 globals)
// change. Neither stock FrameXML nor any addon checked reads them.

#include "Game.h"
#include "Offsets.h"

namespace Frame::SizeChanged {

namespace {

using OnSizeChanged_t = void(__thiscall *)(void *frame, float w, float h);
OnSizeChanged_t g_orig = nullptr;

void __fastcall OnSizeChanged_h(void *frame, void * /*edx*/, float w, float h) {
    double scale = Game::Read<float>(frame, Offsets::OFF_REGION_EFFECTIVE_SCALE);
    if (!(scale > 0.0))
        scale = 1.0;
    g_orig(frame, static_cast<float>(Game::InternalToPixel(w) / scale),
           static_cast<float>(Game::InternalToPixel(h) / scale));
}

const Game::HookAutoRegister _hook{Offsets::FUN_FRAME_ON_SIZE_CHANGED,
                                   reinterpret_cast<void *>(&OnSizeChanged_h),
                                   reinterpret_cast<void **>(&g_orig)};

} // namespace

} // namespace Frame::SizeChanged
