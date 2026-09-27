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

// Equipment sets on the action bar — `C_EquipmentSet.PickupEquipmentSet`
// and everything it takes for the button to behave like 3.3.5's. There an
// equipment set is simply a fourth action type beside spell / macro /
// item: `PickupEquipmentSet` (`FUN_005af2c0`) calls the cursor pickup
// `FUN_00520dc0`, the place / pickup / use / texture / text / usable /
// tooltip readers each have an `0x20000000 | setID` branch, and a click
// fires `WEAR_EQUIPMENT_SET` (`FUN_005ae660`, event 0x276) for FrameXML's
// EquipmentManager to equip, rather than swapping in C.
//
// 1.12's readers know none of it, so each gets that one branch from a
// co-hook in front of the engine's code, which still runs for every other
// slot. Where the engine is right anyway — the spell resolver answers 0
// for the tag, `HasAction` answers true — nothing is hooked.
//
//   pickup (cursor)  Pickup — the macro pickup `FUN_00494F80` line for line
//                    (clear, type, payload, grab sound, drag icon,
//                    ACTIONBAR_SHOWGRID). Only the payload lives here:
//                    1.12 has no global for it.
//   place            FUN_ACTION_PLACE. The engine never sees our cursor,
//                    and when it overwrites a set it can only clear the
//                    cursor, so the displaced set is picked up after it.
//   pickup (slot)    FUN_ACTION_PICKUP.
//   use              Script_UseAction — nampower detours the inner
//                    `CGActionBar_UseAction`, so the branch sits on its
//                    only caller.
//   texture / text / usable — the per-slot readers behind
//                    GetActionTexture / GetActionText / IsUsableAction.
//
// Persistence. The server's CMSG_SET_ACTION_BUTTON handler whitelists
// spell / macro / item and drops type 0x20 without a word (tortoise-wow
// `HandleSetActionButtonOpcode`), keeping whatever the slot held before.
// So the one place the engine announces a player-made change — the
// notifier's `sendToServer` branch — records the placement with the set
// (`Data::SetActionSlot`) and tells the server the slot is EMPTY. At login
// the saved sets are merged into the empty slots of the SMSG_ACTION_BUTTONS
// buffer, and the engine's own enter-world loop applies them exactly as it
// would a server-sent button. A slot the server reports non-empty was
// changed where ClassicAPI wasn't running; the server wins.

#include "Action.h"

#include "Data.h"
#include "Game.h"
#include "Locations.h"
#include "Offsets.h"
#include "event/Custom.h"

#include <cstdio>
#include <vector>

