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

// `CastSpellNoToggle(name | spellID [, unit [, placeGroundSpell]])` —
// spam-safe variant of `CastSpellByName` that won't toggle off an
// already-active self-aura or auto-repeat. The optional second argument is a
// unit token to cast at (issue #22), e.g. `CastSpellNoToggle("Auto Shot",
// "focus")` or `CastSpellNoToggle("Shoot", "targettarget")`: the spell fires
// at that unit without changing your current target, and the third argument
// decides a ground-target spell's aim exactly as `C_Spell.CastAtUnit`'s does.
// When both are omitted the call behaves exactly as before. This is where the
// `/cast !Name` line lands (`Util/SlashCommandsRegistry.lua`), and it covers
// everything that syntax covers in vanilla terms:
//
//   - Auto-repeat: Shoot, Auto-Shot, Wand. Engine tracks via the
//     `VAR_ACTIVE_AUTO_REPEAT_SPELL` global.
//   - Self-aura: shapeshift (Cat/Bear/Travel/Moonkin/Shadowform),
//     stance (Battle/Defensive/Berserker), aspect (Hunter), seal
//     (Paladin), blessing-on-self, etc. Engine tracks via the unit
//     descriptor's aura array; we query through
//     `FUN_SPELL_IS_TOGGLE_AURA_ACTIVE` to ask "would casting this
//     trigger CMSG_CANCEL_AURA instead of CMSG_CAST_SPELL?".
//
// Either signal causes a no-op; otherwise we delegate to the engine's
// `Script_CastSpellByName` to do the actual cast.
//
// **Why no-op and not re-cast for active forms?** 3.3.5's `!` path
// sends CMSG_CAST_SPELL even when the form is active, with a "fresh-
// cast flag" bit (`spellID | 0x1000000`) in the payload — the server
// then re-applies the form, which breaks roots/snares as a stance-
// change side effect. **Vanilla 1.12 servers reject this** ("You are
// in shapeshift form"). Verified empirically against vmangos with a
// direct CMSG_CAST_SPELL bypass: the server refuses duplicate-aura
// casts. So 1.12's safe behavior is to not send the packet at all
// when active.

#include "Game.h"
#include "Offsets.h"
#include "spell/Arg.h"
#include "spell/AtUnit.h"
#include "spell/Lookup.h"
#include "spell/MacroPrimarySpell.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Spell::CastNoToggle {

namespace {


using CastSpellByName_t = int(__fastcall *)(void *L);
using NameToSlot_t = int(__fastcall *)(const char *name, int *outBookType);
using IsToggleAuraActive_t = int(__fastcall *)(unsigned slot, int bookType);

const CastSpellByName_t Script_CastSpellByName_Engine =
    reinterpret_cast<CastSpellByName_t>(Offsets::FUN_SCRIPT_CAST_SPELL_BY_NAME);

int ReadActiveSpellID() {
    return Game::Read<int>(
        static_cast<uintptr_t>(Offsets::VAR_ACTIVE_AUTO_REPEAT_SPELL));
}

const char *LocaleName(int spellID) {
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return nullptr;
    const int locale = Game::Read<int>(
        static_cast<uintptr_t>(Offsets::VAR_LOCALE_INDEX));
    return Game::Read<const char *>(record, Offsets::OFF_SPELL_NAMES + locale * 4);
}

// Case-insensitive name match for the auto-repeat-name gate. Walks
// `userInput` and `spellName` together; on the user side, treats `(`
// and trailing spaces before `(` as end-of-name so `"Shoot(Rank 1)"`
// matches the bare `"Shoot"` we read out of Spell.dbc — mirrors how
// the engine's own name resolver (`FUN_004B3950`) strips the
// parenthetical rank suffix before looking the spell up.
bool NameMatches(const char *userInput, const char *spellName) {
    auto lc = [](unsigned char c) -> unsigned char {
        return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + 32) : c;
    };
    if (userInput == nullptr || spellName == nullptr)
        return false;
    while (*userInput != '\0' && *userInput != '(' && *spellName != '\0') {
        if (lc(static_cast<unsigned char>(*userInput)) !=
            lc(static_cast<unsigned char>(*spellName))) {
            return false;
        }
        ++userInput;
        ++spellName;
    }
    while (*userInput == ' ')
        ++userInput;
    return (*userInput == '\0' || *userInput == '(') && *spellName == '\0';
}

