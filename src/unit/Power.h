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

// Two numberings meet here. A 1.12 power SLOT (0..4: mana, rage, focus, energy,
// happiness) is the unit's POWER1..5 field, the byte UnitPowerType reads, and
// the power type of a spell, energize or drain. A modern power TYPE is
// Enum.PowerType as the 3.80.2 client numbers it: slots 0..3 keep their
// numbers, happiness is 27, and 4 is combo points, which 1.12 keeps on the
// player instead of in a slot. Everything surfaced to Lua is a type.
namespace Unit::Power {

constexpr int kTypeComboPoints = 4;
constexpr int kTypeHappiness = 27;
constexpr int kTypeCount = 30; // 3.80.2 accepts types 0..29

// The modern type of a 1.12 slot (4 → 27); other values pass through, so a
// spell's -2 (health) stays -2.
int TypeOfSlot(int slot);

// The 1.12 slot of a modern type (27 → 4, 0..3 → themselves), or -1 for a
// type no slot holds (combo points included).
int SlotOfType(int type);

// The token of a modern type ("MANA", "COMBO_POINTS", "HAPPINESS", …), as the
// UNIT_POWER_* events, UnitPowerType and spell power costs carry it; "UNKNOWN"
// for a type 1.12 never produces.
const char *TypeToken(int type);

// The engine's divisor from a stored power amount to displayed units
// (`FUN_POWER_DISPLAY_DIVISOR`) for a 1.12 SLOT: 10 for rage, 1000 for
// happiness, 1 otherwise. Every power amount surfaced to Lua — unit power,
// spell costs, combat-log amounts — goes through this.
unsigned int DisplayDivisor(int slot);

// A unit's stored power and max power of `slot` (0..4), from its descriptor
// block (`Object::Fields`): UNIT_FIELD_POWER1 / MAXPOWER1 + slot * 4. Stored
// means before DisplayDivisor: rage in tenths, happiness at 1000x.
uint32_t Stored(const uint8_t *fields, int slot);
uint32_t StoredMax(const uint8_t *fields, int slot);

} // namespace Unit::Power