namespace EquipmentSet::Action {

namespace {

using CursorClear_t = void(__fastcall *)(int returnItem, int fireMoneyEvent);
using CursorSetTexture_t = void(__fastcall *)(const char *path);
using PlaySound_t = void(__fastcall *)(const char *soundName);
using ShowGrid_t = void(__cdecl *)();
using EditAllowed_t = int(__fastcall *)(int kind);
using SlotFn_t = void(__fastcall *)(uint32_t slot0);
using Notify_t = void(__fastcall *)(uint32_t slot0, int sendToServer, int quiet);
using Usable_t = uint32_t(__fastcall *)(uint32_t slot0, int *outNoMana);
using Texture_t = const char *(__fastcall *)(uint32_t slot0);
using Script_t = int(__fastcall *)(void *L);
using LooseBool_t = int(__fastcall *)(void *L, int idx, int defaultValue);
// `__stdcall(a, packet)`, RET 8 — declared `__fastcall` with the two
// register slots passed through untouched, which is the same frame.
using ActionButtons_t = int(__fastcall *)(void *ecx, void *edx, void *a, void *packet);

constexpr uint32_t kTag = Offsets::ACTION_TYPE_EQUIPMENT_SET;
constexpr uint32_t kSlots = Offsets::ACTION_TABLE_MAX_SLOTS;

const Game::Doc::Field kWearPayload[] = {
    Game::Doc::Req("setID", "number"),
};
const Game::Doc::Event kWearDoc{
    "EquipmentSet",
    "Fires when an equipment-set action button is used. The default UI equips the set.",
    kWearPayload};
const Event::Custom::AutoReserve kEvtWear{"WEAR_EQUIPMENT_SET", &kWearDoc};

uint32_t g_cursorSetID = 0;
char g_texture[260];

Notify_t g_origNotify = nullptr;
SlotFn_t g_origPlace = nullptr;
SlotFn_t g_origPickup = nullptr;
Usable_t g_origUsable = nullptr;
Texture_t g_origTexture = nullptr;
Script_t g_origUseAction = nullptr;
Script_t g_origGetActionText = nullptr;
ActionButtons_t g_origActionButtons = nullptr;

uint32_t *Table() {
    return reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(Offsets::VAR_ACTION_TABLE));
}

uint32_t SetIDOf(uint32_t entry) {
    if (entry == 0 || (entry & Offsets::ACTION_TYPE_MASK) != kTag)
        return 0;
    return entry & ~kTag;
}

uint32_t SetIDAt(uint32_t slot0) {
    return slot0 < kSlots ? SetIDOf(Table()[slot0]) : 0;
}

void ClearCursor() {
    reinterpret_cast<CursorClear_t>(Offsets::FUN_CURSOR_CLEAR)(1, 1);
}

void NotifySlot(uint32_t slot0, int sendToServer) {
    reinterpret_cast<Notify_t>(Offsets::FUN_ACTION_SLOT_CHANGED_NOTIFY)(slot0, sendToServer, 0);
}

// The gate FUN_ACTION_PICKUP and `CGActionBar_UseAction` open with: a held
// spell, bag item, macro or type-7 action item makes them place instead.
// Anything else on the cursor they ignore.
bool EngineWouldPlace() {
    const uint32_t type = Game::Read<uint32_t>(Offsets::VAR_CURSOR_TYPE);
    return Game::Read<uint32_t>(Offsets::VAR_CURSOR_SPELL_ID) != 0 ||
           (type == 1 && (Game::Read<uint32_t>(Offsets::VAR_CURSOR_ITEM_GUID_LO) != 0 ||
                          Game::Read<uint32_t>(Offsets::VAR_CURSOR_ITEM_GUID_HI) != 0)) ||
           Game::Read<uint32_t>(Offsets::VAR_CURSOR_MACRO_ID) != 0 ||
           (type == 7 && Game::Read<uint32_t>(Offsets::VAR_CURSOR_GENERIC_SLOT) != 0);
}

// Drop the cursor's set into `slot0` in FUN_ACTION_PLACE's shape: pick up
// what the slot held (FUN_ACTION_PICKUP knows every type, once our cursor
// is out of its way), write the entry, notify with `sendToServer` so the
// notifier persists it.
void PlaceSet(uint32_t slot0, uint32_t setID) {
    if (reinterpret_cast<EditAllowed_t>(Offsets::FUN_CURSOR_EDIT_ALLOWED)(9) == 0)
        return;
    const uint32_t entry = kTag | setID;
    if (Data::FindByID(setID) == nullptr || Table()[slot0] == entry) {
        ClearCursor();
        return;
    }
    ClearCursor();
    reinterpret_cast<SlotFn_t>(Offsets::FUN_ACTION_PICKUP)(slot0);
    Table()[slot0] = entry;
    NotifySlot(slot0, 1);
}

void __fastcall Place_h(uint32_t slot0) {
    if (slot0 >= kSlots)
        return g_origPlace(slot0);
    if (const uint32_t id = CursorSetID())
        return PlaceSet(slot0, id);
    const uint32_t before = Table()[slot0];
    g_origPlace(slot0);
    // The engine clears the cursor for a type it doesn't know and then
    // overwrites; 3.3.5 hands the displaced set to the cursor instead.
    const uint32_t displaced = SetIDOf(before);
    if (displaced != 0 && Table()[slot0] != before &&
        Game::Read<uint32_t>(Offsets::VAR_CURSOR_TYPE) == 0)
        Pickup(displaced);
}

void __fastcall Pickup_h(uint32_t slot0) {
    if (slot0 >= kSlots)
        return g_origPickup(slot0);
    if (const uint32_t id = CursorSetID())
        return PlaceSet(slot0, id);
    const uint32_t setID = SetIDAt(slot0);
    if (setID == 0 || EngineWouldPlace())
        return g_origPickup(slot0);
    // 3.3.5 `FUN_005abe70`: pick the set up if it still exists, then clear
    // the slot either way.
    Pickup(setID);
    reinterpret_cast<SlotFn_t>(Offsets::FUN_ACTION_SLOT_CLEAR)(slot0);
}

int __fastcall UseAction_h(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return g_origUseAction(L); // the engine's usage error
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    if (slot < 0 || slot >= static_cast<int>(kSlots))
        return g_origUseAction(L);
    const uint32_t slot0 = static_cast<uint32_t>(slot);
    if (reinterpret_cast<LooseBool_t>(Offsets::FUN_LUA_TO_BOOLEAN_LOOSE)(L, 2, 0) != 0) {
        if (const uint32_t id = CursorSetID()) {
            PlaceSet(slot0, id);
            return 0;
        }
        if (EngineWouldPlace())
            return g_origUseAction(L);
    }
    const uint32_t setID = SetIDAt(slot0);
    if (setID == 0)
        return g_origUseAction(L);
    if (Data::FindByID(setID) != nullptr) {
        const int evt = kEvtWear.Slot();
        if (evt >= 0)
            Event::Custom::Fire(evt, "%d", static_cast<int>(setID));
    }
    return 0;
}

const char *__fastcall Texture_h(uint32_t slot0) {
    const uint32_t setID = SetIDAt(slot0);
    if (setID == 0)
        return g_origTexture(slot0);
    const Set *s = Data::FindByID(setID);
    if (s == nullptr || !IconPath(*s, g_texture, sizeof(g_texture)))
        return nullptr;
    return g_texture;
}

int __fastcall GetActionText_h(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return g_origGetActionText(L);
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    const uint32_t setID = slot >= 0 ? SetIDAt(static_cast<uint32_t>(slot)) : 0;
    if (setID == 0)
        return g_origGetActionText(L);
    const Set *s = Data::FindByID(setID);
    if (s != nullptr)
        Game::Lua::PushString(L, s->name.c_str());
    else
        Game::Lua::PushNil(L);
    return 1;
}

// 3.3.5 `FUN_005a9e20`'s set branch: unusable while the set is gone or
// holds an item mid-transaction.
uint32_t __fastcall Usable_h(uint32_t slot0, int *outNoMana) {
    const uint32_t setID = SetIDAt(slot0);
    if (setID == 0)
        return g_origUsable(slot0, outNoMana);
    *outNoMana = 0;
    const Set *s = Data::FindByID(setID);
    return (s != nullptr && !Locations::ContainsLockedItems(*s)) ? 1 : 0;
}

void __fastcall Notify_h(uint32_t slot0, int sendToServer, int quiet) {
    if (sendToServer == 0 || slot0 >= kSlots)
        return g_origNotify(slot0, sendToServer, quiet);
    const uint32_t entry = Table()[slot0];
    const uint32_t setID = SetIDOf(entry);
    Data::SetActionSlot(static_cast<int>(slot0), setID);
    if (setID == 0)
        return g_origNotify(slot0, sendToServer, quiet);
    // Send the slot as empty (quietly), then notify the real entry locally.
    Table()[slot0] = 0;
    g_origNotify(slot0, 1, 1);
    Table()[slot0] = entry;
    g_origNotify(slot0, 0, quiet);
}

int __fastcall ActionButtons_h(void *ecx, void *edx, void *a, void *packet) {
    const int result = g_origActionButtons(ecx, edx, a, packet);
    auto *pending = reinterpret_cast<uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_ACTION_BUTTONS_PENDING));
    std::vector<int> stale;
    for (const Set &s : Data::All()) {
        for (const int slot0 : s.actionSlots) {
            if (pending[slot0] == 0)
                pending[slot0] = kTag | s.setID;
            else
                stale.push_back(slot0);
        }
    }
    for (const int slot0 : stale)
        Data::SetActionSlot(slot0, 0);
    return result;
}

