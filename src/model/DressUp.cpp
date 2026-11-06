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

// `DressUpModel:UndressSlot(slot)`, the 4.3.4 `DressUpModel:TryOn(item)`
// (DressUpModelFrame_Scripts.cpp), and retail's TryOn result, IsGeoReady,
// SetSheathed / GetSheathed and SetAutoDress / GetAutoDress.
//
// UndressSlot is one step of vanilla's own Undress (DressUpModel::ClearModel
// 0x00504490), which 4.3.4 split per slot: a component slot is cleared with
// UnequipCharacterSlot, a held lane is detached with DetachHeldItemSlot and
// zeroed. The slot map is 4.3.4's, from inventory slot to component slot.
//
// TryOn keeps vanilla's worker (TryOnItem, FUN_DRESSUPMODEL_TRY_ON_ITEM) and
// takes 4.3.4's argument forms in front of it: an item ID, an item link or
// `item:` string, a trade-skill `enchant:` link (the item its recipe makes),
// or the name of an item the player carries. Anything else shows
// ERR_NOT_EQUIPPABLE, as 4.3.4 does.
//
// Retail's hand argument is an inventory slot name, matched the way
// GetInventorySlotInfo matches it (SStrCmpI over the PaperDollItemFrame
// rows): MainHandSlot or SecondaryHandSlot. The worker picks a hand at one
// site (SITE_TRY_ON_HAND_CHOICE); a named hand sends a held item down that
// hand's path, so the worker's own eviction of an incompatible other lane
// runs, as 4.3.4's does for a forced hand.
//
// The enchant glow is the player's AttachEnchantVisual step on the item the
// worker hangs: FindChild at the attachment AttachItemModel used, then
// AttachItemVisuals with the enchant's ItemVisuals row, unless the item's
// display hangs its own (GetItemDescVisual's rule, and 4.3.4's
// `enchants && !visuals`). The enchant is the spellEnchantID argument, else
// the link's enchant field, as 4.3.4 passes it. The worker's cache callback
// re-runs with neither hand nor enchant, as 4.3.4's does.
//
// 4.3.4's worker (`TryOnWithVisual`, 0x00A05010) tries on what a recipe makes:
// an item with no inventory type whose spell teaches a CREATE_ITEM spell is
// swapped for the made item. Vanilla's worker shows ERR_NOT_EQUIPPABLE
// instead. `TryOnItem` adds the swap in front of the worker, for our TryOn
// and for the worker's cache-load callback, so an uncached pattern works too.
//
// TryOn's result (retail's ItemTryOnReason) is read from the same gates the
// worker passes: an uncached record is DataPending (the worker has queued the
// cache callback), a held type is installed, and an armor type needs the
// pane's component and ready byte (a shapeshifted unit has neither path).
//
// Sheathing is the unit's own AttachHandItem step on the pane's lanes: detach
// the lane, then AttachItemModel with the sheathed flag. The lanes keep no
// display row, so the pane keeps one per lane: the unit's slot-39 row when the
// pane is built, then whatever TryOnItem installs. Until SetSheathed is called
// the pane keeps vanilla's placement (the unit's own, then the hand for every
// try-on). Each lane keeps its glow too, so a re-hung weapon gets it back: a
// player's from GetItemDescVisual on its visible item (only players glow in
// vanilla), a try-on's from its enchant.
//
// Auto-dress off undresses the pane with ClearModel right after
// SetModelInstance clones the unit, unless Dress() is the one rebuilding it.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "Common.h"
#include "item/Arg.h"
#include "item/Dressable.h"
#include "item/ID.h"
#include "item/Location.h"
#include "item/Record.h"
#include "model/ItemDisplay.h"
#include "model/M2.h"
#include "object/Resolve.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace Model::DressUp {
namespace {

using UnequipSlot_t = void(__thiscall *)(void *component, uint32_t slot);
using DetachHeldItem_t = void(__fastcall *)(void *model, uint32_t bodySlot, uint32_t sheatheType,
                                            bool shield);
using AttachItemModel_t = int(__fastcall *)(void *model, const void *record, uint32_t bodySlot,
                                            uint32_t sheatheType, bool sheathed, bool shield,
                                            bool ranged);
using TryOnItem_t = void(__thiscall *)(void *pane, uint32_t itemID);
using SetModelInstance_t = void(__thiscall *)(void *pane, void *model);
using ClearModel_t = void(__thiscall *)(void *pane);
using GetHeldItemDisplay_t = const void *(__thiscall *)(void *unit, uint32_t hand);
using FindChild_t = void *(__thiscall *)(void *model, int attachId);
using GetVisibleItem_t = const void *(__thiscall *)(void *player, uint32_t slot);
using GetDescVisual_t = int(__thiscall *)(const void *visibleItem);
using DisplayError_t = void(__cdecl *)(uint32_t id, ...);

// Body slots of the two held lanes.
constexpr uint32_t kBodySlotMainHand = 0xF;
constexpr uint32_t kBodySlotOffHand = 0x10;

// Retail's Enum.ItemTryOnReason.
constexpr int kTryOnSuccess = 0;
constexpr int kTryOnWrongRace = 1;
constexpr int kTryOnNotEquippable = 2;
constexpr int kTryOnDataPending = 3;

// A held lane (OFF_DRESSUPMODEL_*_HAND_LANE).
struct HeldLane {
    uint8_t itemClass; // 0 = empty
    uint8_t unknown1[2];
    uint8_t inventoryType;
    uint8_t sheatheType;
    uint8_t unknown5[3];
};
static_assert(sizeof(HeldLane) == 8, "ClearModel zeroes 8 bytes per lane");

struct Lane {
    uintptr_t offset;
    uint32_t bodySlot;
};
constexpr Lane kLanes[2] = {
    {Offsets::OFF_DRESSUPMODEL_MAIN_HAND_LANE, kBodySlotMainHand},
    {Offsets::OFF_DRESSUPMODEL_OFF_HAND_LANE, kBodySlotOffHand},
};

struct PaneState {
    bool autoDress = true;
    bool sheathSet = false; // SetSheathed was called; weapons follow the two below
    bool sheathed = false;
    bool hideWeapons = false;
    const void *laneDisplay[2] = {}; // ItemDisplayInfo row on each lane, or null
    uint32_t laneVisual[2] = {};     // enchant ItemVisuals row on each lane, or 0
};
std::unordered_map<void *, PaneState> g_panes;

void *g_tryOnPane = nullptr;  // the pane whose TryOnItem worker is running
uint32_t g_tryOnBodySlot = 0; // the hand TryOn named (kBodySlot*), or 0
uint32_t g_tryOnEnchant = 0;  // the SpellItemEnchantment TryOn gave, or 0
bool g_dressing = false;      // Dress() is rebuilding the pane

uintptr_t g_mainHandPath = Offsets::TRY_ON_MAIN_HAND_PATH;
uintptr_t g_offHandPath = Offsets::TRY_ON_OFF_HAND_PATH;

HeldLane &LaneOf(void *pane, int i) {
    return Game::Ref<HeldLane>(pane, kLanes[i].offset);
}

int LaneIndex(uint32_t bodySlot) {
    return bodySlot == kBodySlotOffHand ? 1 : 0;
}

// The glow `enchantID` gives an item shown with `display`: none when the
// display has its own.
uint32_t EnchantVisual(const void *display, uint32_t enchantID) {
    if (display == nullptr ||
        Model::ItemDisplay::OwnVisual(static_cast<const uint8_t *>(display)) != 0)
        return 0;
    const uint8_t *enchant = DBC::Record(Offsets::VAR_SPELLITEMENCHANT_RECORDS,
                                         Offsets::VAR_SPELLITEMENCHANT_COUNT, enchantID);
    return enchant != nullptr
               ? Game::Read<uint32_t>(enchant, Offsets::OFF_SPELLITEMENCHANT_ITEM_VISUAL)
               : 0;
}

// AttachEnchantVisual's step (0x005EED50): the glow on the item hung at
// `attachId`.
void HangVisual(void *model, int attachId, uint32_t visual) {
    if (visual == 0 || attachId < 0)
        return;
    if (void *item = reinterpret_cast<FindChild_t>(Offsets::FUN_M2MODEL_FIND_CHILD)(model,
                                                                                   attachId))
        M2::AttachItemVisuals(item, visual);
}

// DetachHeldItemSlot on lane `i`'s item.
void DetachLane(void *model, int i, const HeldLane &lane) {
    reinterpret_cast<DetachHeldItem_t>(Offsets::FUN_DETACH_HELD_ITEM_SLOT)(
        model, kLanes[i].bodySlot, lane.sheatheType,
        lane.inventoryType == Offsets::INVTYPE_SHIELD);
}

// The unit's AttachHandItem step on each occupied lane: detach it, then hang
// it in the hand or the sheath, or leave it off.
void ApplyWeapons(void *pane, const PaneState &s) {
    void *model = Pane::Model(pane);
    if (model == nullptr)
        return;
    for (int i = 0; i < 2; ++i) {
        const HeldLane &lane = LaneOf(pane, i);
        if (lane.itemClass == 0 || s.laneDisplay[i] == nullptr)
            continue;
        DetachLane(model, i, lane);
        if (s.hideWeapons)
            continue;
        const int attachId = reinterpret_cast<AttachItemModel_t>(Offsets::FUN_ATTACH_ITEM_MODEL)(
            model, s.laneDisplay[i], kLanes[i].bodySlot, lane.sheatheType, s.sheathed,
            lane.inventoryType == Offsets::INVTYPE_SHIELD,
            lane.inventoryType == Offsets::INVTYPE_RANGEDRIGHT);
        HangVisual(model, attachId, s.laneVisual[i]);
    }
}

// Slot 36, after the original has cloned the unit and copied its lanes.
void __fastcall SetModelInstance(void *pane, void * /*edx*/, void *model) {
    reinterpret_cast<SetModelInstance_t>(Offsets::FUN_DRESSUPMODEL_SET_MODEL_INSTANCE)(pane,
                                                                                      model);
    if (model == nullptr)
        return;
    PaneState &s = g_panes[pane];
    s.laneDisplay[0] = s.laneDisplay[1] = nullptr;
    s.laneVisual[0] = s.laneVisual[1] = 0;
    // The original filled the lanes from this unit, resolved the same way.
    const uint64_t guid = Game::Read<uint64_t>(pane, Offsets::OFF_PLAYERMODEL_UNIT_GUID);
    if (void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid)) {
        const auto get = Game::Read<GetHeldItemDisplay_t>(
            Game::Read<const void *>(unit, 0), Offsets::VTBL_UNIT_GET_HELD_ITEM_DISPLAY);
        s.laneDisplay[0] = get(unit, 0);
        s.laneDisplay[1] = get(unit, 1);
    }
    // AttachHandEnchant's glow (0x005EED00), for players only: the visible
    // item in the lane's slot (0xF / 0x10, the same numbers as the body
    // slots), and its visual unless the display hangs its own (-1).
    if (void *player = Object::ByGuid(Offsets::TYPEMASK_PLAYER, guid)) {
        for (int i = 0; i < 2; ++i) {
            const void *item = reinterpret_cast<GetVisibleItem_t>(
                Offsets::FUN_UNIT_GET_VISIBLE_ITEM)(player, kLanes[i].bodySlot);
            if (item == nullptr || Game::Read<uint32_t>(item, Offsets::OFF_VISIBLE_ITEM_ITEM_ID) == 0)
                continue;
            const int visual =
                reinterpret_cast<GetDescVisual_t>(Offsets::FUN_VISIBLE_ITEM_GET_DESC_VISUAL)(item);
            s.laneVisual[i] = visual > 0 ? static_cast<uint32_t>(visual) : 0;
        }
    }
    if (!s.autoDress && !g_dressing) {
        reinterpret_cast<ClearModel_t>(Offsets::FUN_DRESSUPMODEL_CLEAR_MODEL)(pane);
        return;
    }
    if (s.sheathSet)
        ApplyWeapons(pane, s);
}

