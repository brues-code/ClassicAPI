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

// Client-side totem-slot tracker backing `GetTotemInfo(slot)`. Vanilla
// 1.12 has no totem-bar / PLAYER_TOTEM_UPDATE machinery (all TBC
// additions), so we track the four element slots ourselves. See
// Tracker.cpp for the design.
namespace Totem::Tracker {

// Number of totem slots (Fire, Earth, Water, Air), i.e. the valid `totemN`
// range is `1..kSlotCount`.
constexpr int kSlotCount = 4;

// GUID of the totem creature in slot `slot` (`1..kSlotCount`), or `0` when
// the slot is empty, its creature hasn't been seen yet, or `slot` is out of
// range. Consumed by the `totemN` branch of the token resolver in
// `unit/TokenExtensions.cpp` and by `Unit::TokenEvents`, mirroring
// `Unit::RaidTarget::GetGUIDByMark`.
uint64_t GetGUIDBySlot(int slot);

// Notify of a spell the LOCAL PLAYER just cast (from Aura::Source's
// SpellGo hook — the totem summon doesn't apply an aura, so it must be
// fed before that hook's aura gate). If `spellID` is a totem summon
// (`Spell.dbc` effect 87-90 = SUMMON_TOTEM_SLOT1..4), it's recorded in the
// matching element slot; otherwise ignored.
void OnPlayerSpellGo(uint32_t spellID);

} // namespace Totem::Tracker