int __fastcall Script_PickupEquipmentSet(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_EquipmentSet.PickupEquipmentSet(setID)");
        return 0;
    }
    const double id = Game::Lua::ToNumber(L, 1);
    if (id >= 1.0)
        Pickup(static_cast<uint32_t>(id));
    return 0;
}

const Game::Doc::Field kPickupArgs[] = {
    Game::Doc::Req("setID", "number"),
};
const Game::Doc::Function kPickupDoc{
    "Puts an equipment set on the cursor, so it can be placed on an action bar.",
    kPickupArgs,
    {}};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "PickupEquipmentSet",
                                     &Script_PickupEquipmentSet, &kPickupDoc);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

const Game::HookAutoRegister _hookNotify{
    Offsets::FUN_ACTION_SLOT_CHANGED_NOTIFY, reinterpret_cast<void *>(&Notify_h),
    reinterpret_cast<void **>(&g_origNotify)};
const Game::HookAutoRegister _hookPlace{
    Offsets::FUN_ACTION_PLACE, reinterpret_cast<void *>(&Place_h),
    reinterpret_cast<void **>(&g_origPlace)};
const Game::HookAutoRegister _hookPickup{
    Offsets::FUN_ACTION_PICKUP, reinterpret_cast<void *>(&Pickup_h),
    reinterpret_cast<void **>(&g_origPickup)};
