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
// Fully event-driven via the engine's own descriptor-field observer system
// (see the FUN_DESC_OBSERVER_REGISTER block in Offsets.h). The engine's
// inventory setup (FUN_INV_OBSERVER_SETUP, once per enter-world) registers
// change observers for the bag/backpack/bank/keyring GUID fields of the
// player descriptor — but never for the 19 equipment slots (0x4A8..0x538).
// We co-hook that setup and register our own observer per equipment field,
// mirroring the engine's registration shape exactly (bank 4, size 8). The
// dispatcher invokes us only when a slot's item GUID actually changed
// (per-node mirror memcmp), so there's no polling and no diff bookkeeping
// on our side. The setup re-runs each enter-world; on logout/login the
// player object is recreated so old nodes die with it, but /reload REUSES
// the object, so we unregister-then-register per slot to avoid stacking a
// duplicate observer each reload (see ObserverSetup_h).
//
// `equipmentSlot` is the 1-based Lua inventory slot (1 = head … 19 =
// tabard), matching modern. `hasCurrent` is 1 when the slot now holds an
// item and nil when it's empty (the engine dispatcher has no `%b`; 1/nil
// keeps the idiomatic `if hasCurrent then` working — see FireIdSuccess).
//
// Also `UPDATE_INVENTORY_DURABILITY` (no payload) — fires when any
// equipped item's durability changes (combat damage, death, repairs) —
// via a co-hook on the engine's inventory-alerts recompute
// (FUN_INVENTORY_ALERTS_RECOMPUTE): the engine itself calls that whenever
// an owned item's fields change (its item post-update handler FUN_005D9A90
// routes every item values-update through it) and on equip/enter-world
// paths, so it IS the engine's own "durability may have changed" signal.
// Post-original we diff a per-slot {itemGuid, durability} snapshot and
// fire only on a real change. NOT done with bank-1 (ITEM field) descriptor
// observers: those register without error but are never invoked — the
// registrar's internal wrapper lookup (FUN_00464890, the observer
// registry, distinct from the object manager) silently no-ops for item
// objects, verified empirically (item values-updates arrive and the
// player-bank observers fire, item-bank ones never do).
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
#include "equipmentset/Locations.h"
#include "event/Custom.h"
#include "item/CGItem.h"
#include "object/Resolve.h"
#include "player/StatSignal.h"
#include "tick/WorldTick.h"
#include "unit/Identity.h"

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
// observer fires. The player is resolved from the GUID the observer
// callback receives, via the NON-THROWING object resolver — the same call
// FUN_004F8DB0 makes. NOT via FUN_RESOLVE_UNIT_TOKEN("player"): that's the
// assert-frame helper that raises a Lua error when the token doesn't
// resolve, and this runs inside SMSG_UPDATE_OBJECT processing, where the
// player object can be mid-create and momentarily unresolvable — a Lua
// error thrown from there unwinds through raw engine code.
bool SlotHasItem(uint32_t guidLo, uint32_t guidHi, int slot0Based) {
    auto *player = static_cast<const uint8_t *>(Object::ByGuid(
        Offsets::TYPEMASK_PLAYER,
        (static_cast<uint64_t>(guidHi) << 32) | guidLo, "ClassicAPI", 0x172));
    if (player == nullptr)
        return false;
    const uint8_t *invMgr = player + Offsets::OFF_PLAYER_INVENTORY_MANAGER;
    const uint32_t count = *reinterpret_cast<const uint32_t *>(
        invMgr + Offsets::OFF_INVMGR_SLOT_COUNT);
    if (static_cast<uint32_t>(slot0Based) >= count)
        return false;
    const uint64_t *guids = *reinterpret_cast<const uint64_t *const *>(
        invMgr + Offsets::OFF_INVMGR_GUID_ARRAY);
    return guids != nullptr && guids[slot0Based] != 0;
}

// --- PLAYER_EQUIPMENT_CHANGED (player-descriptor observers) -------------

// Descriptor-field observer callback — ABI per the Offsets.h note:
// __fastcall(fieldOffset /*ecx*/, size /*edx*/, guidLo, guidHi, oldValue*,
// userArg), return 1, callee cleans 0x10. Invoked by the dispatcher only
// when the watched equipment GUID field actually changed.
int __fastcall EquipSlotChanged_cb(uint32_t fieldOffset, uint32_t /*size*/,
                                   uint32_t guidLo, uint32_t guidHi,
                                   const uint32_t * /*oldValue*/,
                                   void * /*userArg*/) {
    const int slot0 =
        static_cast<int>(fieldOffset - Offsets::OFF_DESC_PLAYER_EQUIP_FIRST) >> 3;
    if (slot0 >= 0 && slot0 < Offsets::DESC_PLAYER_EQUIP_SLOTS) {
        Event::Custom::FireIdSuccess(_r.Slot(), slot0 + 1,
                                     SlotHasItem(guidLo, guidHi, slot0));
        // Gear swap changes item +healing and Spirit/Armor — invalidate the
        // GetSpellBonusHealing cache.
        Player::StatSignal::Notify();
        // Mark only; the fire is merged onto the next WorldTick so one
        // action that changes two weapon slots yields one event.
        if (IsWeaponSlot(slot0))
            g_weaponDirty = true;
    }
    return 1;
}

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