// TryOnItem's install: record the lane's row and glow, honor SetSheathed,
// then hang the glow.
int __fastcall TryOnAttach(void *model, const void *record, uint32_t bodySlot,
                           uint32_t sheatheType, bool sheathed, bool shield, bool ranged) {
    const auto attach = reinterpret_cast<AttachItemModel_t>(Offsets::FUN_ATTACH_ITEM_MODEL);
    if (g_tryOnPane == nullptr)
        return attach(model, record, bodySlot, sheatheType, sheathed, shield, ranged);
    PaneState &s = g_panes[g_tryOnPane];
    const int lane = LaneIndex(bodySlot);
    s.laneDisplay[lane] = record;
    s.laneVisual[lane] = EnchantVisual(record, g_tryOnEnchant);
    if (s.sheathSet) {
        if (s.hideWeapons)
            return -1;
        sheathed = s.sheathed;
    }
    const int attachId = attach(model, record, bodySlot, sheatheType, sheathed, shield, ranged);
    HangVisual(model, attachId, s.laneVisual[lane]);
    return attachId;
}

// TryOnItem's eviction of the other lane. The worker leaves that lane's
// record in place, so the pane drops its row to keep SetSheathed from
// hanging the evicted item again.
void __fastcall TryOnEvict(void *model, uint32_t bodySlot, uint32_t sheatheType, bool shield) {
    if (g_tryOnPane != nullptr) {
        PaneState &s = g_panes[g_tryOnPane];
        const int lane = LaneIndex(bodySlot);
        s.laneDisplay[lane] = nullptr;
        s.laneVisual[lane] = 0;
    }
    reinterpret_cast<DetachHeldItem_t>(Offsets::FUN_DETACH_HELD_ITEM_SLOT)(model, bodySlot,
                                                                         sheatheType, shield);
}

