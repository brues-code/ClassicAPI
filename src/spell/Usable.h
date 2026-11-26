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

namespace Spell::Usable {

// The pet-book branch of the action bar's usable test (FUN_ACTION_SLOT_USABLE,
// 0x004E5050): the pet can act (FUN_PET_ACTIONS_USABLE), then the spell's cost
// against the pet's power of its type (health for -2). `*noMana` is true only
// when the cost is what fails.
bool PetSpell(int spellID, bool *noMana);

} // namespace Spell::Usable
