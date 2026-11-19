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

// Nameplate dimming under soft targeting. 1.12's CGNamePlateFrame::OnUpdate
// dims every plate but the target's (alpha 0x7F) whenever a selection exists.
// 3.80.2's plate update (FUN_14194e8b0) dims only under a locked target: a
// loose one (reasons 1/4/5/6) leaves every plate at its no-target alpha, and
// a plate is "selected" (FUN_14194f0f0) when its unit is the target or any
// soft unit. Without this, every SoftTargetForce pick dimmed the other plates
// while no target showed, flickering as the crosshair moved.
//
// The engine keeps its own decision and its own write: the selection load it
// decides from (SITE_NAMEPLATE_DIM_SELECTION) calls DimTarget instead, which
// answers 0 ("no target": full alpha) for a loose target or a soft unit's
// plate, and the selection otherwise. Correcting the alpha after OnUpdate
// instead would overwrite what the plate's Lua OnUpdate scripts set, which
// run at its end.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "object/Resolve.h"
#include "target/Reason.h"

#include <cstdint>

namespace Target::NameplateDim {

namespace {

uint64_t __stdcall DimTarget(const void *unit) {
    const uint64_t target = Reason::Selection();
    if (target == 0 || Reason::IsLoose() || Reason::IsSoftTarget(Object::Guid(unit)))
        return 0;
    return target;
}

// Stands in for the 12-byte selection load: the unit in EAX (kept), the
// answer in ECX:EDX as the load left it.
__declspec(naked) void DimSelectionThunk() {
    __asm {
        push eax
        push eax
        call DimTarget
        mov ecx, eax
        pop eax
        ret
    }
}

void Patch() {
    static const uint8_t kSelectionLoad[12] = {0x8B, 0x0D, 0xD8, 0xE2, 0xB4, 0x00,
                                               0x8B, 0x15, 0xDC, 0xE2, 0xB4, 0x00};
    static_assert(Offsets::VAR_CURRENT_SELECTION_GUID_LO == 0x00B4E2D8,
                  "kSelectionLoad holds the selection's address");
    Common::ReplaceWithCall(Offsets::SITE_NAMEPLATE_DIM_SELECTION, kSelectionLoad,
                            sizeof kSelectionLoad,
                            reinterpret_cast<const void *>(&DimSelectionThunk));
}

const Game::ModuleAutoRegister _autoreg{&Patch};

} // namespace

} // namespace Target::NameplateDim
