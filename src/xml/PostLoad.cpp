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

// A frame loaded from XML lays out after its OnLoad, from the layout queue.
//
// 1.12's finalize (FUN_FRAME_POST_LOAD_XML) ends the layout deferral of a
// visible frame, its title region and its registered regions with
// FUN_REGION_LAYOUT_REALIZE(0), which resolves a pending layout at once. That
// is before the frame's OnLoad, and before the children it then builds run
// theirs, so a frame that loads visible gets OnSizeChanged before OnLoad.
// Building the children first changes nothing: resolving a child ends the
// deferral of the frame it anchors to and lays that frame out too (0x007A2F90).
//
// Retail lays these frames out later, when the resize list runs at the end of
// the frame (its own note in Blizzard_LegacyRewardTrack.lua). Its EventFrame,
// ScrollBox and scroll bars trigger callbacks from OnSizeChanged on the
// registry their OnLoad creates, and Blizzard_EventTrace loads its window
// visible with them inside.
//
// The finalize's three calls go to EndLoadDeferral, which ends the deferral
// the same way but sends a pending layout to the queue through
// FUN_LAYOUT_RESIZE(0), the path SetPoint takes. The queue is drained on the
// top's update and after the interface build, so every frame of the load has
// run OnLoad first. A script that reads a size or an edge still lays its frame
// out on the spot.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Xml::PostLoad {

namespace {

using LayoutDirty_t = int(__fastcall *)(void *layout);
using Resize_t = void(__fastcall *)(void *layout, void *edx, int force);

// Stands in for FUN_REGION_LAYOUT_REALIZE(0): `__thiscall(layout, int)`, `ret 4`.
void __fastcall EndLoadDeferral(void *layout, void * /*edx*/, int /*deferred*/) {
    Game::Ref<uint32_t>(layout, Offsets::OFF_LAYOUT_FLAGS) &=
        ~static_cast<uint32_t>(Offsets::LAYOUT_FLAG_DEFERRED);
    if (reinterpret_cast<LayoutDirty_t>(Offsets::FUN_REGION_LAYOUT_DIRTY)(layout) != 0)
        reinterpret_cast<Resize_t>(Offsets::FUN_LAYOUT_RESIZE)(layout, nullptr, 0);
}

void PatchCode() {
    const auto target = reinterpret_cast<const void *>(&EndLoadDeferral);
    Common::RedirectCall(Offsets::CALL_FRAME_POST_LOAD_XML_UNDEFER_FRAME,
                         Offsets::FUN_REGION_LAYOUT_REALIZE, target);
    Common::RedirectCall(Offsets::CALL_FRAME_POST_LOAD_XML_UNDEFER_TITLE,
                         Offsets::FUN_REGION_LAYOUT_REALIZE, target);
    Common::RedirectCall(Offsets::CALL_FRAME_POST_LOAD_XML_UNDEFER_REGION,
                         Offsets::FUN_REGION_LAYOUT_REALIZE, target);
}

// Each pass runs before its GlueXML or FrameXML loads; the patch is idempotent.
const Game::GlueModuleAutoRegister _gluePatch{&PatchCode};
const Game::ModuleAutoRegister _autoreg{&PatchCode};

} // namespace

} // namespace Xml::PostLoad
