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

// `IsDressableItem(item)` — 1 when the item can be tried on in the dressing
// room, else nil. A port of 4.3.4's `Script_IsDressableItem` (0x008DA910):
// an equippable item (inventory type not 0) is dressable, and so is a recipe
// whose spell teaches a spell that creates an equippable item. Each of the
// item's five spells is tested: effect 0 must be LEARN_SPELL, the taught
// spell's effect 0 must be CREATE_ITEM, and the created item must have an
// inventory type. 4.3.4 also falls back to an item spell with trigger 6 when
// the learn effect names no spell; vanilla items have no trigger 6
// (tortoise-wow `ItemSpelltriggerType` ends at 5), so that branch is left out.
//
// The item and the created item are read from the cache with no load fired:
// an uncached one is not dressable.

#include "Game.h"
#include "Offsets.h"
#include "item/Location.h"
#include "item/Record.h"
#include "spell/Lookup.h"

#include <cstdint>

namespace Item::Dressable {

namespace {

bool IsEquippable(const uint8_t *record) {
    return record != nullptr &&
           Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE) != 0;
}

// The created item of the spell `teachID` teaches, or 0.
int CreatedItem(int teachID) {
    const uint8_t *teach = Spell::Lookup::RecordForID(teachID);
    if (teach == nullptr ||
        Game::Read<int>(teach, Offsets::OFF_SPELL_RECORD_EFFECT) != Offsets::SPELL_EFFECT_LEARN_SPELL)
        return 0;
    const uint8_t *learned = Spell::Lookup::RecordForID(
        Game::Read<int>(teach, Offsets::OFF_SPELL_RECORD_EFFECT_TRIGGER_SPELL));
    if (learned == nullptr ||
        Game::Read<int>(learned, Offsets::OFF_SPELL_RECORD_EFFECT) != Offsets::SPELL_EFFECT_CREATE_ITEM)
        return 0;
    return Game::Read<int>(learned, Offsets::OFF_SPELL_RECORD_EFFECT_ITEM_TYPE);
}

bool IsDressable(int itemID) {
    const uint8_t *record = itemID > 0 ? Item::PeekRecord(static_cast<uint32_t>(itemID)) : nullptr;
    if (record == nullptr)
        return false;
    if (IsEquippable(record))
        return true;
    for (int i = 0; i < Offsets::ITEMSTATS_SPELL_SLOT_COUNT; ++i) {
        const int spellID = Game::Read<int>(record, Offsets::OFF_ITEMSTATS_SPELL_ID + i * 4);
        if (spellID == 0)
            continue;
        const int created = CreatedItem(spellID);
        if (created > 0 && IsEquippable(Item::PeekRecord(static_cast<uint32_t>(created))))
            return true;
    }
    return false;
}

int __fastcall Script_IsDressableItem(void *L) {
    if (IsDressable(Item::Location::IDForArg(L, 1, Offsets::INVMGR_SEARCH_BANK)))
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("item", "number", "An item ID, an item link, or the name of a carried item."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("isDressable", "number", nullptr,
                   "1 when the item, or the item a recipe makes, can be equipped, else nil."),
};
const Game::Doc::Function kIsDressableItem{
    "Whether the item can be tried on in the dressing room.", kArgs, kRets, "ItemGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsDressableItem", &Script_IsDressableItem,
                                      &kIsDressableItem);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::Dressable
