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
// That step, the lanes and the weapon hanging below are Model::Outfit's,
// which the ModelScene actor shares.
//
// TryOn keeps vanilla's worker (TryOnItem, FUN_DRESSUPMODEL_TRY_ON_ITEM) and
// takes the arguments Model::TryOn reads in front of it. No item shows
// ERR_NOT_EQUIPPABLE, as 4.3.4 does.
//
// The worker picks a hand at one site (SITE_TRY_ON_HAND_CHOICE); a named hand
// sends a held item down that hand's path, so the worker's own eviction of an
// incompatible other lane runs, as 4.3.4's does for a forced hand.
//
// The enchant glow is the player's AttachEnchantVisual step on the item the
// worker hangs: FindChild at the attachment AttachItemModel used, then
// AttachItemVisuals with the enchant's ItemVisuals row, unless the item's
// display hangs its own (GetItemDescVisual's rule, and 4.3.4's
// `enchants && !visuals`). The enchant is the spellEnchantID argument, else
// the link's enchant field, as 4.3.4 passes it. The worker's cache callback
// re-runs with neither hand nor enchant, as 4.3.4's does.
//
// Vanilla's worker shows ERR_NOT_EQUIPPABLE for a recipe, where 4.3.4's
// tries on what it makes. `TryOnItem` adds that swap (Model::TryOn::WornItem)
// in front of the worker, for our TryOn and for the worker's cache-load
// callback, so an uncached pattern works too.
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
//
// The ItemTransmogInfo methods are the ModelScene actor's (Outfit::Worn, the
// item ID as the appearance, the glowing enchant as the illusion) on what the
// pane wears: the unit's visible items when SetModelInstance builds it, then
// each slot the worker fills (an armor arm's component slot, or the lane
// TryOnAttach hangs) and each lane it evicts. A read first drops every slot
// the pane no longer shows (Outfit::TrimWorn), which covers the engine's own
// Undress. SetItemTransmogInfo is TryOn without the worker's
// ERR_NOT_EQUIPPABLE: the worker shows it only in its default arm, once the
// record is cached, so a quiet try-on of such an item skips the worker, then
// or when its cache callback runs.

