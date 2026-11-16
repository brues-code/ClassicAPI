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

// The engine's unit relation tests, `__thiscall(unit, other)` with `ret 4`:
// what the targeting, range and color code asks of two units. No range or
// line-of-sight check in any of them.

#include "Offsets.h"

namespace Unit::Relation {

namespace Detail {
template <class R> R Call(uintptr_t fn, const void *unit, const void *other) {
    using Fn = R(__thiscall *)(const void *, const void *);
    return reinterpret_cast<Fn>(fn)(unit, other);
}
} // namespace Detail

// FUN_UNIT_CAN_ATTACK: hostility, PvP flags and attackability.
inline bool CanAttack(const void *unit, const void *other) {
    return Detail::Call<bool>(Offsets::FUN_UNIT_CAN_ATTACK, unit, other);
}

// FUN_UNIT_CAN_ASSIST: whether `unit` may cast a helpful spell on `other`.
inline bool CanAssist(const void *unit, const void *other) {
    return Detail::Call<bool>(Offsets::FUN_UNIT_CAN_ASSIST, unit, other);
}

// FUN_UNIT_CAN_ASSIST_CORPSE: the same for a corpse object.
inline bool CanAssistCorpse(const void *unit, const void *corpse) {
    return Detail::Call<bool>(Offsets::FUN_UNIT_CAN_ASSIST_CORPSE, unit, corpse);
}

// FUN_UNIT_IN_SAME_PARTY / FUN_UNIT_IN_SAME_RAID: the two units share the
// local player's party, or raid.
inline bool InSameParty(const void *unit, const void *other) {
    return Detail::Call<bool>(Offsets::FUN_UNIT_IN_SAME_PARTY, unit, other);
}
inline bool InSameRaid(const void *unit, const void *other) {
    return Detail::Call<bool>(Offsets::FUN_UNIT_IN_SAME_RAID, unit, other);
}

// FUN_UNIT_CAN_COOPERATE: neither unit charmed, and the faction groups agree
// (SetRaidTarget's player test).
inline bool CanCooperate(const void *unit, const void *other) {
    return Detail::Call<bool>(Offsets::FUN_UNIT_CAN_COOPERATE, unit, other);
}

// FUN_UNIT_REACTION: CGUnit_C::UnitReaction, 0..7; above 3 is friendly
// (Script_UnitIsFriend).
inline int Reaction(const void *unit, const void *other) {
    return Detail::Call<int>(Offsets::FUN_UNIT_REACTION, unit, other);
}

} // namespace Unit::Relation
