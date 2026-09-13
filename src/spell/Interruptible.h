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

// `Spell::Interruptible` — the `notInterruptible` field of `UnitCastingInfo` /
// `UnitChannelInfo`: can nothing the LOCAL PLAYER knows stop this cast? The
// 3.3.5 client's own predicate, run on 1.12 data — see Interruptible.cpp.
namespace Spell::Interruptible {

// True when no interrupt or silence the local player knows can stop
// `spellRecord`, cast (or channeled, per `isChannel`) by `caster` — a CGUnit
// pointer, or null when the caster's auras aren't readable (the static and
// capability parts still apply). False for a null record, and ALWAYS false
// while the player knows no interrupt or silence at all (the 3.3.5 reader's
// rule: the flag only exists for someone who could act on it).
bool NotInterruptible(const uint8_t *caster, const uint8_t *spellRecord,
                      bool isChannel);

} // namespace Spell::Interruptible
