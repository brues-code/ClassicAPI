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

// The soft-target names: retail shows a soft unit's overhead name as it shows
// the target's, whatever the UnitName* cvars say (retail: `softenemy` past
// nameplate range keeps its name, and its circle). `softfriend` takes the same
// rule, by symmetry with the circles; not probed. No cvar of its own: it
// follows SoftTargetEnemy / SoftTargetFriend.
//
// 1.12's rule is CGUnit_C::ShouldShowName (0x6070A0), which the name draw
// PLAYERNAMEDESC::UpdatePlate (0x6C6E90) asks every frame, so a new answer
// shows or hides the name that frame. Its tests, the first that decides
// winning: no name when CanSelect refuses the unit, or when it has a
// nameplate or a chat bubble (they carry the name); the active player by
// UnitNameOwn; the target always; then the hidden-name bits of descriptor
// byte +0x213 and the UnitName* mask. A soft unit gets the target's branch:
// the first three tests, then 1. The own-player test does not apply, because
// the player is never a soft unit. So a soft unit in plate range shows its
// plate and no name, as the target does.

#include "Game.h"
#include "Offsets.h"
#include "target/SoftUnit.h"
#include "tick/FrameTick.h"

#include <cstdint>

namespace Target::SoftTargetName {

namespace {

using ShouldShowName_t = int(__fastcall *)(void *unit, void *edx, uint32_t mask);
using CanSelect_t = int(__thiscall *)(void *unit);

ShouldShowName_t g_origShouldShowName = nullptr;

// The soft units (FrameTick), 0 when the side is off.
uint64_t g_soft[2] = {};

// 0x6070A8-0x6070F1, the path a target takes, for a soft unit.
int __fastcall ShouldShowName_h(void *unit, void *edx, uint32_t mask) {
    const uint64_t guid =
        Game::Read<uint64_t>(Game::Read<void *>(unit, Offsets::OFF_UNIT_GUID_PTR), 0);
    if (guid == 0 || (guid != g_soft[0] && guid != g_soft[1]))
        return g_origShouldShowName(unit, edx, mask);
    const auto canSelect = Game::Read<CanSelect_t>(Game::Read<void *>(unit, 0),
                                                   Offsets::OFF_CGOBJECT_VTBL_CAN_SELECT);
    return canSelect(unit) != 0 &&
           Game::Read<void *>(unit, Offsets::OFF_UNIT_NAMEPLATE) == nullptr &&
           Game::Read<void *>(unit, Offsets::OFF_UNIT_CHAT_BUBBLE) == nullptr;
}

const Game::HookAutoRegister _hook{Offsets::FUN_CGUNIT_SHOULD_SHOW_NAME,
                                   reinterpret_cast<void *>(&ShouldShowName_h),
                                   reinterpret_cast<void **>(&g_origShouldShowName)};

// Picked on the UI frame, after the scene, as the circles are. The name draw
// runs inside the scene (the model's draw callback 0x6C7780), so the names
// follow the next scene.
void OnFrameTick() {
    g_soft[0] = SoftUnit::Get(SoftUnit::Side::Enemy);
    g_soft[1] = SoftUnit::Get(SoftUnit::Side::Friend);
}

const Tick::FrameTick::AutoSubscribe _tick{&OnFrameTick};

} // namespace

} // namespace Target::SoftTargetName