// TryOnItem's hand choice. With a hand named, the worker takes that hand's
// path; otherwise the thunk does the instructions it replaced and the
// worker's `jnz` reads their flags.
__declspec(naked) void HandChoiceThunk() {
    __asm {
        cmp dword ptr [g_tryOnBodySlot], 0Fh
        je mainHand
        cmp dword ptr [g_tryOnBodySlot], 10h
        je offHand
        mov edx, dword ptr [edi + 2Ch]
        cmp edx, 0Dh
        ret
    mainHand:
        add esp, 4
        jmp dword ptr [g_mainHandPath]
    offHand:
        add esp, 4
        jmp dword ptr [g_offHandPath]
    }
}

// ClearModel's lane step: detach the held item and zero the lane.
void ClearLane(void *pane, int i) {
    HeldLane &lane = LaneOf(pane, i);
    if (lane.itemClass == 0)
        return;
    DetachLane(Pane::Model(pane), i, lane);
    lane = HeldLane{};
}

// Component slot for a 0-based inventory slot, or -1 (4.3.4 UndressSlot).
int ComponentSlot(int slot0) {
    switch (slot0) {
    case 0: return 0;  // head
    case 2: return 1;  // shoulder
    case 3: return 2;  // shirt
    case 4: return 3;  // chest
    case 5: return 4;  // waist
    case 6: return 5;  // legs
    case 7: return 6;  // feet
    case 8: return 7;  // wrist
    case 9: return 8;  // hands
    case 14: return 10; // back
    case 18: return 9;  // tabard
    default: return -1;
    }
}

