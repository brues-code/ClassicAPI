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

#include "Cooldown.h"

#include "Arg.h"
#include "Lookup.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Spell::Cooldown {

namespace {

// The engine writes *unsigned* millisecond ticks into `outStart`/`outDuration`
// (same OS counter, same epoch as `GetTime()`). Reading `outStart` into a
// signed `int` makes every timestamp negative once the machine has been up
// past 2^31 ms (~24.9 days) — `startTime + duration` then lands a month in the
// past and callers see every cooldown as already elapsed. Keep them unsigned;
// see `Time::Clock` for the full rationale.
using QueryCooldown_t = void(__fastcall *)(int spellID, int pet,
                                            uint32_t *outDuration,
                                            uint32_t *outStart,
                                            uint32_t *outEnable);

// Whether the engine's reported cooldown came from the history's global leg:
// the reader keeps the latest-ending leg, and only a global leg pairs a
// record's `+0x10` start with its GCD time, so a live global leg carrying
// exactly the reported start and duration is the one it kept. The match mirrors
// the leg's own test (category equal, including 0; GCD time nonzero).
bool ReportsGlobalCooldown(int spellID, bool pet, uint32_t startMs,
                           uint32_t durationMs) {
    if (durationMs == 0)
        return false;
    const auto *spellRec = Spell::Lookup::RecordForID(spellID);
    const uint32_t category = Game::Read<uint32_t>(
        spellRec, Offsets::OFF_SPELL_RECORD_START_RECOVERY_CATEGORY);
    const uintptr_t list = Offsets::VAR_SPELL_HISTORY +
                           (pet ? Offsets::SPELL_HISTORY_STRIDE : 0);
    for (uintptr_t rec = Game::Read<uintptr_t>(list + Offsets::OFF_SPELL_HISTORY_HEAD);
         rec != 0 && (rec & 1) == 0;
         rec = Game::Read<uintptr_t>(rec + Offsets::OFF_SPELL_HISTORY_RECORD_NEXT)) {
        if (Game::Read<uint32_t>(rec + Offsets::OFF_SPELL_HISTORY_RECORD_GCD_CATEGORY) == category &&
            Game::Read<uint32_t>(rec + Offsets::OFF_SPELL_HISTORY_RECORD_GCD_TIME) == durationMs &&
            Game::Read<uint32_t>(rec + Offsets::OFF_SPELL_HISTORY_RECORD_START) == startMs)
            return true;
    }
    return false;
}

int __fastcall Script_C_Spell_GetSpellCooldown(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    int petBook = 0;
    Spell::Lookup::FindSpellbookSlot(spellID, &petBook);
    const bool pet = petBook != 0;
    uint32_t durationMs = 0, startMs = 0;
    bool enabled = false;
    if (!Query(spellID, pet, &startMs, &durationMs, &enabled))
        return 0;

    Game::Lua::NewTable(L);
    // Engine returns ms with the same epoch as `GetTime()` — multiply
    // by 0.001 to match modern's seconds-from-`GetTime` shape, same
    // conversion `Script_GetSpellCooldown` does at the Lua boundary.
    Game::Lua::SetFieldNumber(L, "startTime",
                              static_cast<double>(startMs) * 0.001);
    Game::Lua::SetFieldNumber(L, "duration",
                              static_cast<double>(durationMs) * 0.001);
    Game::Lua::SetFieldBool(L, "isEnabled", enabled);
    // Vanilla has no haste-on-cooldown mechanic. Hard-code 1.0 so
    // modern code that divides remaining-time by `modRate` works.
    Game::Lua::SetFieldNumber(L, "modRate", 1.0);
    Game::Lua::SetFieldBool(L, "isOnGCD",
                            ReportsGlobalCooldown(spellID, pet, startMs, durationMs));
    // `activeCategory` / `timeUntilEndOfStartRecovery` are left unset so
    // they read as nil.
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kSpellCooldownInfoFields[] = {
    Game::Doc::Req("startTime", "number",
                   "When the cooldown began, in seconds on the GetTime clock; 0 when none runs."),
    Game::Doc::Req("duration", "number", "Cooldown length in seconds; 0 when none runs."),
    Game::Doc::Req("isEnabled", "bool", "False while the cooldown is on hold."),
    Game::Doc::Req("modRate", "number", "Always 1."),
    Game::Doc::Req("isOnGCD", "bool",
                   "True when the reported cooldown is the global cooldown."),
};
const Game::Doc::Structure kSpellCooldownInfo{
    "SpellCooldownInfo", "Spell", kSpellCooldownInfoFields,
    "Cooldown state for one spell."};

const Game::Doc::Field kGetSpellCooldownArgs[] = {
    Game::Doc::Req("spell", "SpellIdentifier", "A spell ID, spell link, or spell name."),
};
const Game::Doc::Field kGetSpellCooldownRets[] = {
    Game::Doc::Opt("cooldownInfo", "SpellCooldownInfo", nullptr,
                   "Nil when the spell cannot be resolved."),
};
const Game::Doc::Function kGetSpellCooldown{
    "When a spell's cooldown began and how long it lasts.",
    kGetSpellCooldownArgs, kGetSpellCooldownRets};

} // namespace

bool Query(int spellID, uint32_t *startMs, uint32_t *durationMs,
           bool *enabled) {
    int pet = 0;
    Spell::Lookup::FindSpellbookSlot(spellID, &pet);
    return Query(spellID, pet != 0, startMs, durationMs, enabled);
}

bool Query(int spellID, bool pet, uint32_t *startMs, uint32_t *durationMs,
           bool *enabled) {
    if (spellID <= 0 || Spell::Lookup::RecordForID(spellID) == nullptr)
        return false;
    auto fn = reinterpret_cast<QueryCooldown_t>(
        static_cast<uintptr_t>(Offsets::FUN_SPELL_QUERY_COOLDOWN));
    uint32_t duration = 0, start = 0, enable = 0;
    fn(spellID, pet, &duration, &start, &enable);
    *startMs = start;
    *durationMs = duration;
    *enabled = enable != 0;
    return true;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellCooldown",
                                     &Script_C_Spell_GetSpellCooldown,
                                     &kGetSpellCooldown);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::Cooldown
