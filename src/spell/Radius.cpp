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

// `C_Spell.GetSpellRadius(spellID)` and the parallel
// `GetSpellRadius(slot, bookType)` -> (baseRadius, modifiedRadius)
//
// Returns the spell's AOE radius in yards, or nil for a non-AOE spell
// (no effect carries a radius). Two values:
//   - baseRadius     — the raw SpellRadius.dbc value (caster-independent,
//                      what an AOE tracker watching other units wants)
//   - modifiedRadius — baseRadius with the LOCAL PLAYER's spell modifiers
//                      (talents / items, e.g. a Mage's Arctic Reach)
//                      applied. Equal to baseRadius when the player has
//                      no matching modifier.
//
// Two signatures mirroring the other dual-signature spell globals
// (`GetSpellInfo`, `SpellHasRange`, etc.): the namespaced form takes a
// modern spell *identifier* (numeric ID or name), the bare global takes
// the vanilla `(slot, bookType)` spellbook position. `bookType` is the
// `GetSpellName` convention: `"pet"` → pet book, anything else (or
// omitted) → player book.
//
// baseRadius reads `Spell.dbc`'s per-effect `EffectRadiusIndex[3]` and
// resolves each into `SpellRadius.dbc`, taking the largest base radius
// across the three effects. No caster-level scaling (the engine's
// FUN_006e6350 also adds `radiusPerLevel * casterLevel`, which needs a
// unit context; modern `GetSpellRadius` reports the base value).
//
// modifiedRadius applies the local player's SpellMods for op 6 (radius)
// via `Spell::Mod::Apply`. It is inherently local-player-only — the
// client tracks spell mods for the player alone, so there's no way to
// know another caster's talents.
//
// All DBCs / mod tables are resident from boot, so the lookup is
// synchronous with no caching.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "spell/Arg.h"
#include "spell/Lookup.h"
#include "spell/Mod.h"

#include <cstdint>
#include <cstring>

namespace Spell::Radius {

// Largest base radius across the spell's effects, or a negative sentinel
// (< 0) if no effect has a radius.
static float MaxBaseRadius(const uint8_t *record) {
    float best = -1.0f;
    for (int i = 0; i < Offsets::SPELL_RECORD_EFFECT_COUNT; ++i) {
        const int radiusIndex = Game::Read<int>(
            record, Offsets::OFF_SPELL_RECORD_EFFECT_RADIUS_INDEX + i * 4);
        const uint8_t *rad = DBC::Record(Offsets::VAR_SPELL_RADIUS_RECORDS,
                                         Offsets::VAR_SPELL_RADIUS_COUNT,
                                         static_cast<uint32_t>(radiusIndex));
        if (rad == nullptr)
            continue;
        const float r = Game::Read<float>(rad, Offsets::OFF_SPELL_RADIUS_VALUE);
        if (r > best)
            best = r;
    }
    return best;
}

// Pushes (baseRadius, modifiedRadius) for `spellID`, or nil (0 returns)
// for an invalid / non-AOE spell.
static int PushRadiusForSpellID(void *L, int spellID) {
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return 0; // nil
    const float base = MaxBaseRadius(record);
    if (base < 0.0f)
        return 0; // nil — not an AOE spell
    Game::Lua::PushNumber(L, static_cast<double>(base));
    Game::Lua::PushNumber(L,
        static_cast<double>(Spell::Mod::Apply(record, Offsets::SPELLMOD_OP_RADIUS, base)));
    return 2;
}

// `C_Spell.GetSpellRadius(spellID)` — modern spell-identifier form.
static int __fastcall Script_C_Spell_GetSpellRadius(void *L) {
    return PushRadiusForSpellID(L, Spell::Arg::ResolveSpellID(L, 1));
}

// `GetSpellRadius(slot, bookType)` — vanilla-style positional shape.
static int __fastcall Script_GetSpellRadius(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0; // nil
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1));
    int bookType = 0;
    if (Game::Lua::IsString(L, 2)) {
        const char *s = Game::Lua::ToString(L, 2);
        if (s != nullptr && _stricmp(s, "pet") == 0)
            bookType = 1;
    }
    return PushRadiusForSpellID(L, Spell::Lookup::SpellbookSlotToID(slot, bookType));
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kRadiusRets[] = {
    Game::Doc::Opt("baseRadius", "number", nullptr,
                   "The radius in yards for any caster; nil when the spell has no area."),
    Game::Doc::Opt("modifiedRadius", "number", nullptr,
                   "The radius with the local player's talent and item changes applied."),
};

const Game::Doc::Field kByIdentifierArgs[] = {
    Game::Doc::Req("spell", "SpellIdentifier", "A spell ID, spell link, or spell name."),
};
const Game::Doc::Function kC_SpellGetSpellRadius{
    "The area a spell covers in yards, before and after the player's own changes to it.",
    kByIdentifierArgs, kRadiusRets};

const Game::Doc::Field kBySlotArgs[] = {
    Game::Doc::Req("slot", "luaIndex", "A 1-based spellbook slot."),
    Game::Doc::Opt("bookType", "string", "\"spell\"",
                   "\"spell\" or \"pet\"; picks the book the slot belongs to."),
};
const Game::Doc::Function kGetSpellRadius{
    "The area the spell in a spellbook slot covers in yards, before and after the "
    "player's own changes to it.",
    kBySlotArgs, kRadiusRets, "SpellGlobals"};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellRadius",
                                     &Script_C_Spell_GetSpellRadius,
                                     &kC_SpellGetSpellRadius);
    Game::Lua::RegisterGlobalFunction("GetSpellRadius", &Script_GetSpellRadius,
                                      &kGetSpellRadius);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::Radius
