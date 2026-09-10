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

// `C_Item.UseItemByName(item [, unit])` — uses `item` via the engine's
// `CGItem::UseItem` primitive. Optional `unit` is a unit token (`"player"`,
// `"target"`, `"focus"`, …) passed to spell-cast items (scrolls, traps,
// on-use targeted effects) as the cast target.
//
// `item` is an item reference (itemID, `item:N`, a link, or the name of an
// item the player carries — the first bag match is used) or an item location
// (`{bagID=B, slotIndex=S}`, `{equipmentSlotIndex=N}`, an item GUID), which
// names one exact item. See `Item::Location::ResolveItemArgOrLocation`, which
// `C_Item.UseAtCursor` and `C_Item.UseAtUnit` share.
//
// Same structure 3.3.5's `Script_UseItemByName` uses: locate the item
// directly, then hand it to the engine's by-item-pointer use call.
// We skip `Script_UseContainerItem` — that function is a state-machine
// dispatcher for cursor modes (repair vendor, spell-cast targeting,
// drop-on-bag, ...) that don't apply to an addon-issued call from a
// clean cursor state. The default-path primitive `FUN_ITEM_USE`
// (`0x005D8D00`) handles every item-type-specific branch internally:
// food / potion / hearthstone / scroll / quiver all route through
// the same entry.
//
// Target arg: `FUN_ITEM_USE`'s `param_1` is read as a `uint64_t *`
// pointing at the target's GUID. The engine writes it through to the
// "use item" event (`DAT_00c4c1d0`/`DAT_00c4c1d4` + event 0x122) for
// spell-cast items, and into the cast-targeting helper at
// `FUN_006e5a90`. For self-use items (hearthstone, food, potion) the
// engine overwrites with the item's own GUID before dispatch, so a
// stray target is harmless. We default to a zero GUID when the caller
// omits `unit` — matches existing call sites.
//
// Returns nothing. Silently no-ops on bad input, an item the player doesn't
// have, item locked, on cooldown, etc. An unrecognized `unit` string
// is treated as "no target" rather than raising, matching arg-1's
// "silently no-ops on bad input" contract.

#include "Game.h"
#include "Offsets.h"
#include "item/Location.h"
#include "unit/TokenResolve.h"

#include <cstdint>

namespace Item::Use {

namespace {

using UseItem_t = unsigned(__thiscall *)(const void *item, const uint64_t *targetGuid,
                                          int flag);

using TokenToGUID_t = uint64_t(__fastcall *)(const char *token);

uint64_t ResolveUnitGuid(void *L, int idx) {
    if (!Game::Lua::IsString(L, idx))
        return 0;
    const char *token = Game::Lua::ToString(L, idx);
    // The engine's resolver RAISES for a string that names no token, which
    // would turn a bad `unit` into an error out of a function whose contract
    // is to no-op on bad input — and this is reached from secure-button
    // attributes and key bindings, where the string is whatever an addon
    // configured. Probe first so an unusable one is simply no target.
    if (!Unit::TokenResolve::IsUnitToken(token))
        return 0;
    auto fn = reinterpret_cast<TokenToGUID_t>(Offsets::FUN_TOKEN_TO_GUID);
    return fn(token);
}

int __fastcall Script_C_Item_UseItemByName(void *L) {
    // Read the unit first: the item resolve below stomps the Lua stack.
    const uint64_t targetGuid = ResolveUnitGuid(L, 2);

    const uint8_t *item = Item::Location::ResolveItemArgOrLocation(L, 1);
    if (item == nullptr) {
        return 0;
    }

    auto useItem = reinterpret_cast<UseItem_t>(Offsets::FUN_ITEM_USE);
    useItem(item, &targetGuid, 0);
    return 0;
}

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "UseItemByName", &Script_C_Item_UseItemByName);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::Use