#include "Game.h"
#include "Offsets.h"
#include "Common.h"
#include "item/Record.h"
#include "model/M2.h"
#include "model/Outfit.h"
#include "model/TryOn.h"
#include "object/Resolve.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace Model::DressUp {
namespace {

using DetachHeldItem_t = void(__fastcall *)(void *model, uint32_t bodySlot, uint32_t sheatheType,
                                            bool shield);
using AttachItemModel_t = int(__fastcall *)(void *model, const void *record, uint32_t bodySlot,
                                            uint32_t sheatheType, bool sheathed, bool shield,
                                            bool ranged);
using TryOnItem_t = void(__thiscall *)(void *pane, uint32_t itemID);
using SetModelInstance_t = void(__thiscall *)(void *pane, void *model);
using ClearModel_t = void(__thiscall *)(void *pane);

static_assert(Offsets::OFF_DRESSUPMODEL_OFF_HAND_LANE ==
                  Offsets::OFF_DRESSUPMODEL_MAIN_HAND_LANE + sizeof(Outfit::HeldLane),
              "the pane's two lanes are one Outfit::Lanes");

struct PaneState {
    bool autoDress = true;
    bool sheathSet = false; // SetSheathed was called; weapons follow the two below
    bool sheathed = false;
    bool hideWeapons = false;
    Outfit::Weapons weapons;
    Outfit::Worn worn = {};
    std::unordered_set<uint32_t> quietPending; // quiet try-ons waiting for their record
};
std::unordered_map<void *, PaneState> g_panes;

void *g_tryOnPane = nullptr;  // the pane whose TryOnItem worker is running
uint32_t g_tryOnBodySlot = 0; // the hand TryOn named (kBodySlot*), or 0
uint32_t g_tryOnEnchant = 0;  // the SpellItemEnchantment TryOn gave, or 0
bool g_tryOnQuiet = false;    // the try-on shows no ERR_NOT_EQUIPPABLE
uint32_t g_tryOnItem = 0;     // the item the worker is putting on
bool g_dressing = false;      // Dress() is rebuilding the pane

uintptr_t g_mainHandPath = Offsets::TRY_ON_MAIN_HAND_PATH;
uintptr_t g_offHandPath = Offsets::TRY_ON_OFF_HAND_PATH;

Outfit::Lanes &LanesOf(void *pane) {
    return Game::Ref<Outfit::Lanes>(pane, Offsets::OFF_DRESSUPMODEL_MAIN_HAND_LANE);
}

void ApplyWeapons(void *pane, const PaneState &s) {
    Outfit::HangWeapons(Pane::Model(pane), LanesOf(pane), s.weapons, s.sheathed, s.hideWeapons);
}

// Slot 36, after the original has cloned the unit and copied its lanes.
void __fastcall SetModelInstance(void *pane, void * /*edx*/, void *model) {
    reinterpret_cast<SetModelInstance_t>(Offsets::FUN_DRESSUPMODEL_SET_MODEL_INSTANCE)(pane,
                                                                                      model);
    if (model == nullptr)
        return;
    PaneState &s = g_panes[pane];
    // The original filled the lanes from this unit, resolved the same way.
    const auto guid = Game::Read<uint64_t>(pane, Offsets::OFF_PLAYERMODEL_UNIT_GUID);
    Outfit::ReadWeapons(guid, s.weapons);
    Outfit::ReadWorn(guid != 0 ? ::Object::ByGuid(Offsets::TYPEMASK_UNIT, guid) : nullptr, guid,
                     s.weapons, s.worn);
    if (!s.autoDress && !g_dressing) {
        reinterpret_cast<ClearModel_t>(Offsets::FUN_DRESSUPMODEL_CLEAR_MODEL)(pane);
        return;
    }
    if (s.sheathSet)
        ApplyWeapons(pane, s);
}

// TryOnItem's install: record the lane's row, glow and item, honor
// SetSheathed, then hang the glow.
int __fastcall TryOnAttach(void *model, const void *record, uint32_t bodySlot,
                           uint32_t sheatheType, bool sheathed, bool shield, bool ranged) {
    const auto attach = reinterpret_cast<AttachItemModel_t>(Offsets::FUN_ATTACH_ITEM_MODEL);
    if (g_tryOnPane == nullptr)
        return attach(model, record, bodySlot, sheatheType, sheathed, shield, ranged);
    PaneState &s = g_panes[g_tryOnPane];
    const int lane = Outfit::LaneIndex(bodySlot);
    s.weapons.display[lane] = record;
    s.weapons.visual[lane] = Outfit::EnchantVisual(record, g_tryOnEnchant);
    s.worn[(lane == 1 ? Offsets::INVSLOT_OFFHAND : Offsets::INVSLOT_MAINHAND) - 1] = {
        g_tryOnItem, s.weapons.visual[lane] != 0 ? g_tryOnEnchant : 0};
    if (s.sheathSet) {
        if (s.hideWeapons)
            return -1;
        sheathed = s.sheathed;
    }
    const int attachId = attach(model, record, bodySlot, sheatheType, sheathed, shield, ranged);
    Outfit::HangVisual(model, attachId, s.weapons.visual[lane]);
    return attachId;
}

// TryOnItem's eviction of the other lane. The worker leaves that lane's
// record in place, so the pane drops its row to keep SetSheathed from
// hanging the evicted item again.
void __fastcall TryOnEvict(void *model, uint32_t bodySlot, uint32_t sheatheType, bool shield) {
    if (g_tryOnPane != nullptr) {
        PaneState &s = g_panes[g_tryOnPane];
        const int lane = Outfit::LaneIndex(bodySlot);
        s.weapons.display[lane] = nullptr;
        s.weapons.visual[lane] = 0;
        s.worn[(lane == 1 ? Offsets::INVSLOT_OFFHAND : Offsets::INVSLOT_MAINHAND) - 1] = {};
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

int __fastcall Script_UndressSlot(void *L) {
    void *pane = Game::Lua::ResolveDressUpModel(L);
    if (pane == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:UndressSlot(slotIndex)", Game::Lua::ObjectDisplayName(pane));
        return 0;
    }
    Outfit::UndressSlot(Pane::Model(pane),
                        Game::Read<void *>(pane, Offsets::OFF_DRESSUPMODEL_COMPONENT),
                        LanesOf(pane), static_cast<int>(Game::Lua::ToNumber(L, 2)));
    return 0;
}

// What the worker did with `itemID`, from the gates it passes (0x00504540).
int TryOnResult(void *pane, uint32_t itemID) {
    const uint8_t *record = Item::PeekRecord(itemID);
    if (record == nullptr)
        return TryOn::kDataPending;
    if (Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID) == 0 ||
        Pane::Model(pane) == nullptr)
        return TryOn::kNotEquippable;
    switch (Outfit::ArmOf(Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE))) {
    case Outfit::Arm::Armor: // the component and the ready byte
        return Game::Read<void *>(pane, Offsets::OFF_DRESSUPMODEL_COMPONENT) != nullptr &&
                       Game::Read<uint8_t>(pane, Offsets::OFF_DRESSUPMODEL_READY) != 0
                   ? TryOn::kSuccess
                   : TryOn::kNotEquippable;
    case Outfit::Arm::Held:
        return TryOn::kSuccess;
    default:
        return TryOn::kNotEquippable;
    }
}

// `__thiscall(pane, itemID)` as `__fastcall` with the EDX slot unused, so it
// can stand at the worker's call sites. An armor arm the worker took wears
// the item in its slot.
void __fastcall TryOnItem(void *pane, void *, uint32_t itemID) {
    PaneState &s = g_panes[pane];
    const bool quiet = g_tryOnQuiet || s.quietPending.erase(itemID) != 0;
    const uint32_t item = TryOn::WornItem(itemID);
    const uint8_t *record = Item::PeekRecord(item);
    uint32_t componentSlot = 0;
    const Outfit::Arm arm =
        record != nullptr
            ? Outfit::ArmOf(Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE),
                            &componentSlot)
            : Outfit::Arm::Nothing;
    if (record == nullptr) {
        if (quiet)
            s.quietPending.insert(item);
    } else if (quiet && arm == Outfit::Arm::NotEquippable) {
        return;
    }
    g_tryOnPane = pane;
    g_tryOnItem = item;
    reinterpret_cast<TryOnItem_t>(Offsets::FUN_DRESSUPMODEL_TRY_ON_ITEM)(pane, item);
    g_tryOnPane = nullptr;
    g_tryOnItem = 0;
    if (arm == Outfit::Arm::Armor && TryOnResult(pane, item) == TryOn::kSuccess)
        s.worn[Outfit::InvSlotOfComponent(componentSlot) - 1] = {item, 0};
}

