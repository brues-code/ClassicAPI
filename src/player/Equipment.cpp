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

// `PLAYER_EQUIPMENT_CHANGED(equipmentSlot, hasCurrent)` — the WotLK-era
// per-slot paperdoll event. Vanilla only fires UNIT_INVENTORY_CHANGED for
// the player, which doesn't say WHICH slot changed; gear trackers and stat
// sheets built against 3.3+ gate their refresh on this event.
//
// Fired from the engine's own inventory-slot observer
// (FUN_INV_SLOT_CHANGED_OBSERVER), which CGPlayer_C::AddActiveWatchers
// registers on the active player's equipment GUID fields among others.
// The descriptor-observer dispatcher (see the FUN_DESC_OBSERVER_REGISTER
// block in Offsets.h) invokes it only when a slot's item GUID actually
// changed, so there's no polling and no diff bookkeeping on our side.
// That handler is exactly where 4.3.4 fires this event (its descendant
// InventoryUpdate, 0x0056DBD0): after the original has unlocked the
// arriving item, for 0-based slots 0..18. We co-hook it post-original, so
// the engine owns the observer's registration and lifetime.
//
// `equipmentSlot` is the 1-based Lua inventory slot (1 = head … 19 =
// tabard), matching modern. `hasCurrent` is 1 when the slot now holds an
// item and nil when it's empty (the engine dispatcher has no `%b`; 1/nil
// keeps the idiomatic `if hasCurrent then` working — see FireIdSuccess).
//
// Also `UPDATE_INVENTORY_DURABILITY` (no payload), fired after every run
// of the engine's inventory-alerts recompute
// (FUN_INVENTORY_ALERTS_RECOMPUTE) — exactly where 4.3.4 fires it, at the
// end of its descendant CGCharacterInfo::UpdateInventoryAlerts, with no
// change check. Vanilla's callers mirror 4.3.4's (see Offsets.h), so the
// event fires on the same triggers: any owned item's durability change,
// broken-flag / enchantment / visible-item changes, player setup, and Lua
// UpdateInventoryAlertStatus().
//
// Also `WEAPON_SLOT_CHANGED` (no payload) — the narrow sibling of
// PLAYER_EQUIPMENT_CHANGED that swing-timer consumers want: it fires only
// for the three weapon slots (16 main hand / 17 off hand / 18 ranged), so
// they don't filter a 19-slot event in Lua on every gear change. Equip,
// unequip and swap all fire it. It rides the SAME observers — the weapon
// slots are three of the 19 already registered — but is COALESCED to at
// most one fire per frame via a dirty flag drained on WorldTick, because
// one action can change two weapon slots (a two-hander replacing a
// one-hander plus an off-hand) and the per-slot observer would otherwise
// fire twice for it. With no payload there's nothing to lose by merging —
// and Blizzard's generated docs flag this event `UniqueEvent`, where the
// two per-slot events above are `SynchronousEvent`, so coalescing is the
// event's own shape rather than a local preference.
// Slot 18 is exempted for the classes whose slot 18 is a RELIC slot
// (Libram / Idol / Totem) — see IsWeaponSlot.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "event/Custom.h"
#include "object/Resolve.h"
#include "player/StatSignal.h"
#include "tick/WorldTick.h"

#include <cstdint>

