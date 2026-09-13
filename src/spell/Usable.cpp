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

// `IsUsableSpell(spell)` / `IsUsableSpell(slot, bookType)` and
// `C_Spell.IsSpellUsable(spell)` — `(usable, noMana)` for a spell.
//
// The verdict is the engine's own. `IsUsableAction` reads a per-slot cache
// that the recompute `FUN_004E5050` fills by calling `FUN_SPELL_IS_USABLE`
// on the slot's spell record (player spells) or by comparing the pet's power
// against the spell cost (pet spells). This module runs the same two paths
// live, so `IsUsableSpell(id)` agrees with `IsUsableAction(slot)` for a slot
// holding that spell — and is never stale, since it doesn't go through the
// cache. Whatever the helper decides (stance / form, reagents, equipped
// weapon and ammo, combo points, aura states, control loss, power) is what
// we return; see the `FUN_SPELL_IS_USABLE` note in Offsets.h for the full
// verified list. Like the engine, this does NOT fold in cooldown — that is a
// separate concern (`GetSpellCooldown`), and FrameXML greys / swipes the two
// independently.
//
// An earlier version hand-rolled five checks (known, alive, off cooldown,
// power, reagents) instead of calling the helper. It diverged from
// `IsUsableAction` exactly where the helper does more (stance) or less
// (cooldown) — GitHub issue #51. Don't reintroduce a parallel check list.
//
// The one thing added on top of the helper is a knowledge gate: the helper
// doesn't check whether the player knows the spell (it's also fed item
// on-use spells), and the action bar only ever holds known spells, so a
// spell the player hasn't learned reports `(nil, nil)` here.

#include "Game.h"
#include "Offsets.h"
#include "object/Resolve.h"
#include "spell/Arg.h"
#include "spell/Lookup.h"

#include <cstdint>

namespace Spell::Usable {

namespace {

using SpellIsUsable_t = int(__fastcall *)(const uint8_t *spellRecord, int *outNoMana);
using GetSpellCost_t = uint32_t(__fastcall *)(int spellID, int unit);
using PetActionsUsable_t = int(__cdecl *)();

// Returns true if the player knows `spellID` per the engine's spell-
// knowledge bitmap at `[VAR_PLAYER_SPELL_BITMAP]`. Same check
// `IsPlayerSpell` (`src/spell/Info.cpp`) uses — covers trained
// abilities, talents, racials, profession recipes, and spells that are
// not on any action bar.
bool PlayerKnowsSpell(int spellID) {
    if (spellID <= 0)
        return false;
    auto *bitmap = Game::Read<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PLAYER_SPELL_BITMAP));
    if (bitmap == nullptr)
        return false;
    const int spellCount = Game::Read<int>(
        static_cast<uintptr_t>(Offsets::VAR_SPELL_RECORD_COUNT));
    if (spellID > spellCount)
        return false;
    return (bitmap[spellID >> 5] & (1u << (spellID & 31))) != 0;
}

struct Usability {
    bool usable;
    bool noMana;
};

// Player spell — the spell branch of `FUN_004E5050`: the engine helper on
// the record, `noMana` from its out-param.
Usability ComputePlayer(int spellID) {
    Usability r{false, false};
    if (!PlayerKnowsSpell(spellID))
        return r;
    auto *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return r;

    auto isUsable = reinterpret_cast<SpellIsUsable_t>(Offsets::FUN_SPELL_IS_USABLE);
    int noMana = 0;
    r.usable = (isUsable(record, &noMana) & 0xFF) != 0;
    r.noMana = noMana != 0;
    return r;
}

// Pet spell — the pet branch of `FUN_004E5050`, mirrored step for step:
// the pet must be able to act (`FUN_PET_ACTIONS_USABLE`), then the pet's
// current power of the spell's PowerType (-2 = health) is compared against
// the cost from `FUN_GET_SPELL_COST(spellID, 0)` — the engine passes unit
// 0 here too. `cost <= power` is a signed compare, as in the engine.
Usability ComputePet(int spellID) {
    Usability r{false, false};
    auto *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return r;

    auto petCanAct = reinterpret_cast<PetActionsUsable_t>(Offsets::FUN_PET_ACTIONS_USABLE);
    if (petCanAct() == 0)
        return r;

    const uint64_t petGuid = Game::Read<uint64_t>(
        static_cast<uintptr_t>(Offsets::VAR_PET_GUID));
    auto *pet = static_cast<const uint8_t *>(
        Object::ByGuid(Offsets::TYPEMASK_UNIT, petGuid));
    if (pet == nullptr)
        return r;
    auto *desc = Game::Read<const uint8_t *>(pet, Offsets::OFF_UNIT_DESCRIPTOR);
    if (desc == nullptr)
        return r;

    const int powerType = Game::Read<int>(record, Offsets::OFF_SPELL_RECORD_POWER_TYPE);
    int power;
    if (powerType == -2) {
        power = Game::Read<int>(desc, Offsets::OFF_UNIT_FIELD_HEALTH);
    } else if (powerType >= 0 && powerType <= 4) {
        power = Game::Read<int>(desc, Offsets::OFF_UNIT_FIELD_POWER1 + powerType * 4);
    } else {
        return r; // not a power type Spell.dbc can hold; the engine would index garbage
    }

    auto getCost = reinterpret_cast<GetSpellCost_t>(Offsets::FUN_GET_SPELL_COST);
    const int cost = static_cast<int>(getCost(spellID, 0));
    if (cost <= power) {
        r.usable = true;
        return r;
    }
    r.noMana = true;
    return r;
}

