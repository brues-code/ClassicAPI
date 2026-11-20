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

#pragma once

#include "AutoList.h"

#include <cstdint>

// Observers on the active player's own fields (bank PLAYER), registered and
// removed with the engine's: CGPlayer_C::AddActiveWatchers
// (FUN_PLAYER_ADD_ACTIVE_WATCHERS) and its removal. The active player keeps a
// watch copy of every player field, so any player-section offset is safe to
// watch — the engine itself watches coinage, far sight and the combo byte this
// way.
//
// MinHook allows one hook per address, so this module owns both hooks and every
// feature declares its field instead:
//
//   static const Player::ActiveWatchers::AutoWatch _w{{offset, size, &Callback}};
//
// The callback ABI is the descriptor observer's (FUN_DESC_OBSERVER_REGISTER in
// Offsets.h): it runs only when the field's bytes changed, returns 1.
namespace Player::ActiveWatchers {

using Callback = int(__fastcall *)(uint32_t fieldOffset, uint32_t size, uint32_t guidLo,
                                   uint32_t guidHi, const void *oldValues, void *userArg);

struct Watch {
    uint32_t fieldOffset; // player-section offset, as OFF_CGPLAYER_* fields are
    int size;
    Callback callback;
};

using AutoWatch = AutoList<struct AutoWatchTag, Watch>;

} // namespace Player::ActiveWatchers