int __fastcall Script_UndressSlot(void *L) {
    void *pane = Game::Lua::ResolveDressUpModel(L);
    if (pane == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:UndressSlot(slotIndex)", Game::Lua::ObjectDisplayName(pane));
        return 0;
    }
    const int slot0 = static_cast<int>(Game::Lua::ToNumber(L, 2)) - 1;
    void *component = Game::Read<void *>(pane, Offsets::OFF_DRESSUPMODEL_COMPONENT);
    if (Pane::Model(pane) == nullptr || component == nullptr)
        return 0;
    if (slot0 + 1 == Offsets::INVSLOT_MAINHAND) {
        ClearLane(pane, 0);
    } else if (slot0 + 1 == Offsets::INVSLOT_OFFHAND) {
        ClearLane(pane, 1);
    } else {
        const int compSlot = ComponentSlot(slot0);
        if (compSlot >= 0)
            reinterpret_cast<UnequipSlot_t>(Offsets::FUN_CHARCOMP_UNEQUIP_SLOT)(
                component, static_cast<uint32_t>(compSlot));
    }
    return 0;
}

// The item a trade-skill recipe makes, from an `enchant:<spellID>` link: a
// TRADESPELL whose first effect makes an item (4.3.4's test).
uint32_t RecipeItem(const char *link) {
    const char *enchant = std::strstr(link, "enchant:");
    if (enchant == nullptr)
        return 0;
    const long spellID = std::strtol(enchant + 8, nullptr, 10);
    if (spellID <= 0)
        return 0;
    const uint8_t *spell = DBC::Record(Offsets::VAR_SPELL_RECORDS, Offsets::VAR_SPELL_RECORD_COUNT,
                                       static_cast<uint32_t>(spellID));
    if (spell == nullptr ||
        (Game::Read<uint32_t>(spell, Offsets::OFF_SPELL_RECORD_ATTRIBUTES) &
         Offsets::SPELL_ATTR_TRADESPELL) == 0)
        return 0;
    return Game::Read<uint32_t>(spell, Offsets::OFF_SPELL_RECORD_EFFECT_ITEM_TYPE);
}

