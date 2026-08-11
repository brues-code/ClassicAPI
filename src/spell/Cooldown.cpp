// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// `C_Spell.GetSpellCooldown(spellIdentifier)` — modern table-shape
// cooldown query that accepts spellID, name, name(rank), or
// `|Hspell:N|h` hyperlink, and returns a `SpellCooldownInfo` table
// (or nil for unrecognized spells). Vanilla 1.12 only ships
// `GetSpellCooldown(slot, bookType)`, which forces callers to walk
// the spellbook first and rejects spellIDs not in it (talent
// passives, profession recipes, etc.).
//
// Same `FUN_SPELL_QUERY_COOLDOWN` helper `Spell::Usable` uses for
// the cooldown gate, just exposed through the modern table shape.

#include "Arg.h"
#include "Lookup.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Spell::Cooldown {

namespace {

// The engine writes an *unsigned* millisecond tick into `outStart`.
// Reading it into a signed `int` makes every timestamp negative once
// the machine has been up past 2^31 ms (~24.9 days): a real start of
// 2697363158 ms comes back as -1597604138, and `startTime + duration`
// then lands a month in the past, so callers see every cooldown as
// already elapsed. `Aura::Data` already stores these as `uint32_t`
// for the same reason.
using QueryCooldown_t = void(__fastcall *)(int spellID, int bookType,
                                            uint32_t *outDuration,
                                            uint32_t *outStart,
                                            uint32_t *outEnable);

int __fastcall Script_C_Spell_GetSpellCooldown(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    if (spellID <= 0 || Spell::Lookup::RecordForID(spellID) == nullptr)
        return 0;

    auto fn = reinterpret_cast<QueryCooldown_t>(
        static_cast<uintptr_t>(Offsets::FUN_SPELL_QUERY_COOLDOWN));
    uint32_t durationMs = 0, startMs = 0, enable = 0;
    fn(spellID, 0 /* bookType=player */, &durationMs, &startMs, &enable);

    Game::Lua::NewTable(L);
    // Engine returns ms with the same epoch as `GetTime()` — multiply
    // by 0.001 to match modern's seconds-from-`GetTime` shape, same
    // conversion `Script_GetSpellCooldown` does at the Lua boundary.
    Game::Lua::SetFieldNumber(L, "startTime",
                              static_cast<double>(startMs) * 0.001);
    Game::Lua::SetFieldNumber(L, "duration",
                              static_cast<double>(durationMs) * 0.001);
    Game::Lua::SetFieldBool(L, "isEnabled", enable != 0);
    // Vanilla has no haste-on-cooldown mechanic. Hard-code 1.0 so
    // modern code that divides remaining-time by `modRate` works.
    Game::Lua::SetFieldNumber(L, "modRate", 1.0);
    // `activeCategory` / `timeUntilEndOfStartRecovery` / `isOnGCD`
    // are 11.1.5+ / 12.0+ fields with no vanilla source — leave
    // unset so they read as nil, matching modern's "field present
    // but inapplicable" semantics.
    return 1;
}

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellCooldown",
                                     &Script_C_Spell_GetSpellCooldown);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::Cooldown
