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

#pragma once

#include "model/Outfit.h"

#include <cstdint>

// What every `TryOn(item [, handSlotName, spellEnchantID])` takes in front of
// the step that dresses the model: DressUpModel's (model/DressUp.cpp, the
// engine's TryOnItem worker) and the ModelScene actor's (Outfit::TryOn).
namespace Model::TryOn {

// Retail's Enum.ItemTryOnReason.
enum Reason : int {
    kSuccess = 0,
    kWrongRace = 1,
    kNotEquippable = 2,
    kDataPending = 3,
};

struct Request {
    uint32_t itemID = 0;   // 0: nothing to try on
    uint32_t bodySlot = 0; // the hand the arguments name (Outfit::kBodySlot*), or 0
    uint32_t enchant = 0;  // the SpellItemEnchantment whose glow a weapon shows, or 0
    bool quiet = false;    // no ERR_NOT_EQUIPPABLE: the caller shows its own (SetItemTransmogInfo)
};

// The arguments from stack index 2: 4.3.4's item forms (an item ID, an item
// link or `item:` string, a trade-skill `enchant:` link for the item its
// recipe makes, or the name of an item the player carries), retail's hand
// slot name and enchant. The enchant defaults to the link's.
Request ReadArgs(void *L);

// The item the model wears for `itemID`: a cached recipe is what it makes
// (4.3.4's TryOnWithVisual swap).
uint32_t WornItem(uint32_t itemID);

// The worker's answer to an item it cannot put on: ERR_NOT_EQUIPPABLE.
void ShowNotEquippable();

// SetItemTransmogInfo's arguments from stack index 2: the info's item
// (appearanceID) and enchant (illusionID), and the hand an inventory slot of
// 16 or 17 names. Quiet: retail's callers show the error. False, after a Lua
// usage error naming the frame `name`, when index 2 is not a table.
bool ReadTransmogInfo(void *L, const char *name, Request &request);

// One slot as retail's ItemTransmogInfo: ItemUtil.CreateItemTransmogInfo(item,
// NoTransmogID, enchant), built by the mixin's own constructor.
void PushTransmogInfo(void *L, const Outfit::Appearance &slot);

// Every slot 1..19 as a list of ItemTransmogInfo.
void PushTransmogInfoList(void *L, const Outfit::Worn &worn);

} // namespace Model::TryOn
