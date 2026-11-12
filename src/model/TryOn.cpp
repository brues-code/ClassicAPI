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

// TryOn's arguments. Retail's hand argument is an inventory slot name,
// matched the way GetInventorySlotInfo matches it (SStrCmpI over the
// PaperDollItemFrame rows): MainHandSlot or SecondaryHandSlot. Any other
// string names no hand.
//
// A recipe tries on what it makes, as 4.3.4's worker (`TryOnWithVisual`,
// 0x00A05010) does: an item with no inventory type whose spell teaches a
// CREATE_ITEM spell is swapped for the made item, and a trade-skill
// `enchant:` link names the item its TRADESPELL's first effect makes.

#include "model/TryOn.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "item/Arg.h"
#include "item/Dressable.h"
#include "item/ID.h"
#include "item/Location.h"
#include "item/Record.h"
#include "model/Outfit.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Model::TryOn {
namespace {

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
        case Offsets::INVSLOT_MAINHAND: return Outfit::kBodySlotMainHand;
        case Offsets::INVSLOT_OFFHAND: return Outfit::kBodySlotOffHand;
        default: return 0;
        }
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

} // namespace

Request ReadArgs(void *L) {
    Request request;
    // Read before the name search below, which stomps the Lua stack.
    if (Game::Lua::Type(L, 3) == Game::Lua::TYPE_STRING)
        request.bodySlot = NamedHand(Game::Lua::ToString(L, 3));
    const bool enchantGiven = Game::Lua::IsNumber(L, 4) != 0;
    if (enchantGiven)
        request.enchant = static_cast<uint32_t>(Game::Lua::ToNumber(L, 4));
    if (Game::Lua::Type(L, 2) == Game::Lua::TYPE_NUMBER) {
        request.itemID = static_cast<uint32_t>(Game::Lua::ToNumber(L, 2));
    } else if (Game::Lua::Type(L, 2) == Game::Lua::TYPE_STRING) {
        // Copied: the name search below stomps the Lua stack.
        char s[256];
        std::snprintf(s, sizeof(s), "%s", Game::Lua::ToString(L, 2));
        if (std::strstr(s, "enchant:") != nullptr) {
            request.itemID = RecipeItem(s);
        } else {
            // An item link, `item:` string or numeric string resolves to an ID;
            // any other string is the name of an item the player carries.
            const Item::Arg::Resolved arg = Item::Arg::ResolveString(s);
            Item::Location::ByGUIDResult found{};
            if (arg.itemID != 0) {
                request.itemID = static_cast<uint32_t>(arg.itemID);
                if (!enchantGiven)
                    request.enchant = static_cast<uint32_t>(arg.enchant);
            } else if (arg.name != nullptr && Item::Location::FindByArg(L, arg, &found))
                request.itemID = static_cast<uint32_t>(Item::ID::FromCGItem(found.item));
        }
    }
    return request;
}

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

void ShowNotEquippable() {
    using DisplayError_t = void(__cdecl *)(uint32_t id, ...);
    reinterpret_cast<DisplayError_t>(Offsets::FUN_GAMEUI_DISPLAY_ERROR)(
        Offsets::GAME_ERROR_NOT_EQUIPPABLE);
}

namespace {

// A number field of the table at `index`, or 0.
uint32_t FieldNumber(void *L, int index, const char *key) {
    Game::Lua::PushString(L, key);
    Game::Lua::GetTable(L, index);
    const double value = Game::Lua::IsNumber(L, -1) ? Game::Lua::ToNumber(L, -1) : 0.0;
    Game::Lua::SetTop(L, -2);
    return value > 0.0 ? static_cast<uint32_t>(value) : 0;
}

} // namespace

bool ReadTransmogInfo(void *L, const char *name, Request &request) {
    if (Game::Lua::Type(L, 2) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L,
                         "Usage: %s:SetItemTransmogInfo(transmogInfo [, inventorySlots, ignoreChildItems])",
                         name);
        return false;
    }
    request.itemID = FieldNumber(L, 2, "appearanceID");
    request.enchant = FieldNumber(L, 2, "illusionID");
    request.quiet = true;
    if (Game::Lua::IsNumber(L, 3)) {
        const int invSlot = static_cast<int>(Game::Lua::ToNumber(L, 3));
        if (invSlot == Offsets::INVSLOT_MAINHAND)
            request.bodySlot = Outfit::kBodySlotMainHand;
        else if (invSlot == Offsets::INVSLOT_OFFHAND)
            request.bodySlot = Outfit::kBodySlotOffHand;
    }
    return true;
}

void PushTransmogInfo(void *L, const Outfit::Appearance &slot) {
    Game::Lua::PushString(L, "ItemUtil");
    Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
    Game::Lua::PushString(L, "CreateItemTransmogInfo");
    Game::Lua::GetTable(L, -2);
    Game::Lua::Remove(L, -2);
    Game::Lua::PushNumber(L, slot.item);
    Game::Lua::PushNumber(L, 0);
    Game::Lua::PushNumber(L, slot.enchant);
    Game::Lua::Call(L, 3, 1);
}

void PushTransmogInfoList(void *L, const Outfit::Worn &worn) {
    Game::Lua::NewTable(L);
    for (int i = 0; i < Outfit::kInventorySlots; ++i) {
        PushTransmogInfo(L, worn[i]);
        Game::Lua::RawSetI(L, -2, i + 1);
    }
}

} // namespace Model::TryOn
