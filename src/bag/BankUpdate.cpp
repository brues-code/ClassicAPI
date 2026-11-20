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

// `BAG_UPDATE(0)` when one of the bank's main slots changes, as the 3.80.2
// client fires it. Its inventory-slot watcher (FUN_141914D00) takes the bank's
// slots with the bag and bank-bag slots: when a slot's GUID changed, it raises
// BAG_UPDATE for the player's container (0, and -2 for 3.80.2's keyring, which
// shares that container) and arms BAG_UPDATE_DELAYED.
//
// 1.12 watches those slots only for PLAYERBANKSLOTS_CHANGED
// (FUN_INV_SLOT_CHANGED_OBSERVER); its bag watchers (0x004F8CC0) skip slots
// 39..62. So each bank slot gets one 8-byte watch here, as the engine
// registers its own bag-slot watches, and a change calls the engine's
// RaiseBagUpdate for the player: BAG_UPDATE(0), which arms
// BAG_UPDATE_DELAYED through Bag::UpdateDelayed's hook. 1.12's keyring has
// slots and a BAG_UPDATE(-2) of its own, so a bank change does not raise it.

#include "Offsets.h"
#include "player/ActiveWatchers.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace Bag::BankUpdate {

namespace {

using Player::ActiveWatchers::AutoWatch;
using Player::ActiveWatchers::Watch;

constexpr size_t kBankSlots = Offsets::INVMGR_BANK_MAIN_LAST_SLOT - Offsets::INVMGR_BANK_MAIN_FIRST_SLOT + 1;

// A player inventory slot's GUID field, as the engine's slot watchers take it:
// slot = (offset - 0x4A8) >> 3.
constexpr uint32_t SlotOffset(size_t slot) {
    return static_cast<uint32_t>(Offsets::OFF_DESC_PLAYER_EQUIP_FIRST + slot * sizeof(uint64_t));
}

using RaiseBagUpdate_t = void(__stdcall *)(uint32_t guidLo, uint32_t guidHi);

// Observer ABI (Player::ActiveWatchers): runs only when the slot's GUID
// changed; the GUID is the active player's.
int __fastcall BankSlotChanged(uint32_t /*fieldOffset*/, uint32_t /*size*/, uint32_t guidLo, uint32_t guidHi,
                               const void * /*oldValues*/, void * /*userArg*/) {
    reinterpret_cast<RaiseBagUpdate_t>(Offsets::FUN_RAISE_BAG_UPDATE)(guidLo, guidHi);
    return 1;
}

template <class Slots> struct BankSlotWatches;
template <size_t... I> struct BankSlotWatches<std::index_sequence<I...>> {
    const AutoWatch watches[sizeof...(I)]{
        AutoWatch{Watch{SlotOffset(Offsets::INVMGR_BANK_MAIN_FIRST_SLOT + I), sizeof(uint64_t), &BankSlotChanged}}...};
};

const BankSlotWatches<std::make_index_sequence<kBankSlots>> _watches;

} // namespace

} // namespace Bag::BankUpdate
