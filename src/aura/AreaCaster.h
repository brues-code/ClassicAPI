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

#include <cstdint>

// Who cast a party area aura: a totem's (Strength of Earth), attributed to
// the totem's owner, or a player's (Leader of the Pack, the paladin auras).
// No packet names it. See AreaCaster.cpp.
namespace Aura::AreaCaster {

// The caster whose party area aura put `spellId` on the unit `targetGuid` (a
// totem's owner for a totem's aura), or 0 when the spell is not a party area
// aura or no unit in view could have cast it.
uint64_t OfPartyAura(uint64_t targetGuid, uint32_t spellId);

} // namespace Aura::AreaCaster
