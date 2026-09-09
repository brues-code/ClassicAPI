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

// `Spell::Learn` — co-hooks the engine's learn / unlearn writers
// (`FUN_LEARN_SPELL` / `FUN_UNLEARN_SPELL`), the two functions that mutate
// the known-spell bitmap. Two duties:
//
// 1. Bump `Player::StatSignal` so caches keyed on spell knowledge (the
//    talent-conversion cache behind GetSpellBonusHealing — Spiritual
//    Guidance / Ironclad) invalidate the moment a respec lands. Same
//    event-driven path aura / equipment changes take; no polling.
// 2. Fire the modern `LEARNED_SPELL_IN_SKILL_LINE(spellID, skillLineIndex,
//    isGuildPerkSpell)` next to the engine's own `LEARNED_SPELL_IN_TAB
//    (tabIndex)`. The engine fires its event only when `notify != 0` AND it
//    appended the spell to the player spellbook array (decompile notes at
//    `Offsets::FUN_LEARN_SPELL`). We reproduce that exact gate by diffing
//    `VAR_PLAYER_SPELLBOOK_COUNT` across the original call, then map the
//    new slot to its tab through `Spell::Tabs`. Login's bulk
//    SMSG_INITIAL_SPELLS passes notify = 0, so neither event fires there.
//
// Both writers run only at login and on learn / unlearn — never per-frame —
// so they are safe co-hook targets.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "player/StatSignal.h"
#include "spell/Lookup.h"
#include "spell/Tabs.h"

#include <cstdint>

namespace Spell::Learn {

namespace {

const Event::Custom::AutoReserve _evtLearned{"LEARNED_SPELL_IN_SKILL_LINE"};

int PlayerBookCount() {
    return Game::Read<int>(Offsets::VAR_PLAYER_SPELLBOOK_COUNT);
}

using LearnSpell_t = void(__fastcall *)(uint32_t spellID, int notify,
                                        uint32_t replacedSpellID);
LearnSpell_t g_origLearnSpell = nullptr;

void __fastcall LearnSpell_h(uint32_t spellID, int notify,
                             uint32_t replacedSpellID) {
    const int before = PlayerBookCount();
    g_origLearnSpell(spellID, notify, replacedSpellID);
    Player::StatSignal::Notify();

    // The engine's LEARNED_SPELL_IN_TAB gate: notified AND added to the
    // book. The original sorts the array before its own fire, so the slot
    // lookup below already sees the final position.
    if (notify == 0 || PlayerBookCount() <= before)
        return;
    int bookType = -1;
    const int slot = Spell::Lookup::FindSpellbookSlot(
        static_cast<int>(spellID), &bookType);
    if (slot <= 0 || bookType != 0)
        return;
    const int tab = Spell::Tabs::IndexForSlot(slot);
    if (tab <= 0)
        return;
    // isGuildPerkSpell is always false: pushed as nil (falsy) through the
    // dispatcher's `%s` + NULL → lua_pushnil path (see Event::Custom::Fire).
    Event::Custom::Fire(_evtLearned.Slot(), "%d%d%s",
                        static_cast<int>(spellID), tab,
                        static_cast<const char *>(nullptr));
}

using UnlearnSpell_t = void(__fastcall *)(uint32_t spellID, int param2);
UnlearnSpell_t g_origUnlearnSpell = nullptr;

void __fastcall UnlearnSpell_h(uint32_t spellID, int param2) {
    g_origUnlearnSpell(spellID, param2);
    Player::StatSignal::Notify();
}

} // namespace

static const Game::HookAutoRegister _hookLearnSpell{
    Offsets::FUN_LEARN_SPELL, reinterpret_cast<void *>(&LearnSpell_h),
    reinterpret_cast<void **>(&g_origLearnSpell)};

static const Game::HookAutoRegister _hookUnlearnSpell{
    Offsets::FUN_UNLEARN_SPELL, reinterpret_cast<void *>(&UnlearnSpell_h),
    reinterpret_cast<void **>(&g_origUnlearnSpell)};

} // namespace Spell::Learn
