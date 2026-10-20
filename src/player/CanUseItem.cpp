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

// `C_PlayerInfo.CanUseItem(itemID)` — true iff the local player meets the
// item's *use/equip* requirements. This is the "is the item red in the
// tooltip" gate, and is distinct from its siblings:
//   - `C_Item.IsEquippableItem` — does the item fit *some* slot (static).
//   - `IsUsableItem`            — is the item's on-use ability castable now.
//   - `C_PlayerInfo.CanUseItem` — may the player equip/use it at all.
//
// The answer is the engine's own `CGPlayer_C::CanUseItem`
// (FUN_PLAYER_CAN_USE_ITEM): level, class and race masks, proficiency,
// required skill, required spell (or a higher rank), honor rank, PvP medal
// and reputation. It is the check behind every "can use" answer 1.12 gives
// Lua (loot, quest rewards, merchant, auction, mail, trade) and the Bind on
// Equip confirmation, as 4.3.4's CGPlayer_C::CanUseItem is behind the
// modern API.
//
// Cache-miss handling matches `C_Item.IsEquippableItem`: a synchronous
// peek that returns false without firing a load.

#include "Game.h"
#include "Offsets.h"
#include "item/Arg.h"
#include "item/Record.h"
#include "player/CanUseItem.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Player::CanUseItem {

using CanUseItem_t = int(__thiscall *)(const uint8_t *player, const uint8_t *record,
                                       uint32_t *error);

bool Check(const uint8_t *player, const uint8_t *record) {
    uint32_t error = 0;
    return reinterpret_cast<CanUseItem_t>(Offsets::FUN_PLAYER_CAN_USE_ITEM)(player, record,
                                                                            &error) != 0;
}

namespace {

bool ComputeCanUse(int itemID) {
    if (itemID <= 0)
        return false;
    auto *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr)
        return false; // not cached → sync false, no load fired
    auto *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return false; // pre-login / no player
    return Check(player, record);
}

int __fastcall Script_CanUseItem(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    Game::Lua::PushBool(L, ComputeCanUse(itemID));
    return 1;
}

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_PlayerInfo", "CanUseItem", &Script_CanUseItem);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Player::CanUseItem
