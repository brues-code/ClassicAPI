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

// `IsUsableItem(item)` / `C_Item.IsUsableItem(item)` — can the player use
// the item right now, returning `(usable, noMana)`. The item analog of
// `IsUsableSpell`; whether the item can be *equipped* is
// `C_PlayerInfo.CanUseItem`'s question, not this one.
//
// 1.12 answers this question itself for an item on the action bar: the
// per-slot usable recompute FUN_004E5050 behind `IsUsableAction`. This is
// that item leg, for any item, and it is the shape of 4.3.4's
// Script_IsUsableItem (GameUI_Scripts.cpp):
//   1. The player carries the item (4.3.4 GetItemTypeCount, 1.12 the
//      slot's item count).
//   2. The item's on-use spell is not held: a spell marked disabled while
//      active stays unusable while its effect lasts, and its cooldown
//      starts when the effect ends (FUN_ITEM_SPELL_HELD; 4.3.4
//      Spell_C_NeedsCooldownEvent). A running cooldown does not grey it.
//   3. No on-use spell: usable while the player controls the character
//      (VAR_PLAYER_HAS_CONTROL, as the action bar answers a slot with no
//      spell).
//   4. Otherwise the engine's spell test FUN_SPELL_IS_USABLE decides,
//      setting `noMana` when only the power cost fails.
// Neither engine checks the item's level, class or race here; using such
// an item fails with the engine's error when it is used.
//
// Cache-miss handling matches `C_Item.IsEquippableItem`: a sync peek with
// no load fired, returning `(false, false)` if the item isn't cached yet.

#include "Game.h"
#include "Offsets.h"
#include "item/Arg.h"
#include "item/Count.h"
#include "item/Record.h"
#include "item/Spell.h"
#include "spell/Lookup.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Item::Usable {

namespace {

using SpellIsUsable_t = int(__fastcall *)(const uint8_t *spellRecord, int *outNoMana);
using ItemSpellHeld_t = uint32_t(__fastcall *)(int itemID);

struct Usability {
    bool usable;
    bool noMana;
};

// `L` is needed for the carried count (see Item::Count::InInventory); the
// caller has already read its arguments.
Usability ComputeUsability(void *L, int itemID) {
    Usability r{false, false};
    if (itemID <= 0 || Unit::Identity::PlayerObject() == nullptr)
        return r;

    auto *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr)
        return r; // not cached → sync "false", no load fired

    if (Item::Count::InInventory(L, itemID) <= 0)
        return r;
    if (reinterpret_cast<ItemSpellHeld_t>(Offsets::FUN_ITEM_SPELL_HELD)(itemID) != 0)
        return r;

    const int onUseSpellID = Item::Spell::FindOnUseSpellIDInRecord(record);
    if (onUseSpellID == 0) {
        r.usable = Game::Read<int>(static_cast<uintptr_t>(Offsets::VAR_PLAYER_HAS_CONTROL)) != 0;
        return r;
    }

    // `::Spell::Lookup` (leading `::`) — inside `Item::Usable`, a bare
    // `Spell::` binds to the included `Item::Spell` namespace.
    auto *spellRec = ::Spell::Lookup::RecordForID(onUseSpellID);
    if (spellRec == nullptr)
        return r;

    int noMana = 0;
    const int usable =
        reinterpret_cast<SpellIsUsable_t>(Offsets::FUN_SPELL_IS_USABLE)(spellRec, &noMana) & 0xFF;
    r.usable = usable != 0;
    r.noMana = noMana != 0;
    return r;
}

// `IsUsableItem(item)` — legacy global. Returns `(usable, noMana)` as
// 1/nil pairs, matching stock 1.12's `IsUsableAction` / our
// `IsUsableSpell` convention.
int __fastcall Script_IsUsableItem(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    const Usability r = ComputeUsability(L, itemID);
    Game::Lua::SetTop(L, 0);
    if (r.usable)
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    if (r.noMana)
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 2;
}

// `C_Item.IsUsableItem(item)` — modern table-namespace form. Returns
// proper booleans (`usable`, `noMana`) per the `C_Item.*` convention.
int __fastcall Script_C_Item_IsUsableItem(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    const Usability r = ComputeUsability(L, itemID);
    Game::Lua::SetTop(L, 0);
    Game::Lua::PushBool(L, r.usable);
    Game::Lua::PushBool(L, r.noMana);
    return 2;
}

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsUsableItem", &Script_IsUsableItem);
    Game::Lua::RegisterTableFunction("C_Item", "IsUsableItem",
                                     &Script_C_Item_IsUsableItem);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::Usable