// TryOn's step: the worker with the request's hand, enchant and quiet.
int TryOnRequest(void *pane, const TryOn::Request &request) {
    g_tryOnBodySlot = request.bodySlot;
    g_tryOnEnchant = request.enchant;
    g_tryOnQuiet = request.quiet;
    TryOnItem(pane, nullptr, request.itemID);
    g_tryOnBodySlot = g_tryOnEnchant = 0;
    g_tryOnQuiet = false;
    return TryOnResult(pane, TryOn::WornItem(request.itemID));
}

int __fastcall Script_TryOn(void *L) {
    void *pane = Game::Lua::ResolveDressUpModel(L);
    if (pane == nullptr)
        return 0;
    const TryOn::Request request = TryOn::ReadArgs(L);
    if (request.itemID == 0) {
        TryOn::ShowNotEquippable();
        Game::Lua::PushNumber(L, TryOn::kNotEquippable);
        return 1;
    }
    Game::Lua::PushNumber(L, TryOnRequest(pane, request));
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

// What the pane wears now: nothing without a model, and no slot it no
// longer shows.
const Outfit::Worn &WornOf(void *pane) {
    PaneState &s = g_panes[pane];
    if (Pane::Model(pane) == nullptr) {
        for (Outfit::Appearance &slot : s.worn)
            slot = {};
    }
    Outfit::TrimWorn(Game::Read<void *>(pane, Offsets::OFF_DRESSUPMODEL_COMPONENT), LanesOf(pane),
                     s.worn);
    return s.worn;
}

int __fastcall Script_GetItemTransmogInfo(void *L) {
    void *pane = Game::Lua::ResolveDressUpModel(L);
    if (pane == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:GetItemTransmogInfo(inventorySlot)",
                         Game::Lua::ObjectDisplayName(pane));
        return 0;
    }
    const int invSlot = static_cast<int>(Game::Lua::ToNumber(L, 2));
    if (Pane::Model(pane) == nullptr || invSlot < 1 || invSlot > Outfit::kInventorySlots)
        return 0;
    TryOn::PushTransmogInfo(L, WornOf(pane)[invSlot - 1]);
    return 1;
}

