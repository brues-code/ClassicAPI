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

// Lua tables in retail's C_DamageMeter shapes (DamageMeterDocumentation.lua),
// built from the meter's totals. Every front end pushes through these, so the
// live meter and the replay return the same tables. Each function pushes one
// table.

#include "damagemeter/Totals.h"

#include <cstdint>

namespace DamageMeter::Tables {

// DamageMeterCombatSession: the view's sources for `type`, in rank order.
void PushCombatSession(void *L, const View &view, int type);

// DamageMeterCombatSessionSource: the spells of the source with `sourceGUID`
// (null or "" for none), else of the one with `sourceCreatureID` (0 for none).
// An empty one when no source matches.
void PushCombatSessionSource(void *L, const View &view, int type, const char *sourceGUID,
                             uint32_t sourceCreatureID);

// DamageMeterCombatSource for one source of `type`'s storage.
void PushCombatSource(void *L, const Source &source, const View &view, int type);

// DamageMeterCombatSpell: the spell, or with `target` its amount against one
// other unit.
void PushCombatSpell(void *L, const Spell &spell, const Target *target, const View &view);

// DamageMeterCombatSpellUnitDetails for `unit` (null: no unit).
void PushSpellUnitDetails(void *L, const MeterUnit *unit, double amount);

// An array of DamageMeterAvailableCombatSession, oldest first.
void PushAvailableSessions(void *L, const Meter &meter, int64_t nowMs);

} // namespace DamageMeter::Tables