// The item the worker is given: a cached recipe item becomes what it makes.
uint32_t WornItem(uint32_t itemID) {
    const uint8_t *record = Item::PeekRecord(itemID);
    if (record != nullptr &&
        Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE) == 0) {
        const uint32_t made = Item::Dressable::RecipeItem(record);
        if (made != 0)
            return made;
    }
    return itemID;
}

// `__thiscall(pane, itemID)` as `__fastcall` with the EDX slot unused, so it
// can stand at the worker's call sites.
void __fastcall TryOnItem(void *pane, void *, uint32_t itemID) {
    g_tryOnPane = pane;
    reinterpret_cast<TryOnItem_t>(Offsets::FUN_DRESSUPMODEL_TRY_ON_ITEM)(pane, WornItem(itemID));
    g_tryOnPane = nullptr;
}

// The lane a hand name picks: the PaperDollItemFrame row it names
// (GetInventorySlotInfo's match), if that row is a held slot; else 0.
uint32_t NamedHand(const char *name) {
    const auto *records = Game::Read<const uint8_t *>(Offsets::VAR_PAPERDOLLITEMFRAME_RECORDS);
    const int count = Game::Read<int>(Offsets::VAR_PAPERDOLLITEMFRAME_COUNT);
    if (records == nullptr)
        return 0;
    for (int i = 0; i < count; ++i) {
        const uint8_t *row = records + i * Offsets::OFF_PAPERDOLLITEMFRAME_RECORD_STRIDE;
        if (Game::StrCmpI(Game::Read<const char *>(row, Offsets::OFF_PAPERDOLLITEMFRAME_SLOT_NAME),
                          name) != 0)
            continue;
        switch (Game::Read<int>(row, Offsets::OFF_PAPERDOLLITEMFRAME_SLOT_NUMBER)) {
        case Offsets::INVSLOT_MAINHAND: return kBodySlotMainHand;
        case Offsets::INVSLOT_OFFHAND: return kBodySlotOffHand;
        default: return 0;
        }
    }
    return 0;
}