int __fastcall Script_GetItemTransmogInfoList(void *L) {
    void *pane = Game::Lua::ResolveDressUpModel(L);
    if (pane == nullptr)
        return 0;
    TryOn::PushTransmogInfoList(L, WornOf(pane));
    return 1;
}

// TryOn with the info's item and enchant, quiet. No appearance changes
// nothing.
int __fastcall Script_SetItemTransmogInfo(void *L) {
    void *pane = Game::Lua::ResolveDressUpModel(L);
    if (pane == nullptr)
        return 0;
    TryOn::Request request;
    if (!TryOn::ReadTransmogInfo(L, Game::Lua::ObjectDisplayName(pane), request))
        return 0;
    Game::Lua::PushNumber(L, request.itemID == 0 ? TryOn::kNotEquippable
                                                 : TryOnRequest(pane, request));
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
    {"GetItemTransmogInfo", &Script_GetItemTransmogInfo},
    {"GetItemTransmogInfoList", &Script_GetItemTransmogInfoList},
    {"SetItemTransmogInfo", &Script_SetItemTransmogInfo},
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
const Game::Doc::Field kTransmogInfoRets[] = {
    Game::Doc::Req("itemTransmogInfo", "ItemTransmogInfo",
                   "What the slot wears: appearanceID is the item ID, illusionID the enchant whose "
                   "glow a weapon shows. Nil when the model shows nothing."),
};
const Game::Doc::Function kGetItemTransmogInfo{
    "Returns what the model wears in one inventory slot.", kUndressArgs, kTransmogInfoRets};
const Game::Doc::Field kTransmogInfoListRets[] = {
    Game::Doc::Req("infoList", "table", "An ItemTransmogInfo for each inventory slot 1 to 19."),
};
const Game::Doc::Function kGetItemTransmogInfoList{
    "Returns what the model wears in each inventory slot.", {}, kTransmogInfoListRets};
const Game::Doc::Field kSetTransmogInfoArgs[] = {
    Game::Doc::Req("transmogInfo", "ItemTransmogInfo",
                   "appearanceID is an item ID, illusionID a SpellItemEnchantment ID."),
    Game::Doc::Opt("inventorySlots", "number", nullptr, "16 or 17: the hand that holds a weapon."),
    Game::Doc::Opt("ignoreChildItems", "bool", "false", "Has no effect."),
};
const Game::Doc::Function kSetItemTransmogInfo{
    "Puts an item on the model, as TryOn does. Shows no error. An appearanceID of 0 changes "
    "nothing.",
    kSetTransmogInfoArgs, kTryOnRets};
const Game::Doc::Method g_methodDocs[] = {
    {"UndressSlot", &kUndressSlot},   {"TryOn", &kTryOn},
    {"Dress", &kDress},               {"IsGeoReady", &kIsGeoReady},
    {"SetSheathed", &kSetSheathed},   {"GetSheathed", &kGetSheathed},
    {"SetAutoDress", &kSetAutoDress}, {"GetAutoDress", &kGetAutoDress},
    {"GetItemTransmogInfo", &kGetItemTransmogInfo},
    {"GetItemTransmogInfoList", &kGetItemTransmogInfoList},
    {"SetItemTransmogInfo", &kSetItemTransmogInfo},
};

const Game::Lua::EnumIntegerEntry kItemTryOnReason[] = {
    {"Success", TryOn::kSuccess},
    {"WrongRace", TryOn::kWrongRace},
    {"NotEquippable", TryOn::kNotEquippable},
    {"DataPending", TryOn::kDataPending},
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