// Resolves `name` to a (slot, bookType) pair via the engine's name
// resolver. Goes through `Spell::NameResolve`'s hook so numeric input
// like `"5019"` is accepted in addition to spell names. Returns -1 on
// resolution failure.
int ResolveSlot(const char *name, int *outBookType) {
    auto fn = reinterpret_cast<NameToSlot_t>(
        static_cast<uintptr_t>(Offsets::FUN_RESOLVE_SPELL_NAME_TO_SLOT));
    *outBookType = 0;
    return fn(name, outBookType);
}

bool IsAuraToggleActive(int slot, int bookType) {
    if (slot < 0)
        return false;
    auto fn = reinterpret_cast<IsToggleAuraActive_t>(
        static_cast<uintptr_t>(Offsets::FUN_SPELL_IS_TOGGLE_AURA_ACTIVE));
    return fn(static_cast<unsigned>(slot), bookType) != 0;
}

// `CastSpellNoToggle(name | spellID [, unit [, placeGroundSpell]])` — see the
// file-header block.
// Returns a boolean indicating whether the requested spell is, at
// function exit, in the "active" state the caller asked for: true
// when we either started the cast or determined the spell was already
// active; false when input was invalid or the cast didn't take effect.
int __fastcall Script_CastSpellNoToggle(void *L) {
    char nameBuf[128];
    int requestedID = 0;

    const int argType = Game::Lua::Type(L, 1);
    if (argType == Game::Lua::TYPE_NUMBER) {
        requestedID = static_cast<int>(Game::Lua::ToNumber(L, 1));
        if (requestedID <= 0) {
            Game::Lua::PushBool(L, false);
            return 1;
        }
        const char *name = LocaleName(requestedID);
        if (name == nullptr || *name == '\0') {
            Game::Lua::PushBool(L, false);
            return 1;
        }
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", name);
    } else if (argType == Game::Lua::TYPE_STRING) {
        const char *s = Game::Lua::ToString(L, 1);
        if (s == nullptr || *s == '\0') {
            Game::Lua::PushBool(L, false);
            return 1;
        }
        // Tolerate the macro `!Name` prefix: this call is exactly what it
        // asks for, so the name behind it is the spell to compare and cast.
        // The engine's resolver drops the prefix too (`Spell::NameResolve`),
        // but the auto-repeat name compare below is ours.
        const char *p = s;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (*p == '!') {
            ++p;
            while (*p == ' ' || *p == '\t')
                ++p;
            if (*p != '\0')
                s = p;
        }
        // Copy off Lua's string heap — the SetTop+PushString below
        // could shift it and invalidate `s`.
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", s);
    } else {
        Game::Lua::Error(
            L,
            "Usage: CastSpellNoToggle(\"name\" | spellID [, \"unit\" [, placeGroundSpell]])");
        return 0;
    }

    // Optional arg2 — a unit token to cast at (issue #22), e.g.
    // CastSpellNoToggle("Auto Shot", "focus"). Copy it off Lua's string
    // heap now, before the toggle gates and the SetTop below can shift the
    // stack. A non-string arg2 (nil / absent) leaves unitToken null and the
    // call keeps its original single-arg behavior.
    char unitBuf[64];
    const char *unitToken = nullptr;
    if (Game::Lua::Type(L, 2) == Game::Lua::TYPE_STRING) {
        const char *u = Game::Lua::ToString(L, 2);
        if (u != nullptr && *u != '\0') {
            std::snprintf(unitBuf, sizeof(unitBuf), "%s", u);
            unitToken = unitBuf;
        }
    }

    // Optional arg3 — the ground-target rule, read exactly as
    // `C_Spell.CastAtUnit` reads its own third argument: omitted or nil means
    // place a ground spell at the unit, an explicit false leaves the reticle
    // up. `lua_type` reports NONE, not nil, past the top of the stack, so test
    // the top rather than the type alone.
    const bool placeGroundSpell = Game::Lua::GetTop(L) < 3 ||
                                  Game::Lua::Type(L, 3) == Game::Lua::TYPE_NIL ||
                                  Game::Lua::ToBoolean(L, 3) != 0;

    // Check 1 — auto-repeat (Shoot / Auto-Shot / Wand). Cheap global
    // read; the spell-cast subsystem owns this state separately from
    // the aura system.
    const int activeAutoRepeat = ReadActiveSpellID();
    if (activeAutoRepeat != 0) {
        const bool match = (requestedID > 0)
            ? (activeAutoRepeat == requestedID)
            : NameMatches(nameBuf, LocaleName(activeAutoRepeat));
        if (match) {
            Game::Lua::PushBool(L, true); // already auto-repeating — spam-safe
            return 1;
        }
        // A different auto-repeat is active. Don't disrupt it — silently
        // refuse.
        Game::Lua::PushBool(L, false);
        return 1;
    }

    // Check 2 — self-aura (shapeshift / stance / aspect / blessing /
    // seal / etc.). Resolve name → slot, then ask the engine.
    int bookType = 0;
    const int slot = ResolveSlot(nameBuf, &bookType);
    if (IsAuraToggleActive(slot, bookType)) {
        Game::Lua::PushBool(L, true); // already active — no-op
        return 1;
    }

    // Neither toggle would fire — safe to cast.
    if (unitToken != nullptr) {
        // Cast at the given unit without disturbing the current target —
        // reuses AtUnit's resolve→GUID→dispatch core. A unit-target /
        // auto-repeat spell fires straight at the unit; a ground spell lands
        // at its feet unless the caller said not to. Casts by name so numeric
        // and string input match the no-unit path (highest known rank). A
        // genuinely unknown token raises the engine's standard "Unknown unit"
        // error, same as UnitHealth.
        Spell::AtUnit::CastByName(nameBuf, unitToken, placeGroundSpell);
    } else {
        // Delegate to the engine with a fresh, one-arg stack so
        // `lua_toboolean(L, 2)` (the `onSelf` flag inside
        // Script_CastSpellByName) sees nil → false.
        Game::Lua::SetTop(L, 0);
        Game::Lua::PushString(L, nameBuf);
        Script_CastSpellByName_Engine(L);
    }

    // Reflect the post-cast state: true if either toggle is now active.
    const bool autoRepeatNow = ReadActiveSpellID() != 0;
    const bool auraNow = !autoRepeatNow && slot >= 0
        ? IsAuraToggleActive(slot, bookType)
        : false;
    Game::Lua::PushBool(L, autoRepeatNow || auraNow);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("CastSpellNoToggle",
                                      &Script_CastSpellNoToggle);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// ---------------------------------------------------------------------------
// Macro parser pattern
//
// Tags macros containing `CastSpellNoToggle("Name")` with the
// resolved spellID, so `IsAutoRepeatAction(macroSlot)` reports
// correctly and action-bar UIs (pfUI etc.) highlight the slot for
// auto-repeat spells. `Spell::MacroPrimarySpell` owns the parser
// hook and the line walk; we just declare our `(prefix, extractor)`.
// ---------------------------------------------------------------------------

int ExtractCastSpellNoToggleArg(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\t'))
        ++p;
    if (p >= end || *p != '"')
        return 0;
    ++p;
    const char *q = p;
    while (q < end && *q != '"')
        ++q;
    if (q >= end)
        return 0;
    const size_t n = static_cast<size_t>(q - p);
    if (n == 0 || n >= 128)
        return 0;
    char name[128];
    std::memcpy(name, p, n);
    name[n] = '\0';
    return Spell::Arg::NameToSpellID(name);
}

const Spell::MacroPrimarySpell::PatternAutoRegister _patreg{
    "CastSpellNoToggle(", &ExtractCastSpellNoToggleArg};

} // namespace

} // namespace Spell::CastNoToggle
