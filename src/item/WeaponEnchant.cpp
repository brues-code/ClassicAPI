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
#include "event/Custom.h"
#include "item/CGItem.h"
#include "item/Location.h"
#include "net/PacketDispatch.h"
#include "tick/WorldTick.h"

#include <cstdint>

namespace Item::WeaponEnchant {

namespace {

struct EnchantInfo {
    bool has;
    bool hasExpiration;
    uint32_t expirationMs;
    uint32_t charges;
    uint32_t enchantID;
};

// Reads the temporary-enchant slot of `cgItem`. `has` is true iff the
// slot holds an enchant id — the engine's own gate (1.12's
// `Script_GetWeaponEnchantInfo` and 4.3.4's both test the id alone). The
// time left comes from the item's expiry clock, which the enchant time
// update sets and which counts down between server updates; the
// descriptor duration field only gates it (0 → no time), exactly as the
// engine reads it. Returns
// a zeroed struct for an empty slot, a missing item, or a missing
// descriptor, so every caller can report "no enchant" uniformly.
//
// The slot layout lives in `Offsets.h` (`OFF_DESCRIPTOR_ENCHANTMENT_*`)
// — the same block `spell/BonusDamage.cpp` walks for enchant-granted
// spell power.
EnchantInfo ReadTempEnchant(const uint8_t *cgItem) {
    EnchantInfo r{false, false, 0, 0, 0};
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
    if (r.enchantID == 0)
        return r;
    r.has = true;
    r.charges = Game::Read<uint32_t>(
        desc, kSlot + Offsets::DESCRIPTOR_ENCHANTMENT_CHARGES_DELTA);
    if (Game::Read<uint32_t>(
            desc, kSlot + Offsets::DESCRIPTOR_ENCHANTMENT_DURATION_DELTA) != 0) {
        r.hasExpiration = true;
        using TimeLeft_t = uint32_t(__fastcall *)(const uint8_t *item, void *edx, int slot);
        r.expirationMs = reinterpret_cast<TimeLeft_t>(
            static_cast<uintptr_t>(Offsets::FUN_ITEM_ENCHANTMENT_TIME_LEFT))(
            cgItem, nullptr, Offsets::DESCRIPTOR_ENCHANTMENT_SLOT_TEMPORARY);
    }
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
    const EnchantInfo main = ReadEquippedTempEnchant(Offsets::INVSLOT_MAINHAND);
    const EnchantInfo off = ReadEquippedTempEnchant(Offsets::INVSLOT_OFFHAND);
    const EnchantInfo ranged = ReadEquippedTempEnchant(Offsets::INVSLOT_RANGED);

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

// `C_PaperDollInfo.GetTemporaryEnchantmentInfo(slot)` — retail's per-slot
// temporary enchant, the one its deprecated `GetWeaponEnchantInfo` is now
// built from. `slot` is an equipment slot (INVSLOT_*). Returns a
// `TemporaryItemEnchantInfo` table, or nothing when the slot has no
// temporary enchant (or no item, or is not an equipment slot).
// `hasExpirationTime` is the descriptor duration gate `ReadTempEnchant`
// reads; without it `remainingTimeMs` is 0.
const Game::Doc::Field kTemporaryItemEnchantInfoFields[] = {
    Game::Doc::Req("enchantID", "number"),
    Game::Doc::Req("remainingTimeMs", "number"),
    Game::Doc::Req("chargesRemaining", "number"),
    Game::Doc::Req("hasExpirationTime", "bool"),
};
const Game::Doc::Structure kTemporaryItemEnchantInfo{
    "TemporaryItemEnchantInfo", "PaperDollInfo", kTemporaryItemEnchantInfoFields};

const Game::Doc::Field kGetTemporaryEnchantmentInfoArgs[] = {
    Game::Doc::Req("slot", "number", "An INVSLOT_ constant."),
};
const Game::Doc::Field kGetTemporaryEnchantmentInfoRets[] = {
    Game::Doc::Req("enchantInfo", "TemporaryItemEnchantInfo"),
};
const Game::Doc::Function kGetTemporaryEnchantmentInfo{
    "Returns the temporary enchantment on an equipped item, or nothing if there is none.",
    kGetTemporaryEnchantmentInfoArgs, kGetTemporaryEnchantmentInfoRets};

int __fastcall Script_GetTemporaryEnchantmentInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_PaperDollInfo.GetTemporaryEnchantmentInfo(slot)");
        return 0;
    }
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (slot < Offsets::EQUIPMENT_SLOT_FIRST || slot > Offsets::EQUIPMENT_SLOT_LAST)
        return 0;

