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

// A region gets its frame's scale as soon as it gets the frame.
//
// Vanilla gives a texture or font string its frame's scale only when it links
// the region into a draw layer (CSimpleFrame::AddRegionToLayer), and it does
// that only once the frame is visible. Until then the region keeps the scale
// 1.0 it was built with: a region made on, or moved to, a hidden frame lays
// out in the wrong units, and GetLeft/GetRect report its position divided by
// the wrong scale. Probed: on a hidden row under a 0.9-scale UIParent, a
// texture anchored to the row's left edge reported 454,446 against the row's
// 499,498, the same ratio after moving the row; on a shown row the two
// matched. Retail regions carry their frame's scale whether or not it is
// shown, and Blizzard_Menu measures its rows from their regions while the rows
// are still hidden.
//
// CScriptRegion::SetRegionParent links a region into its new parent's region
// list (CSimpleFrame::AddRegion). The thunk below makes that call, then the
// one AddRegionToLayer would make later: the region layout's own scale setter
// (vtable +0x10) with the frame's layout scale and no force. The draw layer is
// still joined at show, and a frame's later scale changes still reach every
// region through the frame's own scale setter.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Frame::RegionScale {

namespace {

using SetScale_t = void(__thiscall *)(void *layout, float scale, int force);

constexpr uintptr_t addrAddRegion = Offsets::FUN_FRAME_ADD_REGION;

void __fastcall TakeParentScale(uint8_t *region, uint8_t *parent) {
    void *layout = region + Offsets::OFF_REGION_ANCHOR;
    const auto setScale = reinterpret_cast<SetScale_t>(
        (*reinterpret_cast<void ***>(layout))[Offsets::VTBL_LAYOUT_SET_SCALE / sizeof(void *)]);
    setScale(layout, Game::Read<float>(parent, Offsets::OFF_REGION_EFFECTIVE_SCALE), 0);
}

// Stands in for `push esi; mov ecx,edi; call CSimpleFrame::AddRegion`.
// ESI = the region, EDI = the new parent (never null on this path).
__declspec(naked) void AddRegionThunk() {
    __asm {
        push esi
        mov ecx, edi
        call addrAddRegion
        pushad
        mov ecx, esi
        mov edx, edi
        call TakeParentScale
        popad
        ret
    }
}

// Runs on every registration pass; a site already patched no longer matches
// and is skipped.
void Apply() {
    static const uint8_t kAddRegionCall[8] = {0x56, 0x8B, 0xCF, 0xE8, 0xDD, 0xA9, 0xFE, 0xFF};
    Common::ReplaceWithCall(Offsets::SITE_REGION_PARENT_ADD_REGION, kAddRegionCall, sizeof kAddRegionCall,
                            reinterpret_cast<const void *>(&AddRegionThunk));
}

const Game::ModuleAutoRegister _autoreg{&Apply};
const Game::GlueModuleAutoRegister _glueAutoreg{&Apply};

} // namespace

} // namespace Frame::RegionScale