// What the worker did with `itemID`, from the gates it passes (0x00504540).
int TryOnResult(void *pane, uint32_t itemID) {
    const uint8_t *record = Item::PeekRecord(itemID);
    if (record == nullptr)
        return kTryOnDataPending;
    if (Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID) == 0 ||
        Pane::Model(pane) == nullptr)
        return kTryOnNotEquippable;
    switch (Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE)) {
    case Offsets::INVTYPE_HEAD:
    case Offsets::INVTYPE_SHOULDER:
    case Offsets::INVTYPE_BODY:
    case Offsets::INVTYPE_CHEST:
    case Offsets::INVTYPE_WAIST:
    case Offsets::INVTYPE_LEGS:
    case Offsets::INVTYPE_FEET:
    case Offsets::INVTYPE_WRIST:
    case Offsets::INVTYPE_HAND:
    case Offsets::INVTYPE_CLOAK:
    case Offsets::INVTYPE_TABARD:
    case Offsets::INVTYPE_ROBE:
        // The armor arms: the component and the ready byte.
        return Game::Read<void *>(pane, Offsets::OFF_DRESSUPMODEL_COMPONENT) != nullptr &&
                       Game::Read<uint8_t>(pane, Offsets::OFF_DRESSUPMODEL_READY) != 0
                   ? kTryOnSuccess
                   : kTryOnNotEquippable;
    case Offsets::INVTYPE_WEAPON:
    case Offsets::INVTYPE_SHIELD:
    case Offsets::INVTYPE_RANGED:
    case Offsets::INVTYPE_2HWEAPON:
    case Offsets::INVTYPE_WEAPONMAINHAND:
    case Offsets::INVTYPE_WEAPONOFFHAND:
    case Offsets::INVTYPE_HOLDABLE:
    case Offsets::INVTYPE_THROWN:
    case Offsets::INVTYPE_RANGEDRIGHT:
        return kTryOnSuccess; // the held arm
    default:
        return kTryOnNotEquippable;
    }
}

int __fastcall Script_TryOn(void *L) {
    void *pane = Game::Lua::ResolveDressUpModel(L);
    if (pane == nullptr)
        return 0;
    // Read before the name search below, which stomps the Lua stack.
    const uint32_t bodySlot = Game::Lua::Type(L, 3) == Game::Lua::TYPE_STRING
                                  ? NamedHand(Game::Lua::ToString(L, 3))
                                  : 0;
    const bool enchantGiven = Game::Lua::IsNumber(L, 4) != 0;
    uint32_t enchant = enchantGiven ? static_cast<uint32_t>(Game::Lua::ToNumber(L, 4)) : 0;
    uint32_t itemID = 0;
    if (Game::Lua::Type(L, 2) == Game::Lua::TYPE_NUMBER) {
        itemID = static_cast<uint32_t>(Game::Lua::ToNumber(L, 2));
    } else if (Game::Lua::Type(L, 2) == Game::Lua::TYPE_STRING) {
        // Copied: the name search below stomps the Lua stack.
        char s[256];
        std::snprintf(s, sizeof(s), "%s", Game::Lua::ToString(L, 2));
        if (std::strstr(s, "enchant:") != nullptr) {
            itemID = RecipeItem(s);
        } else {
            // An item link, `item:` string or numeric string resolves to an ID;
            // any other string is the name of an item the player carries.
            const Item::Arg::Resolved arg = Item::Arg::ResolveString(s);
            Item::Location::ByGUIDResult found{};
            if (arg.itemID != 0) {
                itemID = static_cast<uint32_t>(arg.itemID);
                if (!enchantGiven)
                    enchant = static_cast<uint32_t>(arg.enchant);
            } else if (arg.name != nullptr && Item::Location::FindByArg(L, arg, &found))
                itemID = static_cast<uint32_t>(Item::ID::FromCGItem(found.item));
        }
    }
    if (itemID == 0) {
        reinterpret_cast<DisplayError_t>(Offsets::FUN_GAMEUI_DISPLAY_ERROR)(
            Offsets::GAME_ERROR_NOT_EQUIPPABLE);
        Game::Lua::PushNumber(L, kTryOnNotEquippable);
        return 1;
    }
    g_tryOnBodySlot = bodySlot;
    g_tryOnEnchant = enchant;
    TryOnItem(pane, nullptr, itemID);
    g_tryOnBodySlot = g_tryOnEnchant = 0;
    Game::Lua::PushNumber(L, TryOnResult(pane, WornItem(itemID)));
    return 1;
}

