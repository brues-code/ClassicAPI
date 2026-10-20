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

#include "spell/Known.h"

#include "Game.h"
#include "Offsets.h"

namespace Spell::Known {

bool IsKnown(uint32_t spellID) {
    if (spellID == 0)
        return false;
    const uint32_t spellCount =
        Game::Read<uint32_t>(static_cast<uintptr_t>(Offsets::VAR_SPELL_RECORD_COUNT));
    if (spellID > spellCount)
        return false;
    auto *bitmap =
        Game::Read<const uint32_t *>(static_cast<uintptr_t>(Offsets::VAR_PLAYER_SPELL_BITMAP));
    if (bitmap == nullptr)
        return false;
    return (bitmap[spellID >> 5] & (1u << (spellID & 31))) != 0;
}

// The server's `Player::m_canDualWield` is set only by SPELL_EFFECT_DUAL_WIELD
// (effect 40), and the client Spell.dbc has exactly one spell with that
// effect: 674 "Dual Wield", the warrior / rogue / hunter passive.
//
// Do NOT use the engine's own `CGPlayer_C::CanDualWield` (0x005EAB70, the
// 4.3.4 `s_dualWieldSpell != 0`). The learn path stores an effect-40 spell in
// the global 0x00C4D770 and only the unlearn of that same spell clears it;
// nothing clears it at logout. So after logging out of a character that
// knows Dual Wield, every later character in the session reads true. Seen in
// game: true on a character with IsPlayerSpell(674) false, false on the same
// character after a fresh client start. The bitmap is per character.
bool CanDualWield() {
    constexpr uint32_t kDualWieldSpellID = 674;
    return IsKnown(kDualWieldSpellID);
}

} // namespace Spell::Known
