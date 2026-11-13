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

// `C_Item.GetEnchantInfo(enchantID)` -> table | nil
//
// Resolves an item-enchantment ID — the kind `C_Item.GetWeaponEnchantInfo`
// returns for a weapon's temporary enchant (poisons, oils, sharpening
// stones, shaman imbues, …), and the same IDs item permanent enchants
// use — into its `SpellItemEnchantment.dbc` record and returns a table:
//
//   {
//     enchantID = <id>,
//     name      = "<localized display name>",   -- "Crusader", "+8 Spirit"
//     effects   = {                              -- 1..3 active effect slots
//       { type = <ITEM_ENCHANTMENT_TYPE>, amount = <int>, arg = <int> },
//       ...
//     },
//     spellID   = <int>,   -- present only for spell-type enchants (proc/
//                          -- equip/use): the EffectArg of the first such
//                          -- effect, e.g. Crusader -> 20007 "Holy Strength".
//   }
//
// `effects[i].type` is the standard ITEM_ENCHANTMENT_TYPE:
//   1 = combat-proc spell   (arg = spellID)
//   2 = weapon damage       (amount = +dmg)
//   3 = equip spell / aura  (arg = spellID)
//   4 = resistance / armor  (amount = +value)
//   5 = stat                (arg = stat index, amount = value)
//   6 = totem, 7 = use spell
// For spell types (1/3/7) `arg` is a spellID you can feed straight into
// `C_Spell.GetSpellInfo` / `GetSpellDescription`; `spellID` is surfaced
// at top level as a convenience for that common case.
//
// Returns nil for a non-numeric / non-positive id, an out-of-range id,
// or a record with no name. Lives in C_Item (not C_Spell) because the
// id originates from `C_Item.GetWeaponEnchantInfo` and the concept is an
// item enchantment — `SpellItemEnchantment` is just the DBC's internal
// name (enchants are *implemented* via spell effects).
//
// Not derivable: the source item. The record holds no back-reference to
// the item that applied the enchant, and vanilla has no client-side
// reverse index (enchantID -> item).

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"

#include <cstdint>

namespace Item::EnchantInfo {

namespace {

constexpr int kNumEffects = 3;
constexpr int kNumNameLocales = 8; // Name[8]; field after is the locale mask

// Spell-type enchants whose EffectArg is a spellID (proc / equip / use).
bool IsSpellType(int type) { return type == 1 || type == 3 || type == 7; }

int __fastcall Script_C_Item_GetEnchantInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0; // nil
    const int enchantID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (enchantID <= 0)
        return 0;

    const uint8_t *rec = DBC::Record(Offsets::VAR_SPELLITEMENCHANT_RECORDS,
                                     Offsets::VAR_SPELLITEMENCHANT_COUNT,
                                     static_cast<uint32_t>(enchantID));
    if (rec == nullptr)
        return 0; // OOR id or unpopulated slot -> nil

    // Localized display name (8-slot locale array at +0x34). Clamp the
    // locale index to the array bounds — index 8 would read the locale
    // mask column that follows. enUS (0) fallback for empty slots.
    auto names = Game::Ptr<const char *const>(rec, Offsets::OFF_SPELLITEMENCHANT_NAME);
    int locale = Game::Read<int>(Offsets::VAR_LOCALE_INDEX);
    if (locale < 0 || locale >= kNumNameLocales)
        locale = 0;
    const char *name = names[locale];
    if (name == nullptr || name[0] == '\0')
        name = names[0]; // enUS fallback
    if (name == nullptr || name[0] == '\0')
        return 0; // unnamed record -> nil

    auto type = Game::Ptr<const int32_t>(rec, Offsets::OFF_SPELLITEMENCHANT_TYPE);
    auto amount = Game::Ptr<const int32_t>(rec, Offsets::OFF_SPELLITEMENCHANT_AMOUNT);
    auto arg = Game::Ptr<const int32_t>(rec, Offsets::OFF_SPELLITEMENCHANT_ARG);

    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "enchantID", enchantID);
    Game::Lua::SetFieldString(L, "name", name);

    // effects = { {type,amount,arg}, ... } — only non-empty slots.
    Game::Lua::PushString(L, "effects");
    Game::Lua::NewTable(L);
    int n = 0;
    int spellID = 0;
    for (int i = 0; i < kNumEffects; ++i) {
        if (type[i] == 0)
            continue;
        if (spellID == 0 && IsSpellType(type[i]) && arg[i] != 0)
            spellID = arg[i];
        ++n;
        Game::Lua::PushNumber(L, n); // 1-based array index
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldNumber(L, "type", type[i]);
        Game::Lua::SetFieldNumber(L, "amount", amount[i]);
        Game::Lua::SetFieldNumber(L, "arg", arg[i]);
        Game::Lua::SetTable(L, -3); // effects[n] = entry
    }
    Game::Lua::SetTable(L, -3); // result.effects = effects

    if (spellID != 0)
        Game::Lua::SetFieldNumber(L, "spellID", spellID);

    return 1;
}

const Game::Doc::Field kEffectFields[] = {
    Game::Doc::Req("type", "number",
                   "1 proc spell, 2 weapon damage, 3 equip spell, 4 resistance or armor, 5 stat, "
                   "6 totem, 7 use spell."),
    Game::Doc::Req("amount", "number"),
    Game::Doc::Req("arg", "number", "The spell ID for types 1, 3 and 7; the stat for type 5."),
};
const Game::Doc::Structure kItemEnchantEffect{"ItemEnchantEffect", "Item", kEffectFields,
                                              "One effect of an item enchantment."};

const Game::Doc::Field kInfoFields[] = {
    Game::Doc::Req("enchantID", "number"),
    Game::Doc::Req("name", "string"),
    Game::Doc::Req("effects", "table", "An array of ItemEnchantEffect, empty effects left out."),
    Game::Doc::Opt("spellID", "number", nullptr,
                   "The spell of the first spell effect; nil when the enchantment has none."),
};
const Game::Doc::Structure kItemEnchantInfo{"ItemEnchantInfo", "Item", kInfoFields};

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("enchantID", "number"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("info", "ItemEnchantInfo", nullptr,
                   "Nil when no named enchantment has that ID."),
};
const Game::Doc::Function kGetEnchantInfo{
    "Returns the name and effects of an item enchantment.", kArgs, kRets, nullptr, true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetEnchantInfo",
                                     &Script_C_Item_GetEnchantInfo, &kGetEnchantInfo);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::EnchantInfo