// `bookType` 1 = pet book (the `(slot, "pet")` arg shape). A spell given
// by ID takes the player path when the player knows it; otherwise, if it
// sits in the pet book (Growl, Cower, …), the pet path.
Usability Compute(int spellID, int bookType) {
    if (bookType == 1)
        return ComputePet(spellID);
    if (PlayerKnowsSpell(spellID))
        return ComputePlayer(spellID);
    int foundBook = 0;
    if (Spell::Lookup::FindSpellbookSlot(spellID, &foundBook) != 0 && foundBook == 1)
        return ComputePet(spellID);
    return Usability{false, false};
}

// `IsUsableSpell(spellID)` or `IsUsableSpell(slot, bookType)`. Writes the
// engine bookType (0 player, 1 pet) to `*outBookType`; returns the spellID
// (0 when the slot is empty / out of range).
int ResolveSpellArg(void *L, int *outBookType) {
    *outBookType = 0;
    if (!Game::Lua::IsNumber(L, 1))
        return 0;
    const int arg1 = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (Game::Lua::Type(L, 2) == Game::Lua::TYPE_STRING) {
        const char *book = Game::Lua::ToString(L, 2);
        const int bookType = static_cast<int>(
            book != nullptr &&
            (book[0] == 'p' || book[0] == 'P') &&
            (book[1] == 'e' || book[1] == 'E') &&
            (book[2] == 't' || book[2] == 'T') &&
            book[3] == 0);
        *outBookType = bookType;
        return Spell::Lookup::SpellbookSlotToID(arg1, bookType);
    }
    return arg1;
}

// `IsUsableSpell(spell)` / `IsUsableSpell(slot, bookType)` — vanilla-
// shaped legacy global. Returns `(usable, noMana)` as 1/nil pairs to
// match `Script_IsUsableAction`'s convention in stock 1.12.
int __fastcall Script_IsUsableSpell(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: IsUsableSpell(spellID) or IsUsableSpell(slot, bookType)");
        return 0;
    }
    int bookType = 0;
    const int spellID = ResolveSpellArg(L, &bookType);
    const Usability r = Compute(spellID, bookType);
    if (r.usable) {
        Game::Lua::PushNumber(L, 1.0);
        Game::Lua::PushNil(L);
    } else if (r.noMana) {
        Game::Lua::PushNil(L);
        Game::Lua::PushNumber(L, 1.0);
    } else {
        Game::Lua::PushNil(L);
        Game::Lua::PushNil(L);
    }
    return 2;
}

// `C_Spell.IsSpellUsable(spell)` — modern table-namespace form. Accepts
// a spellID, link, or name. Returns proper booleans (`isUsable`,
// `insufficientPower`) per the `C_Spell.*` convention, not 1/nil pairs.
int __fastcall Script_C_Spell_IsSpellUsable(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    const Usability r = Compute(spellID, 0);
    Game::Lua::PushBool(L, r.usable);
    Game::Lua::PushBool(L, r.noMana);
    return 2;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kLegacyArgs[] = {
    Game::Doc::Req("spell", "number", "A spell ID, or a spellbook slot when bookType is given."),
    Game::Doc::Opt("bookType", "string", nullptr,
                   "\"spell\" or \"pet\"; makes the first argument a slot in that book."),
};
const Game::Doc::Field kLegacyRets[] = {
    Game::Doc::Opt("usable", "number", nullptr, "1 when the spell can be cast now, else nil."),
    Game::Doc::Opt("noMana", "number", nullptr, "1 when power is the only block, else nil."),
};
const Game::Doc::Function kIsUsableSpell{
    "Whether the player can cast the spell now, in 1/nil pairs like IsUsableAction; "
    "cooldown is not part of the answer.",
    kLegacyArgs, kLegacyRets, "SpellGlobals"};

const Game::Doc::Field kModernArgs[] = {
    Game::Doc::Req("spell", "SpellIdentifier", "A spell ID, spell link, or spell name."),
};
const Game::Doc::Field kModernRets[] = {
    Game::Doc::Req("isUsable", "bool", "True when the spell can be cast now."),
    Game::Doc::Req("insufficientPower", "bool", "True when power is the only block."),
};
const Game::Doc::Function kIsSpellUsable{
    "Whether the player can cast the spell now; cooldown is not part of the answer.",
    kModernArgs, kModernRets};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsUsableSpell", &Script_IsUsableSpell, &kIsUsableSpell);
    Game::Lua::RegisterTableFunction("C_Spell", "IsSpellUsable",
                                     &Script_C_Spell_IsSpellUsable, &kIsSpellUsable);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::Usable
