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

// `DressUpModel:UndressSlot(slot)` and the 4.3.4 `DressUpModel:TryOn(item)`
// (DressUpModelFrame_Scripts.cpp).
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
// ERR_NOT_EQUIPPABLE, as 4.3.4 does. 4.3.4's optional hand argument
// ("mainhand" / "offhand") is not supported: vanilla's worker picks the hand
// itself.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "item/Arg.h"
#include "item/ID.h"
#include "item/Location.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Model::DressUp {
namespace {

using UnequipSlot_t = void(__thiscall *)(void *component, uint32_t slot);
using DetachHeldItem_t = void(__fastcall *)(void *model, uint32_t bodySlot, uint32_t sheatheType,
                                            bool shield);
using TryOnItem_t = void(__thiscall *)(void *pane, uint32_t itemID);
using DisplayError_t = void(__cdecl *)(uint32_t id, ...);

// Body slots of the two held lanes.
constexpr uint32_t kBodySlotMainHand = 0xF;
constexpr uint32_t kBodySlotOffHand = 0x10;
constexpr uint8_t kInvTypeShield = 14;

void *ResolvePane(void *L) {
    return Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_DRESSUPMODEL_LUA_TYPE_ID);
}

// ClearModel's lane step: detach the held item and zero the lane.
void ClearLane(void *pane, uintptr_t laneOffset, uint32_t bodySlot) {
    uint8_t *lane = Game::Ptr<uint8_t>(pane, laneOffset);
    if (lane[0] == 0)
        return;
    reinterpret_cast<DetachHeldItem_t>(Offsets::FUN_DETACH_HELD_ITEM_SLOT)(
        Game::Read<void *>(pane, Offsets::OFF_SIMPLEMODELFFX_MODEL_INSTANCE), bodySlot, lane[4],
        lane[3] == kInvTypeShield);
    std::memset(lane, 0, 8);
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
    void *pane = ResolvePane(L);
    if (pane == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:UndressSlot(slotIndex)", Game::Lua::ObjectDisplayName(pane));
        return 0;
    }
    const int slot0 = static_cast<int>(Game::Lua::ToNumber(L, 2)) - 1;
    void *component = Game::Read<void *>(pane, Offsets::OFF_DRESSUPMODEL_COMPONENT);
    if (Game::Read<void *>(pane, Offsets::OFF_SIMPLEMODELFFX_MODEL_INSTANCE) == nullptr ||
        component == nullptr)
        return 0;
    if (slot0 == 15) {
        ClearLane(pane, Offsets::OFF_DRESSUPMODEL_MAIN_HAND_LANE, kBodySlotMainHand);
    } else if (slot0 == 16) {
        ClearLane(pane, Offsets::OFF_DRESSUPMODEL_OFF_HAND_LANE, kBodySlotOffHand);
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

int __fastcall Script_TryOn(void *L) {
    void *pane = ResolvePane(L);
    if (pane == nullptr)
        return 0;
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
            if (arg.itemID != 0)
                itemID = static_cast<uint32_t>(arg.itemID);
            else if (arg.name != nullptr && Item::Location::FindByArg(L, arg, &found))
                itemID = static_cast<uint32_t>(Item::ID::FromCGItem(found.item));
        }
    }
    if (itemID == 0) {
        reinterpret_cast<DisplayError_t>(Offsets::FUN_GAMEUI_DISPLAY_ERROR)(
            Offsets::GAME_ERROR_NOT_EQUIPPABLE);
        return 0;
    }
    reinterpret_cast<TryOnItem_t>(Offsets::FUN_DRESSUPMODEL_TRY_ON_ITEM)(pane, itemID);
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"UndressSlot", &Script_UndressSlot},
    {"TryOn", &Script_TryOn},
};

const Game::Doc::Field kUndressArgs[] = {
    Game::Doc::Req("slot", "number", "An inventory slot, 1 (head) to 19 (tabard)."),
};
const Game::Doc::Function kUndressSlot{
    "Removes the item in one inventory slot from the model.", kUndressArgs, {}};
const Game::Doc::Field kTryOnArgs[] = {
    Game::Doc::Req("item", "ItemInfo",
                   "An item ID, item link, trade-skill recipe link, or the name of an item you carry."),
};
const Game::Doc::Function kTryOn{"Puts an item on the model.", kTryOnArgs, {}};
const Game::Doc::Method g_methodDocs[] = {
    {"UndressSlot", &kUndressSlot},
    {"TryOn", &kTryOn},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_DRESSUPMODEL_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Model::DressUp
