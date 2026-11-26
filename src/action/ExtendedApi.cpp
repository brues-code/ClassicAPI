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

// The Lua side of Action::Extended's slots 121..360.
//
//   PlaceAction / PickupAction / UseAction — still C globals in 3.80.2.
//     Registered in front of the engine's (last registration wins), each a
//     strict superset: a number naming one of our slots runs
//     Action::ExtendedDrag / ExtendedUse, anything else tail-calls the
//     engine's Script function with the untouched stack (its own usage error,
//     its own 1..120, its own bounds for the rest). They are not MinHooked:
//     EquipmentSet::Action already hooks FUN_ACTION_PLACE, FUN_ACTION_PICKUP
//     and FUN_SCRIPT_USE_ACTION, and calling those addresses goes through its
//     hooks. UseAction reads its two flags with the engine's own flag reader
//     rule (Game::Lua::ToFlag, default false), as Script_UseAction does.
//   C_ActionBar.HasAction / GetActionTexture — 3.80.2's reads, both ranges:
//     1..120 ask the engine (FUN_ACTION_SLOT_HAS_CONTENT, the test behind
//     HasAction; FUN_ACTION_SLOT_TEXTURE, behind GetActionTexture, which
//     EquipmentSet::Action's hook extends), 121..360 our store with the same
//     rules. Retail answers a fileID; this answers the texture path. A slot
//     outside 1..360 returns nothing, as 3.80.2's slot reads do. The other
//     reads are Action::BarReads.

#include "Game.h"
#include "Offsets.h"
#include "action/Extended.h"
#include "action/ExtendedDrag.h"
#include "action/ExtendedState.h"
#include "action/ExtendedUse.h"
#include "equipmentset/Action.h"
#include "item/Data.h"
#include "spell/Lookup.h"

#include <cstdint>
#include <cstdio>

namespace Action::ExtendedApi {

namespace {

using HasContent_t = bool(__fastcall *)(uint32_t slot0);
using SlotTexture_t = const char *(__fastcall *)(uint32_t slot0);
using WeaponIconPath_t = const char *(__cdecl *)();
using InventoryIcon_t = const char *(__fastcall *)(uint32_t displayID);
using MacroIconPath_t = void(__fastcall *)(uint32_t macroID, char *out, uint32_t size);

char g_texture[0x104]; // the engine's own texture buffer size (0x004E6B5D)

// GetActionSlotTexture (0x004E6A50) for a word of ours, in its branch order
// behind EquipmentSet::Action's co-hook: set → attack → ranged auto-repeat →
// item → spell → macro. The attack and ranged tests (0x004E59B0 / 0x004E5A00)
// read the word's resolved spell, so a macro whose spell is Attack shows the
// weapon too.
const char *Texture(uint32_t word) {
    if (word == 0)
        return nullptr;
    if (const uint32_t setID = EquipmentSet::Action::WordSetID(word))
        return EquipmentSet::Action::Texture(setID);
    uint32_t book = 0;
    const uint32_t spell = Extended::SpellFor(word, &book);
    if (ExtendedState::IsAttack(word))
        return reinterpret_cast<WeaponIconPath_t>(Offsets::FUN_ATTACK_ICON_PATH)();
    if (ExtendedState::IsRangedRepeat(word)) {
        const char *ranged = reinterpret_cast<WeaponIconPath_t>(Offsets::FUN_RANGED_ICON_PATH)();
        if (ranged != nullptr && ranged[0] != '\0')
            return ranged;
    }

    const uint32_t tag = word & Offsets::ACTION_TYPE_MASK;
    if (tag == Offsets::ACTION_TYPE_ITEM_BY_ID) {
        // The template's display id (0 while it isn't cached; the engine asks
        // for it here) through the engine's icon getter, which answers
        // INV_Misc_QuestionMark for display 0 — so an item never reads nil.
        const uint8_t *record =
            Item::Data::RecordOrRequest(word & Offsets::ACTION_PAYLOAD_MASK_ITEM_BY_ID);
        const uint32_t display =
            record != nullptr ? Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID) : 0;
        std::snprintf(g_texture, sizeof g_texture, "Interface\\Icons\\%s",
                      reinterpret_cast<InventoryIcon_t>(Offsets::FUN_ITEM_DISPLAY_INVENTORY_ICON)(display));
        return g_texture;
    }
    if (tag == Offsets::ACTION_TYPE_SPELL) {
        const uint8_t *record = ::Spell::Lookup::RecordForID(static_cast<int>(spell));
        if (record == nullptr)
            return nullptr;
        return ::Spell::Lookup::IconPath(record, Extended::AuraActive(spell, book));
    }
    if (const uint32_t macroID = Extended::LiveMacroID(word)) {
        // The hooked address: Macro::IconPath's `?` / #show icon rule lives there.
        g_texture[0] = '\0';
        reinterpret_cast<MacroIconPath_t>(Offsets::FUN_MACRO_ICON_PATH)(macroID, g_texture,
                                                                        sizeof g_texture);
        return g_texture;
    }
    return nullptr;
}

// FUN_ACTION_SLOT_HAS_CONTENT for a word of ours: nonzero, and a macro word
// only while its macro exists.
bool HasContent(uint32_t word) {
    if (word == 0)
        return false;
    if ((word & Offsets::ACTION_TYPE_MASK) == Offsets::ACTION_TYPE_BAG_OR_MACRO)
        return Extended::LiveMacroID(word) != 0;
    return true;
}

int __fastcall Script_PlaceAction(void *L) {
    int slot0 = 0;
    if (!Extended::SlotArg(L, 1, &slot0))
        return Game::Lua::CallScript(Offsets::FUN_SCRIPT_PLACE_ACTION, L);
    ExtendedDrag::Place(slot0);
    return 0;
}

int __fastcall Script_PickupAction(void *L) {
    int slot0 = 0;
    if (!Extended::SlotArg(L, 1, &slot0))
        return Game::Lua::CallScript(Offsets::FUN_SCRIPT_PICKUP_ACTION, L);
    ExtendedDrag::Pickup(slot0);
    return 0;
}

int __fastcall Script_UseAction(void *L) {
    int slot0 = 0;
    if (!Extended::SlotArg(L, 1, &slot0))
        return Game::Lua::CallScript(Offsets::FUN_SCRIPT_USE_ACTION, L);
    ExtendedUse::Use(slot0, Game::Lua::ToFlag(L, 2), Game::Lua::ToFlag(L, 3));
    return 0;
}

// The 0-based slot of stack[1] for the C_ActionBar reads; false after the
// usage error when it is not a number, and false (nothing returned) for a
// number naming no slot.
bool ActionArg(void *L, const char *usage, int *slot0) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, usage);
        return false;
    }
    *slot0 = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    return (*slot0 >= 0 && *slot0 < Offsets::ACTION_TABLE_MAX_SLOTS) || Extended::Owns(*slot0);
}

