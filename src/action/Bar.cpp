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

// `IsItemAction(slot)` / `HasBonusActionBar()` — the action-bar predicates
// from the newer clients' ActionBarFrame_Scripts, read off the same engine
// state the 1.12 readers use.
//
//   IsItemAction       1 when the 1-based slot holds an item, else nil; a
//                      non-number slot is a usage error. The item kind is
//                      the 0x80000000 tag in VAR_ACTION_TABLE.
//   HasBonusActionBar  1 while a bonus bar is active, else nil: the offset
//                      GetBonusBarOffset returns is nonzero.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Action::Bar {

namespace {

int PushFlag(void *L, bool on) {
    if (on)
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

int __fastcall Script_IsItemAction(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: IsItemAction(slot)");
        return 0;
    }

    const int slot0 = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    if (slot0 < 0 || slot0 >= Offsets::ACTION_TABLE_MAX_SLOTS)
        return PushFlag(L, false);
    const auto *table = reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_ACTION_TABLE));
    return PushFlag(L, (table[slot0] & Offsets::ACTION_TYPE_MASK) ==
                           Offsets::ACTION_TYPE_ITEM_BY_ID);
}

int __fastcall Script_HasBonusActionBar(void *L) {
    return PushFlag(L, Game::Read<uint32_t>(Offsets::VAR_BONUS_BAR_OFFSET) != 0);
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kSlotArgs[] = {
    Game::Doc::Req("slot", "number", "The action slot, 1 to 120."),
};
const Game::Doc::Field kIsItemRets[] = {
    Game::Doc::Opt("isItem", "number", nullptr, "1 if the slot holds an item, else nil."),
};
const Game::Doc::Function kIsItemAction{
    "Returns 1 if an action slot holds an item.",
    kSlotArgs, kIsItemRets, "ActionBar"};

const Game::Doc::Field kHasBonusRets[] = {
    Game::Doc::Opt("hasBonusBar", "number", nullptr,
                   "1 if a bonus action bar is active, else nil."),
};
const Game::Doc::Function kHasBonusActionBar{
    "Returns 1 if a bonus action bar is active, for example in a stance or form.",
    {}, kHasBonusRets, "ActionBar"};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsItemAction", &Script_IsItemAction, &kIsItemAction);
    Game::Lua::RegisterGlobalFunction("HasBonusActionBar", &Script_HasBonusActionBar,
                                      &kHasBonusActionBar);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Action::Bar
