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

#include "Game.h"
#include "Offsets.h"
#include "item/CGItem.h"
#include "item/Location.h"

#include <cstdint>

namespace Item::WeaponEnchant {

namespace {

constexpr int INVSLOT_MAINHAND = 16;
constexpr int INVSLOT_OFFHAND = 17;
constexpr int INVSLOT_RANGED = 18;

struct EnchantInfo {
    bool has;
    uint32_t expirationMs;
    uint32_t charges;
    uint32_t enchantID;
};

// Reads the temporary-enchant slot of `cgItem`. `has` is true iff an
// enchant is currently active (id non-zero AND non-expired) — matches
// modern `GetWeaponEnchantInfo`'s hasMainHandEnchant semantic. Returns
// a zeroed struct for an empty slot, a missing item, or a missing
// descriptor, so every caller can report "no enchant" uniformly.
//
// The slot layout lives in `Offsets.h` (`OFF_DESCRIPTOR_ENCHANTMENT_*`)
// — the same block `spell/BonusDamage.cpp` walks for enchant-granted
// spell power.
EnchantInfo ReadTempEnchant(const uint8_t *cgItem) {
    EnchantInfo r{false, 0, 0, 0};
    if (cgItem == nullptr)
        return r;
    const uint8_t *desc = Item::ObjectFields(cgItem);
    if (desc == nullptr)
        return r;

    constexpr uintptr_t kSlot =
        Offsets::OFF_DESCRIPTOR_ENCHANTMENT_ID +
        Offsets::DESCRIPTOR_ENCHANTMENT_SLOT_TEMPORARY *
            Offsets::DESCRIPTOR_ENCHANTMENT_SLOT_STRIDE;
    r.enchantID = Game::Read<uint32_t>(desc, kSlot);
    r.expirationMs = Game::Read<uint32_t>(
        desc, kSlot + Offsets::DESCRIPTOR_ENCHANTMENT_DURATION_DELTA);
    r.charges = Game::Read<uint32_t>(
        desc, kSlot + Offsets::DESCRIPTOR_ENCHANTMENT_CHARGES_DELTA);
    r.has = (r.enchantID != 0 && r.expirationMs > 0);
    return r;
}

EnchantInfo ReadEquippedTempEnchant(int paperdollSlot) {
    return ReadTempEnchant(Item::Location::ResolveEquipmentSlot(paperdollSlot));
}

// `C_Item.GetWeaponEnchantInfo()` — modern 12-tuple. Vanilla 1.12's
// global `GetWeaponEnchantInfo` returns 8 values without enchant
// IDs; this returns the modern signature for each weapon slot:
//
//   1.  hasMainHandEnchant     (bool)
//   2.  mainHandExpiration     (ms remaining)
//   3.  mainHandCharges        (int)
//   4.  mainHandEnchantID      (int — 0 if no temp enchant)
//   5.  hasOffHandEnchant
//   6.  offHandExpiration
//   7.  offHandCharges
//   8.  offHandEnchantID
//   9.  hasRangedEnchant
//   10. rangedExpiration
//   11. rangedCharges
//   12. rangedEnchantID
//
// Reads from CGItem descriptor +0x4C (the TEMPORARY enchantment
// slot — slot 1 in the ITEM_FIELD_ENCHANTMENT 7-slot array at
// +0x40). The PERMANENT enchant (Crusader, Mongoose, etc.) lives
// in slot 0 at +0x40 and isn't reported here — modern's
// `GetWeaponEnchantInfo` is specifically about the timed
// temp-enchant data, which is what this function returns.
int __fastcall Script_C_Item_GetWeaponEnchantInfo(void *L) {
    const EnchantInfo main = ReadEquippedTempEnchant(INVSLOT_MAINHAND);
    const EnchantInfo off = ReadEquippedTempEnchant(INVSLOT_OFFHAND);
    const EnchantInfo ranged = ReadEquippedTempEnchant(INVSLOT_RANGED);

    Game::Lua::PushBool(L, main.has);
    Game::Lua::PushNumber(L, static_cast<double>(main.expirationMs));
    Game::Lua::PushNumber(L, static_cast<double>(main.charges));
    Game::Lua::PushNumber(L, static_cast<double>(main.enchantID));

    Game::Lua::PushBool(L, off.has);
    Game::Lua::PushNumber(L, static_cast<double>(off.expirationMs));
    Game::Lua::PushNumber(L, static_cast<double>(off.charges));
    Game::Lua::PushNumber(L, static_cast<double>(off.enchantID));

    Game::Lua::PushBool(L, ranged.has);
    Game::Lua::PushNumber(L, static_cast<double>(ranged.expirationMs));
    Game::Lua::PushNumber(L, static_cast<double>(ranged.charges));
    Game::Lua::PushNumber(L, static_cast<double>(ranged.enchantID));

    return 12;
}

// `C_Item.GetItemTempEnchantInfo(itemLocation)` — the same temporary
// enchant `GetWeaponEnchantInfo` reports, for ANY item the player owns
// rather than only the three equipped weapon slots:
//
//   1. hasEnchant    (bool)
//   2. expirationMs  (ms remaining)
//   3. charges       (int)
//   4. enchantID     (int — 0 if no temp enchant)
//
// Same 4-tuple `GetWeaponEnchantInfo` returns per slot, so the two
// compose, and `enchantID` feeds `C_Item.GetEnchantInfo`.
//
// A rogue's poisoned weapon keeps its temp enchant while it sits in a
// bag, and the equipped-slot getters cannot see it there. Accepts every
// location form the rest of the `C_Item` location family does (table or
// GUID string) via `Item::Location::Resolve`.
//
// An unresolvable location reports "no enchant" rather than raising or
// returning nothing, matching what an empty equipped slot reports.
int __fastcall Script_C_Item_GetItemTempEnchantInfo(void *L) {
    if (!Item::Location::IsLocationArg(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Item.GetItemTempEnchantInfo(itemLocation)");
        return 0;
    }

    const EnchantInfo e = ReadTempEnchant(Item::Location::Resolve(L, 1));
    Game::Lua::PushBool(L, e.has);
    Game::Lua::PushNumber(L, static_cast<double>(e.expirationMs));
    Game::Lua::PushNumber(L, static_cast<double>(e.charges));
    Game::Lua::PushNumber(L, static_cast<double>(e.enchantID));
    return 4;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetWeaponEnchantInfo",
                                      &Script_C_Item_GetWeaponEnchantInfo);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemTempEnchantInfo",
                                      &Script_C_Item_GetItemTempEnchantInfo);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::WeaponEnchant
