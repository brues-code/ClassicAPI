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

// `PLAYER_MOUNT_DISPLAY_CHANGED` — the player mounted, dismounted or changed
// mount (retail's no-payload event; its action bars refresh usability on it).
//
// 1.12 reacts to a unit's new UNIT_FIELD_MOUNTDISPLAYID in
// CGUnit_C::OnMountDisplayChanged (FUN_UNIT_MOUNT_DISPLAY_CHANGED), which the
// field's type watcher calls with the old value from one E8 call. That call
// points here instead: the function runs the handler, then fires the event
// when the unit is the player. The watcher calls only on a real change.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Player::MountDisplay {

namespace {

const Game::Doc::Event kDoc{"Unit", "Fires when the player mounts, dismounts or changes mount.",
                            {}};
const Event::Custom::AutoReserve kChanged{"PLAYER_MOUNT_DISPLAY_CHANGED", &kDoc};

using MountDisplayChanged_t = void(__thiscall *)(void *unit, uint32_t oldMountDisplay);

void __fastcall MountDisplayChangedSite(void *unit, void *, uint32_t oldMountDisplay) {
    reinterpret_cast<MountDisplayChanged_t>(Offsets::FUN_UNIT_MOUNT_DISPLAY_CHANGED)(
        unit, oldMountDisplay);
    if (unit == Unit::Identity::PlayerObject())
        Event::Custom::Fire(kChanged.Slot(), "");
}

void Register() {
    Common::RedirectCall(Offsets::CALL_MOUNT_DISPLAY_FIELD_CHANGED,
                         Offsets::FUN_UNIT_MOUNT_DISPLAY_CHANGED,
                         reinterpret_cast<const void *>(&MountDisplayChangedSite));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Player::MountDisplay