namespace Player::Equipment {

namespace {

constexpr const char *kEvtEquipmentChanged = "PLAYER_EQUIPMENT_CHANGED";
const Event::Custom::AutoReserve _r{kEvtEquipmentChanged};
constexpr const char *kEvtDurability = "UPDATE_INVENTORY_DURABILITY";
const Event::Custom::AutoReserve _r2{kEvtDurability};

// System per Blizzard's own generated docs, where all three of this
// file's events live (PaperDollInfoDocumentation.lua) — so documenting
// PLAYER_EQUIPMENT_CHANGED / UPDATE_INVENTORY_DURABILITY later puts them
// in the same place.
constexpr const char *kEvtWeaponSlotChanged = "WEAPON_SLOT_CHANGED";
const Game::Doc::Event kWeaponSlotChangedDoc{
    "PaperDollInfo",
    "Fires when the item in a weapon slot changes: main hand, off hand, or "
    "ranged.",
    {}};
const Event::Custom::AutoReserve _r3{kEvtWeaponSlotChanged,
                                     &kWeaponSlotChangedDoc};

// 0-based descriptor slots for the three weapon slots — the `slot0 =
// luaSlot - 1` convention below, so Lua 16/17/18.
constexpr int kSlot0MainHand = 15;
constexpr int kSlot0OffHand = 16;
constexpr int kSlot0Ranged = 17;
bool g_weaponDirty = false;

// Whether slot 18 holds relics (Libram / Idol / Totem) instead of a ranged
// weapon. Same test Script_UnitHasRelicSlot (0x00519E50) makes: index
// ChrClasses.dbc by the player's class byte and read the record's
// relic-slot field. Read live rather than cached — the class-byte global
// is repopulated per character-enter, so logging out to a character of
// another class would leave a cached answer stale.
bool PlayerHasRelicSlot() {
    const uint8_t *cls =
        DBC::Record(Offsets::VAR_CHRCLASSES_RECORDS, Offsets::VAR_CHRCLASSES_COUNT,
                    Game::Read<uint8_t>(Offsets::VAR_PLAYER_CLASS_BYTE));
    return cls != nullptr &&
           Game::Read<uint32_t>(cls, Offsets::OFF_CHRCLASSES_RELIC_SLOT) != 0;
}

// A relic is a passive stat item, never something you swing, so a relic
// swap is not a weapon change — the three classes with a relic slot have
// no ranged weapon to report at all.
bool IsWeaponSlot(int slot0) {
    if (slot0 == kSlot0MainHand || slot0 == kSlot0OffHand)
        return true;
    return slot0 == kSlot0Ranged && !PlayerHasRelicSlot();
}

// Whether the slot currently holds an item — read from the player's
// inventory-manager GUID array, the same source the engine's own bag
// observer (FUN_004F8DB0) treats as the post-change truth when its field
// observer fires.
bool SlotHasItem(const uint8_t *player, int slot0Based) {
    const uint8_t *invMgr = player + Offsets::OFF_PLAYER_INVENTORY_MANAGER;
    const uint32_t count = *reinterpret_cast<const uint32_t *>(
        invMgr + Offsets::OFF_INVMGR_SLOT_COUNT);
    if (static_cast<uint32_t>(slot0Based) >= count)
        return false;
    const uint64_t *guids = *reinterpret_cast<const uint64_t *const *>(
        invMgr + Offsets::OFF_INVMGR_GUID_ARRAY);
    return guids != nullptr && guids[slot0Based] != 0;
}

// --- PLAYER_EQUIPMENT_CHANGED (inventory-slot observer co-hook) ---------

// ABI per the observer-callback note in Offsets.h: __fastcall(fieldOffset
// /*ecx*/, size /*edx*/, guidLo, guidHi, oldValue*, userArg), return 1,
// callee cleans 0x10.
using SlotChanged_t = int(__fastcall *)(uint32_t fieldOffset, uint32_t size,
                                        uint32_t guidLo, uint32_t guidHi,
                                        const uint32_t *oldValue, void *userArg);
SlotChanged_t g_origSlotChanged = nullptr;

// The player is resolved from the GUID the observer receives, via the
// NON-THROWING object resolver — the same call the original makes, and the
// same early-out 4.3.4's InventoryUpdate takes: no player, no event. NOT via
// FUN_RESOLVE_UNIT_TOKEN("player"): that's the assert-frame helper that
// raises a Lua error when the token doesn't resolve, and this runs inside
// SMSG_UPDATE_OBJECT processing, where the player object can be mid-create
// and momentarily unresolvable — a Lua error thrown from there unwinds
// through raw engine code.
int __fastcall SlotChanged_h(uint32_t fieldOffset, uint32_t size,
                             uint32_t guidLo, uint32_t guidHi,
                             const uint32_t *oldValue, void *userArg) {
    const int ret =
        g_origSlotChanged(fieldOffset, size, guidLo, guidHi, oldValue, userArg);

    const int slot0 =
        static_cast<int>(fieldOffset - Offsets::OFF_DESC_PLAYER_EQUIP_FIRST) >> 3;
    if (slot0 < 0 || slot0 >= Offsets::DESC_PLAYER_EQUIP_SLOTS)
        return ret;
    auto *player = static_cast<const uint8_t *>(Object::ByGuid(
        Offsets::TYPEMASK_PLAYER,
        (static_cast<uint64_t>(guidHi) << 32) | guidLo, "ClassicAPI", 0x172));
    if (player == nullptr)
        return ret;

    Event::Custom::FireIdSuccess(_r.Slot(), slot0 + 1, SlotHasItem(player, slot0));
    // Gear swap changes item +healing and Spirit/Armor — invalidate the
    // GetSpellBonusHealing cache.
    Player::StatSignal::Notify();
    // Mark only; the fire is merged onto the next WorldTick so one action
    // that changes two weapon slots yields one event.
    if (IsWeaponSlot(slot0))
        g_weaponDirty = true;
    return ret;
}

static const Game::HookAutoRegister _slotChangedHook{
    Offsets::FUN_INV_SLOT_CHANGED_OBSERVER,
    reinterpret_cast<void *>(&SlotChanged_h),
    reinterpret_cast<void **>(&g_origSlotChanged)};

// Drain for WEAPON_SLOT_CHANGED. The slot check runs BEFORE clearing the
// flag: while the reservation is unclaimed (`Slot()` -1, between static
// init and the first `frame:RegisterEvent`) the change is held rather than
// swallowed. Non-subscribers pay one already-false branch per frame.
void OnWorldTick() {
    if (!g_weaponDirty)
        return;
    const int slot = _r3.Slot();
    if (slot < 0)
        return;
    g_weaponDirty = false;
    Event::Custom::Fire(slot, "");
}

const Tick::WorldTick::AutoSubscribe _weaponTick{&OnWorldTick};

// --- UPDATE_INVENTORY_DURABILITY (alerts-recompute co-hook) --------------

using AlertsRecompute_t = void(__fastcall *)();
AlertsRecompute_t g_origAlertsRecompute = nullptr;

void __fastcall AlertsRecompute_h() {
    g_origAlertsRecompute();
    Event::Custom::Fire(_r2.Slot(), "");
}

static const Game::HookAutoRegister _alertsHook{
    Offsets::FUN_INVENTORY_ALERTS_RECOMPUTE,
    reinterpret_cast<void *>(&AlertsRecompute_h),
    reinterpret_cast<void **>(&g_origAlertsRecompute)};

} // namespace

} // namespace Player::Equipment