// Co-hook on the engine's inventory observer setup: after it registers the
// bag-range observers, register ours for the equipment range it skips.
// Same registrar, same shape — only the field offsets differ.
using RegisterObserver_t = void(__fastcall *)(
    int bank, uint32_t fieldOffset, uint32_t guidLo, uint32_t guidHi, int size,
    const void *callback, void *userArg1, void *userArg2);
using UnregisterObserver_t = void(__fastcall *)(int bank, uint32_t fieldOffset,
                                                uint32_t guidLo, uint32_t guidHi,
                                                const void *callback,
                                                void *userArg1);
using ObserverSetup_t = void(__fastcall *)();
ObserverSetup_t g_origSetup = nullptr;

// One-time (per process) seed latch for the durability diff below — the
// very first recompute seeds the snapshot silently instead of firing for
// the initial gear population. Deliberately NOT reset per enter-world:
// death/release/graveyard teleports re-run the setup, and a per-setup
// reset made the post-teleport recompute re-seed silently — swallowing
// the death's own durability loss (verified in-game: repairs fired,
// deaths didn't). Keeping the snapshot across loading screens means
// changes that land around them diff normally; the cost is at most one
// spurious fire on relog to a different character, which matches
// modern's login-time fire anyway.
bool g_durSnapValid = false;

void __fastcall ObserverSetup_h() {
    g_origSetup();

    const uint64_t guid = Unit::Identity::PlayerGuid();
    if (guid == 0)
        return;
    const uint32_t lo = static_cast<uint32_t>(guid);
    const uint32_t hi = static_cast<uint32_t>(guid >> 32);
    auto reg = reinterpret_cast<RegisterObserver_t>(
        Offsets::FUN_DESC_OBSERVER_REGISTER);
    auto unreg = reinterpret_cast<UnregisterObserver_t>(
        Offsets::FUN_DESC_OBSERVER_UNREGISTER);
    for (int slot = 0; slot < Offsets::DESC_PLAYER_EQUIP_SLOTS; ++slot) {
        const uint32_t field = Offsets::OFF_DESC_PLAYER_EQUIP_FIRST + slot * 8;
        // Drop any observer we already registered for this field before adding
        // a fresh one. The setup re-runs on every enter-world, including
        // /reload — but /reload does NOT destroy the CGPlayer object, so our
        // previous nodes are still attached and the registrar always APPENDS
        // (no dedup by callback). Without this, each /reload stacks another
        // observer per slot → PLAYER_EQUIPMENT_CHANGED fires N times after N
        // reloads. On logout/login the object is recreated and the old nodes
        // died with it, so the unregister no-ops there. Same objects-survive-
        // /reload hazard `Unit::TokenObserver` guards against for nameplates.
        unreg(Offsets::DESC_OBSERVER_BANK_PLAYER, field, lo, hi,
              reinterpret_cast<const void *>(&EquipSlotChanged_cb), nullptr);
        reg(Offsets::DESC_OBSERVER_BANK_PLAYER, field, lo, hi,
            /*size*/ 8, reinterpret_cast<const void *>(&EquipSlotChanged_cb),
            nullptr, nullptr);
    }
}

static const Game::HookAutoRegister _setupHook{
    Offsets::FUN_INV_OBSERVER_SETUP,
    reinterpret_cast<void *>(&ObserverSetup_h),
    reinterpret_cast<void **>(&g_origSetup)};

// --- UPDATE_INVENTORY_DURABILITY (alerts-recompute co-hook) --------------

struct SlotDurability {
    uint64_t guid;       // equipped item, 0 = empty slot
    uint32_t durability; // ITEM_FIELD_DURABILITY, 0 when empty/unresolved
};
SlotDurability g_durSnap[EquipmentSet::SLOT_COUNT];

uint32_t ItemDurability(uint64_t guid) {
    if (guid == 0)
        return 0;
    const uint8_t *item = EquipmentSet::Locations::ResolveItemByGUID(guid);
    if (item == nullptr)
        return 0;
    const uint8_t *descriptor = Item::ObjectFields(item);
    if (descriptor == nullptr)
        return 0;
    return *reinterpret_cast<const uint32_t *>(
        descriptor + Offsets::OFF_DESCRIPTOR_DURABILITY);
}

using AlertsRecompute_t = void(__fastcall *)();
AlertsRecompute_t g_origAlertsRecompute = nullptr;

void __fastcall AlertsRecompute_h() {
    g_origAlertsRecompute();

    uint64_t paperdoll[EquipmentSet::SLOT_COUNT];
    EquipmentSet::Locations::SnapshotPaperdoll(paperdoll);

    bool changed = false;
    for (int i = 0; i < EquipmentSet::SLOT_COUNT; ++i) {
        const SlotDurability cur{paperdoll[i], ItemDurability(paperdoll[i])};
        if (g_durSnap[i].guid != cur.guid ||
            g_durSnap[i].durability != cur.durability) {
            changed = true;
            g_durSnap[i] = cur;
        }
    }

    if (!g_durSnapValid) {
        g_durSnapValid = true; // first recompute of the session seeds only
        return;
    }
    if (changed)
        Event::Custom::Fire(_r2.Slot(), "");
}

static const Game::HookAutoRegister _alertsHook{
    Offsets::FUN_INVENTORY_ALERTS_RECOMPUTE,
    reinterpret_cast<void *>(&AlertsRecompute_h),
    reinterpret_cast<void **>(&g_origAlertsRecompute)};

} // namespace

} // namespace Player::Equipment
