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

// What every front end of the damage meter fills a MeterUnit with the same
// way: the live meter (Live.cpp) from the objects in view, the replay
// (replay/Meter.cpp) from a recording.

#include "damagemeter/Totals.h"

#include <cstdint>

namespace DamageMeter {

// The spell a melee swing is shown as: Attack, the client's own auto attack
// spell (Spell.dbc 6603, "Attack" in 1.12's table), as retail shows swings.
constexpr uint32_t kMeleeSpellID = 6603;

// The key of a creature source: one creature ID is one source (our choice:
// C_DamageMeter's own lookup takes a creature ID). Never equal to a player's
// or pet's GUID, nor to a small index.
uint64_t CreatureKey(uint32_t creatureID);

// UnitClassification's word for a creature ID: the engine's table indexed by
// the creature cache record's rank (FUN_00605620's rule); "normal" for 0 and
// for a creature with no record.
const char *Classification(uint32_t creatureID);

// A player's class token ("MAGE") and faction group ("Alliance") from its
// ChrClasses and ChrRaces IDs; either is left empty when unknown.
void SetPlayerDetails(MeterUnit &unit, uint32_t classID, uint32_t raceID);

} // namespace DamageMeter