int __fastcall Script_Dress(void *L) {
    g_dressing = true;
    const int n = reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_DRESSUPMODEL_DRESS)(L);
    g_dressing = false;
    return n;
}

int __fastcall Script_IsGeoReady(void *L) {
    void *pane = Game::Lua::ResolveDressUpModel(L);
    if (pane == nullptr)
        return 0;
    void *model = Pane::Model(pane);
    Game::Lua::PushBool(L, model != nullptr && M2::IsLoaded(model, 0, 1));
    return 1;
}

int __fastcall Script_SetSheathed(void *L) {
    void *pane = Game::Lua::ResolveDressUpModel(L);
    if (pane == nullptr)
        return 0;
    PaneState &s = g_panes[pane];
    s.sheathSet = true;
    s.sheathed = Game::Lua::ToBoolean(L, 2) != 0;
    s.hideWeapons = Game::Lua::ToBoolean(L, 3) != 0;
    ApplyWeapons(pane, s);
    return 0;
}

int __fastcall Script_GetSheathed(void *L) {
    void *pane = Game::Lua::ResolveDressUpModel(L);
    if (pane == nullptr)
        return 0;
    const auto it = g_panes.find(pane);
    Game::Lua::PushBool(L, it != g_panes.end() && it->second.sheathed);
    return 1;
}

int __fastcall Script_SetAutoDress(void *L) {
    if (void *pane = Game::Lua::ResolveDressUpModel(L))
        g_panes[pane].autoDress = Game::Lua::ToBoolean(L, 2) != 0;
    return 0;
}

int __fastcall Script_GetAutoDress(void *L) {
    void *pane = Game::Lua::ResolveDressUpModel(L);
    if (pane == nullptr)
        return 0;
    const auto it = g_panes.find(pane);
    Game::Lua::PushBool(L, it == g_panes.end() || it->second.autoDress);
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"UndressSlot", &Script_UndressSlot},
    {"TryOn", &Script_TryOn},
    {"Dress", &Script_Dress},
    {"IsGeoReady", &Script_IsGeoReady},
    {"SetSheathed", &Script_SetSheathed},
    {"GetSheathed", &Script_GetSheathed},
    {"SetAutoDress", &Script_SetAutoDress},
    {"GetAutoDress", &Script_GetAutoDress},
};

const Game::Doc::Field kUndressArgs[] = {
    Game::Doc::Req("slot", "number", "An inventory slot, 1 (head) to 19 (tabard)."),
};
const Game::Doc::Function kUndressSlot{
    "Removes the item in one inventory slot from the model.", kUndressArgs, {}};
const Game::Doc::Field kTryOnArgs[] = {
    Game::Doc::Req("item", "ItemInfo",
                   "An item ID, item link, trade-skill recipe link, or the name of an item you carry."),
    Game::Doc::Opt("handSlotName", "cstring", nullptr,
                   "MAINHANDSLOT or SECONDARYHANDSLOT: the hand that holds the item."),
    Game::Doc::Opt("spellEnchantID", "number", nullptr,
                   "The enchant whose glow a weapon shows. Defaults to the item link's enchant."),
};
const Game::Doc::Field kTryOnRets[] = {
    Game::Doc::Req("result", "ItemTryOnReason"),
};
const Game::Doc::Function kTryOn{"Puts an item on the model.", kTryOnArgs, kTryOnRets};
const Game::Doc::Function kDress{"Dresses the model in its unit's equipment.", {}, {}};
const Game::Doc::Field kGeoReadyRets[] = {
    Game::Doc::Req("ready", "bool"),
};
const Game::Doc::Function kIsGeoReady{
    "Returns whether the model and the item models on it have loaded.", {}, kGeoReadyRets};
