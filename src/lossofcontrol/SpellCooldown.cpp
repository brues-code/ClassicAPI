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

// `C_Spell.GetSpellLossOfControlCooldown(spellIdentifier)` ->
//     startTime, duration
// `C_SpellBook.GetSpellBookItemLossOfControlCooldownInfo(slotIndex,
//     spellBank)` -> SpellLossOfControlInfo | nil
// `C_SpellBook.GetSpellBookItemLossOfControlCooldownDuration(slotIndex,
//     spellBank)` -> duration | nil
//
// The per-spell view of `C_LossOfControl`: the control-loss effect that
// stops THIS spell, shaped as a cooldown (`GetTime()`-epoch seconds) so an
// action button can draw the loss-of-control swipe over it.
// `LossOfControl::LockoutForSpell` decides which effects block which spells;
// this file only converts ticks and builds the Lua shapes.
//
// An effect whose end is known but whose start is not (a CC aura seen only
// in the aura array) is reported as starting now and lasting the remaining
// time — the same swipe a button would draw, just without the elapsed part.

#include "Lockout.h"

#include "Game.h"
#include "spell/Arg.h"
#include "spell/Cooldown.h"
#include "spell/Lookup.h"
#include "time/Clock.h"

#include <cstdint>

namespace LossOfControl::SpellCooldown {

namespace {

using Time::Clock::NowMs;

struct Lockout {
    bool active;
    uint32_t startMs;
    uint32_t endMs;
};

Lockout ForSpell(int spellID) {
    Lockout lock{false, 0, 0};
    if (!LockoutForSpell(spellID, &lock.startMs, &lock.endMs))
        return lock;
    lock.active = true;
    if (lock.startMs == 0)
        lock.startMs = NowMs(); // end known, start not: swipe from now
    return lock;
}

double StartSeconds(const Lockout &lock) {
    return static_cast<double>(lock.startMs) * 0.001;
}

double DurationSeconds(const Lockout &lock) {
    if (!lock.active)
        return 0.0;
    return static_cast<double>(Time::Clock::Elapsed(lock.startMs, lock.endMs)) *
           0.001;
}

// True when the lockout outlasts the spell's own cooldown — the case where
// a button should draw the loss-of-control swipe instead of the normal one.
bool OutlastsNormalCooldown(int spellID, const Lockout &lock) {
    if (!lock.active)
        return false;
    uint32_t cdStart = 0, cdDuration = 0;
    bool enabled = false;
    if (!Spell::Cooldown::Query(spellID, &cdStart, &cdDuration, &enabled) ||
        cdDuration == 0)
        return true;
    const uint32_t now = NowMs();
    return Time::Clock::Remaining(now, lock.endMs) >
           Time::Clock::Remaining(now, cdStart + cdDuration);
}

int __fastcall Script_C_Spell_GetSpellLossOfControlCooldown(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    if (Spell::Lookup::RecordForID(spellID) == nullptr)
        return 0;
    const Lockout lock = ForSpell(spellID);
    Game::Lua::PushNumber(L, lock.active ? StartSeconds(lock) : 0.0);
    Game::Lua::PushNumber(L, DurationSeconds(lock));
    return 2;
}

int __fastcall Script_GetSpellBookItemLossOfControlCooldownInfo(void *L) {
    const int spellID = Spell::Lookup::SpellbookItemArgsToID(L, 1, 2);
    if (spellID <= 0)
        return 0; // empty / out-of-range slot -> nil
    const Lockout lock = ForSpell(spellID);
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "startTime",
                              lock.active ? StartSeconds(lock) : 0.0);
    Game::Lua::SetFieldNumber(L, "duration", DurationSeconds(lock));
    Game::Lua::SetFieldNumber(L, "modRate", 1.0);
    Game::Lua::SetFieldBool(L, "isActive", lock.active);
    Game::Lua::SetFieldBool(L, "shouldReplaceNormalCooldown",
                            OutlastsNormalCooldown(spellID, lock));
    return 1;
}

int __fastcall Script_GetSpellBookItemLossOfControlCooldownDuration(void *L) {
    const int spellID = Spell::Lookup::SpellbookItemArgsToID(L, 1, 2);
    if (spellID <= 0)
        return 0;
    Game::Lua::PushNumber(L, DurationSeconds(ForSpell(spellID)));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction(
        "C_Spell", "GetSpellLossOfControlCooldown",
        &Script_C_Spell_GetSpellLossOfControlCooldown);
    Game::Lua::RegisterTableFunction(
        "C_SpellBook", "GetSpellBookItemLossOfControlCooldownInfo",
        &Script_GetSpellBookItemLossOfControlCooldownInfo);
    Game::Lua::RegisterTableFunction(
        "C_SpellBook", "GetSpellBookItemLossOfControlCooldownDuration",
        &Script_GetSpellBookItemLossOfControlCooldownDuration);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace LossOfControl::SpellCooldown
