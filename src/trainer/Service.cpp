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

// `GetTrainerServiceItemLink(index)` links the item that the recipe in row
// `index` of a profession trainer makes, or returns nil.
//
// Mirrors 3.3.5's Script_GetTrainerServiceItemLink (0x00596290): only a
// tradeskill trainer (type 2), only a recipe whose first effect is
// CREATE_ITEM with an item, and only while that item is in the item cache.
// The link is the plain `item:<id>:0:0:0` link with the item's quality
// colour, the one 1.12's GetTradeSkillItemLink (0x004FF410) builds
// (Item::Link::BasicFromItemID, a passive cache read like the engine's).
// 3.3.5 also accepts effects 59 and 157 as create-item; in 1.12, 59 is
// OPEN_LOCK_ITEM and 157 does not exist.
//
// The one difference is which spell is the recipe. A 3.3.5 server sends the
// recipe spell itself; a 1.12 server sends the trainer spell that teaches
// it. So the row's spell goes through the first LEARN_SPELL or
// LEARN_PET_SPELL effect to the spell it teaches, exactly as 1.12's own
// Script_GetTrainerServiceIcon (0x004D8F50) does to show the made item's
// icon for a type-2 trainer.

#include "Game.h"
#include "Offsets.h"
#include "item/Link.h"
#include "spell/Lookup.h"

#include <cstdint>

namespace Trainer::Service {

namespace {

// The spell `trainerSpell` teaches through its first LEARN_SPELL or
// LEARN_PET_SPELL effect, or nullptr. Like GetTrainerServiceIcon, only the
// first learn effect counts.
const uint8_t *TaughtSpell(const uint8_t *trainerSpell) {
    for (int i = 0; i < Offsets::SPELL_RECORD_EFFECT_COUNT; ++i) {
        const int effect =
            Game::Read<int>(trainerSpell, Offsets::OFF_SPELL_RECORD_EFFECT + i * 4);
        if (effect == Offsets::SPELL_EFFECT_LEARN_SPELL ||
            effect == Offsets::SPELL_EFFECT_LEARN_PET_SPELL)
            return Spell::Lookup::RecordForID(Game::Read<int>(
                trainerSpell, Offsets::OFF_SPELL_RECORD_EFFECT_TRIGGER_SPELL + i * 4));
    }
    return nullptr;
}

int __fastcall Script_GetTrainerServiceItemLink(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetTrainerServiceItemLink(index)");
        return 0;
    }
    // GetTrainerService (0x004D89B0) takes the truncated index - 1 and
    // compares it unsigned against the service count. Compared as a double
    // first: casting NaN or an out-of-range value to an integer is undefined
    // in C++.
    const double n = Game::Lua::ToNumber(L, 1);
    const uint32_t count = Game::Read<uint32_t>(Offsets::VAR_TRAINER_SERVICE_COUNT);
    if (n >= 1.0 && n < static_cast<double>(count) + 1.0 &&
        Game::Read<int>(Offsets::VAR_TRAINER_TYPE) == Offsets::TRAINER_TYPE_TRADESKILLS) {
        const uint8_t *trainerSpell = Spell::Lookup::RecordForID(
            Spell::Lookup::RecipeSlotSpellID(Offsets::VAR_TRAINER_SERVICES,
                                             Offsets::VAR_TRAINER_SERVICE_COUNT,
                                             static_cast<int>(n) - 1));
        const uint8_t *recipe = trainerSpell != nullptr ? TaughtSpell(trainerSpell) : nullptr;
        if (recipe != nullptr &&
            Game::Read<int>(recipe, Offsets::OFF_SPELL_RECORD_EFFECT) ==
                Offsets::SPELL_EFFECT_CREATE_ITEM) {
            const uint32_t itemID =
                Game::Read<uint32_t>(recipe, Offsets::OFF_SPELL_RECORD_EFFECT_ITEM_TYPE);
            char link[256];
            if (itemID != 0 && Item::Link::BasicFromItemID(itemID, link, sizeof link)) {
                Game::Lua::PushString(L, link);
                return 1;
            }
        }
    }
    Game::Lua::PushNil(L);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kItemLinkArgs[] = {
    Game::Doc::Req("index", "luaIndex", "A row of the trainer window."),
};
const Game::Doc::Field kItemLinkRets[] = {
    Game::Doc::Opt("link", "string", nullptr,
                   "Nil when the recipe makes no item or the item is not cached yet."),
};
const Game::Doc::Function kGetTrainerServiceItemLink{
    "Returns a link to the item that a profession trainer's recipe makes.",
    kItemLinkArgs, kItemLinkRets, "TrainerGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetTrainerServiceItemLink",
                                      &Script_GetTrainerServiceItemLink,
                                      &kGetTrainerServiceItemLink);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Trainer::Service