const Game::Doc::Field kSheathedArgs[] = {
    Game::Doc::Opt("sheathed", "bool", "false"),
    Game::Doc::Opt("hideWeapons", "bool", "false"),
};
const Game::Doc::Function kSetSheathed{
    "Puts the model's weapons in its hands, in their sheaths, or hides them.", kSheathedArgs,
    {}};
const Game::Doc::Field kSheathedRets[] = {
    Game::Doc::Req("sheathed", "bool"),
};
const Game::Doc::Function kGetSheathed{"Returns whether the model's weapons are sheathed.", {},
                                       kSheathedRets};
const Game::Doc::Field kEnabledArgs[] = {
    Game::Doc::Opt("enabled", "bool", "false"),
};
const Game::Doc::Function kSetAutoDress{
    "Sets whether the model wears its unit's equipment when the unit is set.", kEnabledArgs, {}};
const Game::Doc::Field kEnabledRets[] = {
    Game::Doc::Req("enabled", "bool"),
};
const Game::Doc::Function kGetAutoDress{
    "Returns whether the model wears its unit's equipment when the unit is set.", {},
    kEnabledRets};
const Game::Doc::Method g_methodDocs[] = {
    {"UndressSlot", &kUndressSlot},   {"TryOn", &kTryOn},
    {"Dress", &kDress},               {"IsGeoReady", &kIsGeoReady},
    {"SetSheathed", &kSetSheathed},   {"GetSheathed", &kGetSheathed},
    {"SetAutoDress", &kSetAutoDress}, {"GetAutoDress", &kGetAutoDress},
};

const Game::Lua::EnumIntegerEntry kItemTryOnReason[] = {
    {"Success", kTryOnSuccess},
    {"WrongRace", kTryOnWrongRace},
    {"NotEquippable", kTryOnNotEquippable},
    {"DataPending", kTryOnDataPending},
};

void RegisterLuaFunctions() {
    Common::RedirectCall(Offsets::CALL_TRY_ON_ITEM_CACHED, Offsets::FUN_DRESSUPMODEL_TRY_ON_ITEM,
                         reinterpret_cast<const void *>(&TryOnItem));
    Common::RedirectCall(Offsets::CALL_TRY_ON_ATTACH_ITEM, Offsets::FUN_ATTACH_ITEM_MODEL,
                         reinterpret_cast<const void *>(&TryOnAttach));
    Common::RedirectCall(Offsets::CALL_TRY_ON_EVICT_OFF_HAND, Offsets::FUN_DETACH_HELD_ITEM_SLOT,
                         reinterpret_cast<const void *>(&TryOnEvict));
    Common::RedirectCall(Offsets::CALL_TRY_ON_EVICT_MAIN_HAND, Offsets::FUN_DETACH_HELD_ITEM_SLOT,
                         reinterpret_cast<const void *>(&TryOnEvict));
    static const uint8_t kHandChoice[6] = {0x8B, 0x57, 0x2C, 0x83, 0xFA, 0x0D};
    Common::ReplaceWithCall(Offsets::SITE_TRY_ON_HAND_CHOICE, kHandChoice, sizeof kHandChoice,
                            reinterpret_cast<const void *>(&HandChoiceThunk));
    Common::RedirectSlot(Offsets::SLOT_DRESSUPMODEL_SET_MODEL_INSTANCE,
                         Offsets::FUN_DRESSUPMODEL_SET_MODEL_INSTANCE,
                         reinterpret_cast<const void *>(&SetModelInstance));
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_DRESSUPMODEL_METHOD_REGISTRY), g_methods,
        g_methodDocs);
    Game::Lua::RegisterIntegerEnum("Enum", "ItemTryOnReason", kItemTryOnReason, "UIModelInfo");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// Frames are rebuilt on /reload and a new frame can reuse an old address.
void PrepareForReload() {
    g_panes.clear();
}

const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

} // namespace
} // namespace Model::DressUp