    const EnchantInfo e = ReadEquippedTempEnchant(slot);
    if (!e.has)
        return 0;
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "enchantID", static_cast<double>(e.enchantID));
    Game::Lua::SetFieldNumber(L, "remainingTimeMs", static_cast<double>(e.expirationMs));
    Game::Lua::SetFieldNumber(L, "chargesRemaining", static_cast<double>(e.charges));
    Game::Lua::SetFieldBool(L, "hasExpirationTime", e.hasExpiration);
    return 1;
}

const Game::Doc::Field kGetWeaponEnchantInfoRets[] = {
    Game::Doc::Req("hasMainHandEnchant", "bool"),
    Game::Doc::Req("mainHandExpiration", "number",
                   "Time left in milliseconds; 0 for an enchant with no time limit."),
    Game::Doc::Req("mainHandCharges", "number"),
    Game::Doc::Req("mainHandEnchantID", "number", "0 when the weapon has no temporary enchant."),
    Game::Doc::Req("hasOffHandEnchant", "bool"),
    Game::Doc::Req("offHandExpiration", "number"),
    Game::Doc::Req("offHandCharges", "number"),
    Game::Doc::Req("offHandEnchantID", "number"),
    Game::Doc::Req("hasRangedEnchant", "bool"),
    Game::Doc::Req("rangedExpiration", "number"),
    Game::Doc::Req("rangedCharges", "number"),
    Game::Doc::Req("rangedEnchantID", "number"),
};
const Game::Doc::Function kGetWeaponEnchantInfo{
    "Returns the temporary enchant, such as a poison or an oil, on each equipped weapon.",
    {}, kGetWeaponEnchantInfoRets};

const Game::Doc::Field kGetItemTempEnchantInfoArgs[] = {
    Game::Doc::Req("itemLocation", "ItemLocation", "An item location or an item GUID."),
};
const Game::Doc::Field kGetItemTempEnchantInfoRets[] = {
    Game::Doc::Req("hasEnchant", "bool",
                   "False for an empty slot or a location that names no item."),
    Game::Doc::Req("expirationMs", "number",
                   "Time left in milliseconds; 0 for an enchant with no time limit."),
    Game::Doc::Req("charges", "number"),
    Game::Doc::Req("enchantID", "number", "0 when the item has no temporary enchant."),
};
const Game::Doc::Function kGetItemTempEnchantInfo{
    "Returns the temporary enchant, such as a poison or an oil, on the item at a location.",
    kGetItemTempEnchantInfoArgs, kGetItemTempEnchantInfoRets, nullptr, true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetWeaponEnchantInfo",
                                      &Script_C_Item_GetWeaponEnchantInfo,
                                      &kGetWeaponEnchantInfo);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemTempEnchantInfo",
                                      &Script_C_Item_GetItemTempEnchantInfo,
                                      &kGetItemTempEnchantInfo);
    Game::Lua::RegisterTableFunction("C_PaperDollInfo", "GetTemporaryEnchantmentInfo",
                                      &Script_GetTemporaryEnchantmentInfo,
                                      &kGetTemporaryEnchantmentInfo);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// --- WEAPON_ENCHANT_CHANGED --------------------------------------------