const Game::HookAutoRegister _hookUsable{
    Offsets::FUN_ACTION_SLOT_USABLE, reinterpret_cast<void *>(&Usable_h),
    reinterpret_cast<void **>(&g_origUsable)};
const Game::HookAutoRegister _hookTexture{
    Offsets::FUN_ACTION_SLOT_TEXTURE, reinterpret_cast<void *>(&Texture_h),
    reinterpret_cast<void **>(&g_origTexture)};
const Game::HookAutoRegister _hookUseAction{
    Offsets::FUN_SCRIPT_USE_ACTION, reinterpret_cast<void *>(&UseAction_h),
    reinterpret_cast<void **>(&g_origUseAction)};
const Game::HookAutoRegister _hookActionText{
    Offsets::FUN_SCRIPT_GET_ACTION_TEXT, reinterpret_cast<void *>(&GetActionText_h),
    reinterpret_cast<void **>(&g_origGetActionText)};
const Game::HookAutoRegister _hookActionButtons{
    Offsets::FUN_SMSG_ACTION_BUTTONS, reinterpret_cast<void *>(&ActionButtons_h),
    reinterpret_cast<void **>(&g_origActionButtons)};

} // namespace

uint32_t CursorSetID() {
    if (Game::Read<uint32_t>(Offsets::VAR_CURSOR_TYPE) != Offsets::CURSOR_TYPE_EQUIPMENT_SET)
        return 0;
    return g_cursorSetID;
}

uint32_t SlotSetID(uint32_t slot0) {
    return SetIDAt(slot0);
}

bool Pickup(uint32_t setID) {
    const Set *s = Data::FindByID(setID);
    if (s == nullptr)
        return false;
    char path[260];
    if (!IconPath(*s, path, sizeof(path)))
        return false;
    ClearCursor();
    Game::Ref<uint32_t>(Offsets::VAR_CURSOR_TYPE) = Offsets::CURSOR_TYPE_EQUIPMENT_SET;
    g_cursorSetID = setID;
    reinterpret_cast<PlaySound_t>(Offsets::FUN_PLAY_SOUND_BY_NAME)(
        "INTERFACESOUND_CURSORGRABOBJECT");
    reinterpret_cast<CursorSetTexture_t>(Offsets::FUN_CURSOR_SET_TEXTURE)(path);
    reinterpret_cast<ShowGrid_t>(Offsets::FUN_ACTIONBAR_SHOW_GRID)();
    Game::Ref<uint32_t>(Offsets::VAR_CURSOR_SHOWS_ACTION_GRID) = 1;
    return true;
}

bool IconPath(const Set &set, char *out, size_t outSize) {
    // Stored as given to Create/SaveEquipmentSet — a bare name or a path.
    const char *base = set.icon.c_str();
    for (const char *p = base; *p != '\0'; ++p) {
        if (*p == '\\' || *p == '/')
            base = p + 1;
    }
    const int n = std::snprintf(out, outSize, "Interface\\Icons\\%s", base);
    return n > 0 && static_cast<size_t>(n) < outSize;
}

void Repaint(uint32_t setID) {
    for (uint32_t slot0 = 0; slot0 < kSlots; ++slot0) {
        if (SetIDAt(slot0) == setID)
            NotifySlot(slot0, 0);
    }
}

void ClearButtons(uint32_t setID) {
    for (uint32_t slot0 = 0; slot0 < kSlots; ++slot0) {
        if (SetIDAt(slot0) == setID)
            reinterpret_cast<SlotFn_t>(Offsets::FUN_ACTION_SLOT_CLEAR)(slot0);
    }
}

} // namespace EquipmentSet::Action
