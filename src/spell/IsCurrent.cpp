// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// `C_Spell.IsCurrentSpell(spellIdentifier)` — true if the spell is
// currently being cast, queued mid-GCD, or channeled.
//
// Vanilla maintains three slots we have to check:
//   - `VAR_CURRENT_CAST_SPELL` (`DAT_00CECA88`): the cast-bar spell.
//   - `VAR_QUEUED_CAST_SPELL` (`DAT_00CECAA8`): the spell that got
//     pushed aside mid-GCD when a new cast superseded it; restored
//     to current when the new cast ends.
//   - `OFF_UNIT_FIELD_CHANNEL_SPELL` on the player descriptor:
//     channels (Fishing, Drain Soul, Ritual of Summoning, etc.).
//
// Modern `IsCurrentSpell` is "is this spell on the cast bar / next
// up", and vanilla splits that state across these three slots —
// covering all three matches the documented modern semantics.

#include "Arg.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Spell::IsCurrent {

namespace {

int ReadGlobalSpellID(uintptr_t addr) {
    return *reinterpret_cast<const int *>(addr);
}

int ReadPlayerChannelSpell() {
    auto *player = static_cast<const uint8_t *>(Game::ResolveUnitToken("player"));
    if (player == nullptr)
        return 0;
    auto *desc = *reinterpret_cast<const uint8_t *const *>(
        player + Offsets::OFF_UNIT_DESCRIPTOR);
    if (desc == nullptr)
        return 0;
    return *reinterpret_cast<const int *>(
        desc + Offsets::OFF_UNIT_FIELD_CHANNEL_SPELL);
}

int __fastcall Script_IsCurrentSpell(void *L) {
    const int target = Spell::Arg::ResolveSpellID(L, 1);
    if (target <= 0) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const bool match = ReadGlobalSpellID(Offsets::VAR_CURRENT_CAST_SPELL) == target ||
                       ReadGlobalSpellID(Offsets::VAR_QUEUED_CAST_SPELL) == target ||
                       ReadPlayerChannelSpell() == target;
    Game::Lua::PushBool(L, match);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("spell", "SpellIdentifier", "A spell ID, spell link, or spell name."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("isCurrent", "bool",
                   "True while the spell is on the cast bar, queued, or channeling."),
};
const Game::Doc::Function kIsCurrentSpell{
    "Whether the player is casting, channeling, or has queued this spell.",
    kArgs, kRets};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "IsCurrentSpell",
                                     &Script_IsCurrentSpell, &kIsCurrentSpell);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::IsCurrent
