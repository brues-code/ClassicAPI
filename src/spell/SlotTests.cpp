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

// `IsAttackSpell(slot, bookType)`, `IsAutoRepeatSpell(slot, bookType)` and
// `IsConsumableSpell(spellID | slot, bookType)` — 4.3.4's spellbook tests
// (Script_IsAttackSpell 0x00915F60, Script_IsAutoRepeatSpell 0x009160A0,
// Script_IsConsumableSpell 0x00916490).
//
// All three read their slot through 4.3.4's GetSlotFromLua with names
// allowed; `ReadSlot` below is that helper. Its (slot, bookType) branch is
// 1.12's own reader (FUN_READ_BOOK_SLOT_ARGS); its name branch is 1.12's
// name resolver, which takes the rank as a "(Rank N)" suffix, so a separate
// rank argument is folded into that form. A name not in the book returns
// nothing — and so does a bad slot number, since Lua's isstring accepts
// numbers and it falls to the name branch, as in 4.3.4. Only a first
// argument that is neither raises "<fn>(): Invalid spell slot".
//
// IsAttackSpell: effect 0 is SPELL_EFFECT_ATTACK — the same test 1.12's
// spellbook texture getter (0x004B3F8A) makes.
// IsAutoRepeatSpell: a player-book spell equal to the auto-repeat spell —
// 1.12's IsAutoRepeatAction helper (0x004E55B0). 4.3.4 also accepts its
// separate auto-ranged-combat spell, which 1.12 folds into the same one.
// IsConsumableSpell: any reagent with a positive count.

#include "Lookup.h"

#include "Game.h"
#include "Offsets.h"
#include "unit/TokenResolve.h"

#include <cstdint>
#include <cstdio>

namespace Spell::SlotTests {

namespace {

constexpr int kSpellEffectAttack = 78;

using ReadBookSlotArgs_t = int(__fastcall *)(void *L, int *slot, int *pet);
using NameToSlot_t = int(__fastcall *)(const char *name, int *outBookType);

// The spellID in the slot named by stack[1..2], or 0 for an empty slot.
// Returns false with nothing pushed when the caller must return 0 results.
bool ReadSlot(void *L, const char *function, int *spellID, int *bookType) {
    int slot = 0;
    *bookType = 0;
    // The reader fails a non-number stack[1] on its first test; gating it here
    // keeps names away from nampower's detour on it, which resolves a string
    // itself and raises for one not in the book.
    if (Game::Lua::IsNumber(L, 1) &&
        reinterpret_cast<ReadBookSlotArgs_t>(Offsets::FUN_READ_BOOK_SLOT_ARGS)(L, &slot,
                                                                              bookType)) {
        *spellID = Spell::Lookup::SpellbookSlotToID(slot + 1, *bookType);
        return true;
    }
    if (Game::Lua::IsString(L, 1)) {
        const char *name = Game::Lua::ToString(L, 1);
        char ranked[512];
        if (Game::Lua::IsString(L, 2) &&
            !Unit::TokenResolve::IsUnitToken(Game::Lua::ToString(L, 2))) {
            std::snprintf(ranked, sizeof(ranked), "%s(%s)", name, Game::Lua::ToString(L, 2));
            name = ranked;
        }
        slot = reinterpret_cast<NameToSlot_t>(Offsets::FUN_RESOLVE_SPELL_NAME_TO_SLOT)(name,
                                                                                   bookType);
        if (slot < 0)
            return false;
        *spellID = Spell::Lookup::SpellbookSlotToID(slot + 1, *bookType);
        return true;
    }
    Game::Lua::Error(L, "%s(): Invalid spell slot", function);
    return false;
}

int __fastcall Script_IsAttackSpell(void *L) {
    int spellID, bookType;
    if (!ReadSlot(L, "IsAttackSpell", &spellID, &bookType))
        return 0;
    const uint8_t *spell = Spell::Lookup::RecordForID(spellID);
    Game::Lua::PushBool(L, spell != nullptr &&
                               Game::Read<int32_t>(spell, Offsets::OFF_SPELL_RECORD_EFFECT) ==
                                   kSpellEffectAttack);
    return 1;
}

int __fastcall Script_IsAutoRepeatSpell(void *L) {
    int spellID, bookType;
    if (!ReadSlot(L, "IsAutoRepeatSpell", &spellID, &bookType))
        return 0;
    Game::Lua::PushBool(L, spellID != 0 && bookType == 0 &&
                               Game::Read<int32_t>(Offsets::VAR_ACTIVE_AUTO_REPEAT_SPELL) ==
                                   spellID);
    return 1;
}

int __fastcall Script_IsConsumableSpell(void *L) {
    int spellID, bookType;
    if (Game::Lua::IsNumber(L, 1) && Game::Lua::GetTop(L) == 1)
        spellID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    else if (!ReadSlot(L, "IsConsumableSpell", &spellID, &bookType))
        return 0;

    bool consumable = false;
    if (const uint8_t *spell = Spell::Lookup::RecordForID(spellID)) {
        for (int i = 0; i < Offsets::SPELL_MAX_REAGENTS && !consumable; ++i)
            consumable =
                Game::Read<int32_t>(spell, Offsets::OFF_SPELL_REAGENT_ID + i * 4) != 0 &&
                Game::Read<int32_t>(spell, Offsets::OFF_SPELL_REAGENT_COUNT + i * 4) > 0;
    }
    Game::Lua::PushBool(L, consumable);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kSlotArgs[] = {
    Game::Doc::Req("slot", "luaIndex", "A 1-based spellbook slot, or a spell name."),
    Game::Doc::Opt("bookType", "string", nullptr,
                   "\"spell\" or \"pet\" with a slot. With a name, the rank text."),
};
const Game::Doc::Field kConsumableArgs[] = {
    Game::Doc::Req("spell", "number", "A spell ID, a 1-based spellbook slot, or a spell name."),
    Game::Doc::Opt("bookType", "string", nullptr,
                   "\"spell\" or \"pet\" with a slot. With a name, the rank text."),
};
const Game::Doc::Field kAttackRets[] = {
    Game::Doc::Req("isAttack", "boolean", "True if the spell is the melee Attack spell."),
};
const Game::Doc::Field kAutoRepeatRets[] = {
    Game::Doc::Req("isAutoRepeat", "boolean", "True if the spell is repeating now."),
};
const Game::Doc::Field kConsumableRets[] = {
    Game::Doc::Req("isConsumable", "boolean", "True if the spell uses up reagents."),
};
const Game::Doc::Function kIsAttackSpell{
    "Returns true if a spellbook spell is the melee Attack spell.",
    kSlotArgs, kAttackRets, "SpellGlobals"};
const Game::Doc::Function kIsAutoRepeatSpell{
    "Returns true if a spellbook spell is the spell that repeats now, such as Auto Shot.",
    kSlotArgs, kAutoRepeatRets, "SpellGlobals"};
const Game::Doc::Function kIsConsumableSpell{
    "Returns true if a spell uses up reagents.",
    kConsumableArgs, kConsumableRets, "SpellGlobals"};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsAttackSpell", &Script_IsAttackSpell, &kIsAttackSpell);
    Game::Lua::RegisterGlobalFunction("IsAutoRepeatSpell", &Script_IsAutoRepeatSpell,
                                      &kIsAutoRepeatSpell);
    Game::Lua::RegisterGlobalFunction("IsConsumableSpell", &Script_IsConsumableSpell,
                                      &kIsConsumableSpell);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::SlotTests