//
// Retail's no-payload event for "re-read GetWeaponEnchantInfo": its buff
// frame refreshes the weapon-enchant buttons on this event and
// WEAPON_SLOT_CHANGED alone. 1.12 has two places a weapon enchant can
// change, and neither fires an event for the change on its own:
//
//   1. SMSG_ITEM_ENCHANT_TIME_UPDATE sets the item's expiry clock and
//      signals nothing. A renewal of the same enchant (a shaman recasting
//      Rockbiter before the server saves the item) changes only this
//      clock: the server writes no descriptor field when the id,
//      duration and charges all match.
//   2. The ITEM_FIELD_ENCHANTMENT watcher (FUN_ITEM_ENCHANTMENT_WATCHER)
//      runs when the enchant is applied, removed, or loses a charge. The
//      engine only raises UNIT_INVENTORY_CHANGED from it.
//
// Both only mark the event. It is fired on the next WorldTick, once per
// frame. Blizzard's generated docs flag it `UniqueEvent`, so coalescing is
// the event's own shape. It also orders the fire after the engine: packet
// subscribers run before the engine's handler, so a fire from the
// subscriber would show the handler the old expiry.

const Game::Doc::Event kWeaponEnchantChangedDoc{
    "Item",
    "Fires when a temporary enchant on an equipped weapon is applied, renewed, "
    "changes, or ends.",
    {}};
const Event::Custom::AutoReserve _rWeaponEnchantChanged{"WEAPON_ENCHANT_CHANGED",
                                                       &kWeaponEnchantChangedDoc};
bool g_enchantDirty = false;

// Whether `itemGuid` is the item in the active player's main-hand,
// off-hand or ranged slot — the three slots GetWeaponEnchantInfo reads.
// `FindByGUID` takes no Lua stack and resolves only through the
// non-throwing object lookup, so it is safe inside packet processing,
// where both callers run.
bool IsEquippedWeapon(uint64_t itemGuid) {
    Item::Location::ByGUIDResult found;
    if (!Item::Location::FindByGUID(itemGuid, false, &found) ||
        found.place != Item::Location::PLACE_EQUIPPED)
        return false;
    return found.equipmentSlotIndex == Offsets::INVSLOT_MAINHAND ||
           found.equipmentSlotIndex == Offsets::INVSLOT_OFFHAND ||
           found.equipmentSlotIndex == Offsets::INVSLOT_RANGED;
}

void OnEnchantTimePacket(uint32_t opcode, Net::CDataStore *packet) {
    if (opcode != Offsets::SMSG_ITEM_ENCHANT_TIME_UPDATE || packet == nullptr)
        return;
    const uint64_t itemGuid = Net::Read<uint64_t>(packet);
    const uint32_t slot = Net::Read<uint32_t>(packet);
    if (packet->m_read > packet->m_size ||
        slot != Offsets::DESCRIPTOR_ENCHANTMENT_SLOT_TEMPORARY)
        return;
    if (IsEquippedWeapon(itemGuid))
        g_enchantDirty = true;
}

const Net::PacketDispatch::AutoSubscribe _enchantTimeSub{&OnEnchantTimePacket};

// Observer callback ABI (see FUN_TYPE_OBSERVER_REGISTER in Offsets.h).
using EnchantWatcher_t = int(__fastcall *)(uint32_t fieldOffset, uint32_t size,
                                           uint32_t guidLo, uint32_t guidHi,
                                           const void *oldValue, void *userArg);
EnchantWatcher_t g_origEnchantWatcher = nullptr;

int __fastcall EnchantWatcher_h(uint32_t fieldOffset, uint32_t size, uint32_t guidLo,
                                uint32_t guidHi, const void *oldValue, void *userArg) {
    const int ret =
        g_origEnchantWatcher(fieldOffset, size, guidLo, guidHi, oldValue, userArg);
    if (IsEquippedWeapon((static_cast<uint64_t>(guidHi) << 32) | guidLo))
        g_enchantDirty = true;
    return ret;
}

const Game::HookAutoRegister _enchantWatcherHook{
    Offsets::FUN_ITEM_ENCHANTMENT_WATCHER, reinterpret_cast<void *>(&EnchantWatcher_h),
    reinterpret_cast<void **>(&g_origEnchantWatcher)};

// The slot check runs before the flag clears, so a change made while the
// name is still unclaimed is held, not lost.
void OnWorldTick() {
    if (!g_enchantDirty)
        return;
    const int slot = _rWeaponEnchantChanged.Slot();
    if (slot < 0)
        return;
    g_enchantDirty = false;
    Event::Custom::Fire(slot, "");
}

const Tick::WorldTick::AutoSubscribe _enchantTick{&OnWorldTick};

} // namespace

} // namespace Item::WeaponEnchant