int __fastcall Script_HasAction(void *L) {
    int slot0 = 0;
    if (!ActionArg(L, "Usage: C_ActionBar.HasAction(actionID)", &slot0))
        return 0;
    bool has = false;
    if (slot0 < Offsets::ACTION_TABLE_MAX_SLOTS)
        has = reinterpret_cast<HasContent_t>(Offsets::FUN_ACTION_SLOT_HAS_CONTENT)(
            static_cast<uint32_t>(slot0));
    else
        has = HasContent(Extended::Get(slot0));
    Game::Lua::PushBool(L, has);
    return 1;
}

int __fastcall Script_GetActionTexture(void *L) {
    int slot0 = 0;
    if (!ActionArg(L, "Usage: C_ActionBar.GetActionTexture(actionID)", &slot0))
        return 0;
    const char *path = nullptr;
    if (slot0 < Offsets::ACTION_TABLE_MAX_SLOTS)
        path = reinterpret_cast<SlotTexture_t>(Offsets::FUN_ACTION_SLOT_TEXTURE)(
            static_cast<uint32_t>(slot0));
    else
        path = Texture(Extended::Get(slot0));
    if (path != nullptr)
        Game::Lua::PushString(L, path);
    else
        Game::Lua::PushNil(L);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kSlotArgs[] = {
    Game::Doc::Req("slot", "luaIndex", "The action slot, 1 to 360."),
};
const Game::Doc::Function kPlaceAction{
    "Puts the spell, item or macro on the cursor into an action slot; the slot's old action "
    "goes onto the cursor.",
    kSlotArgs, {}, "ActionBarGlobals"};
const Game::Doc::Function kPickupAction{
    "Puts the action in a slot onto the cursor and empties the slot; with an action already "
    "on the cursor it places that one instead.",
    kSlotArgs, {}, "ActionBarGlobals"};

const Game::Doc::Field kUseActionArgs[] = {
    Game::Doc::Req("slot", "luaIndex", "The action slot, 1 to 360."),
    Game::Doc::Opt("checkCursor", "bool", "false",
                   "When true and the cursor holds an action, places it instead."),
    Game::Doc::Opt("onSelf", "bool", "false", "Casts the spell at yourself."),
};
const Game::Doc::Function kUseAction{
    "Uses the action in a slot: casts its spell, runs its macro, or uses or equips its item.",
    kUseActionArgs, {}, "ActionBarGlobals"};

const Game::Doc::Field kActionIDArgs[] = {
    Game::Doc::Req("actionID", "luaIndex"),
};
const Game::Doc::Field kHasActionRets[] = {
    Game::Doc::Opt("hasAction", "bool", nullptr, "Nothing for a slot outside 1 to 360."),
};
const Game::Doc::Function kHasAction{"Returns whether an action slot holds an action.",
                                     kActionIDArgs, kHasActionRets};

const Game::Doc::Field kGetActionTextureRets[] = {
    Game::Doc::Opt("textureFileID", "string", nullptr,
                   "The icon's texture path; nil for an empty slot, nothing for a slot outside "
                   "1 to 360."),
};
const Game::Doc::Function kGetActionTexture{"Returns the icon of the action in a slot.",
                                            kActionIDArgs, kGetActionTextureRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("PlaceAction", &Script_PlaceAction, &kPlaceAction);
    Game::Lua::RegisterGlobalFunction("PickupAction", &Script_PickupAction, &kPickupAction);
    Game::Lua::RegisterGlobalFunction("UseAction", &Script_UseAction, &kUseAction);
    Game::Lua::RegisterTableFunction("C_ActionBar", "HasAction", &Script_HasAction, &kHasAction);
    Game::Lua::RegisterTableFunction("C_ActionBar", "GetActionTexture", &Script_GetActionTexture,
                                     &kGetActionTexture);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Action::ExtendedApi
