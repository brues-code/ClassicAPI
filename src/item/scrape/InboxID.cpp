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

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Item::InboxID {

// `GetInboxItemID(mailID)` — itemID companion to `GetInboxItem`
// (which already returns name/texture/count/quality/canUse but not
// itemID). nil for empty / OOR mail slot, or when the mail entry
// has no attached item (gold-only mail).
static int __fastcall Script_GetInboxItemID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetInboxItemID(mailID)");
        return 0;
    }
    const int mailID = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    if (mailID < 0)
        return 0;

    const int count = static_cast<int>(Game::Read<uint32_t>(
        Offsets::VAR_INBOX_COUNT));
    if (mailID >= count)
        return 0;

    // `VAR_INBOX_ENTRIES` is a pointer slot, not the array — the
    // engine reads it via `MOV ECX, [imm32]; MOV EDI, [ECX + idx*4]`
    // (two derefs total before the entry pointer is in hand).
    auto **entries = Game::Read<const uint8_t **>(Offsets::VAR_INBOX_ENTRIES);
    if (entries == nullptr)
        return 0;
    auto *entry = entries[mailID];
    if (entry == nullptr)
        return 0;

    const int itemID = static_cast<int>(Game::Read<uint32_t>(
        entry, Offsets::OFF_INBOX_ENTRY_ITEM_ID));
    if (itemID == 0)
        return 0;

    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    return 1;
}

static const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("mailID", "luaIndex"),
};
static const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("itemID", "number", nullptr,
                   "Nil for a mail index out of range or a mail with no attached item."),
};
static const Game::Doc::Function kGetInboxItemID{
    "Returns the item ID of the item attached to an inbox mail.", kArgs, kRets, "ItemGlobals",
    true};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetInboxItemID", &Script_GetInboxItemID, &kGetInboxItemID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::InboxID
