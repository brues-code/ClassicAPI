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

// `Enum.BagIndex` — retail's BagIndexConstants enumeration, with Classic
// Era's names for the containers this client has and this client's own
// indices for them. Era numbers its bank bags from 6 because its reagent bag
// takes 5; here they start right after the equipped bags, as the stock bank
// frame numbers them. The reagent bag, reagent bank and account bank tabs
// have no entry.

#include "Game.h"
#include "Offsets.h"

namespace Container::BagIndex {

namespace {

const Game::Lua::EnumIntegerEntry kEntries[] = {
    {"Keyring", Offsets::BAG_INDEX_KEYRING},
    {"Bank", Offsets::BAG_INDEX_BANK},
    {"Backpack", Offsets::BAG_INDEX_BACKPACK},
    {"Bag_1", Offsets::BAG_INDEX_BAG_FIRST},
    {"Bag_2", Offsets::BAG_INDEX_BAG_FIRST + 1},
    {"Bag_3", Offsets::BAG_INDEX_BAG_FIRST + 2},
    {"Bag_4", Offsets::BAG_INDEX_BAG_LAST},
    {"BankBag_1", Offsets::BAG_INDEX_BANK_BAG_FIRST},
    {"BankBag_2", Offsets::BAG_INDEX_BANK_BAG_FIRST + 1},
    {"BankBag_3", Offsets::BAG_INDEX_BANK_BAG_FIRST + 2},
    {"BankBag_4", Offsets::BAG_INDEX_BANK_BAG_FIRST + 3},
    {"BankBag_5", Offsets::BAG_INDEX_BANK_BAG_FIRST + 4},
    {"BankBag_6", Offsets::BAG_INDEX_BANK_BAG_LAST},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterIntegerEnum("Enum", "BagIndex", kEntries,
                                   sizeof(kEntries) / sizeof(kEntries[0]), "BagIndexConstants");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::BagIndex
