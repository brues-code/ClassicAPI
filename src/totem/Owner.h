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

// Who an aura from a totem belongs to. Aura::Source attributes every aura a
// totem applies to the totem's owner, whoever's totem it is (a totem's party
// area aura through Aura::AreaCaster). See Owner.cpp.
namespace Totem::Owner {

// The unit that dropped `guid` when `guid` is a visible totem, else 0.
uint64_t OfTotem(uint64_t guid);

} // namespace Totem::Owner
