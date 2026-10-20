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

namespace Spell::Cancel {

// Cancels the local player's first visible buff whose spell applies aura
// `auraType` (`EffectApplyAuraName`), with `EffectMiscValue == miscValue` when
// `miscValue` is not negative. Sends CMSG_CANCEL_AURA through the engine's own
// sender. True when a matching buff was found and its cancel sent.
bool CancelPlayerAura(int auraType, int miscValue = -1);

} // namespace Spell::Cancel
