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

// `GetSendMailItemLink([attachmentIndex])` — returns
// `(link, itemID)` for the item currently attached to the outgoing-
// mail slot. `link` is the fully-decorated per-instance hyperlink
// (enchant / random suffix preserved). Modern WoW expanded mail to up
// to 12 attachments and added the index arg; vanilla 1.12 allows
// exactly one, so we accept the arg for signature parity but treat
// only index 1 as meaningful (any other index returns nothing).
//
// `itemID` as a second return is a ClassicAPI extension on top of
// modern's link-only signature — saves callers a `string.match` to
// parse it back out of the link.
//
// The attached item still exists as a live CGItem in the player's
// inventory until the mail is actually sent — the engine stores its
// GUID at `VAR_SEND_MAIL_ITEM_GUID_LO/HI`. We resolve the GUID via
// `FUN_GET_OBJECT_BY_GUID(OBJECT_TYPE_ITEM, ...)` and feed the
// resulting CGItem to the standard `Item::Link::FromCGItem` builder,
// so enchant / random-suffix / unique-ID decoration matches what
// `GetContainerItemLink` would return for the same item back in
// the player's bag.

#include "Game.h"
#include "Offsets.h"
#include "item/ID.h"
#include "item/Link.h"

#include <cstdint>

namespace Mail::SendMailItemLink {

namespace {

using GetObjectByGuid_t = void *(__fastcall *)(uint32_t typeMask, const char *debugMsg,
                                                uint32_t guidLo, uint32_t guidHi,
                                                int debugCode);

int __fastcall Script_GetSendMailItemLink(void *L) {
    // Optional 1-based attachment index. Anything other than 1 is a
    // no-op (vanilla is single-slot). Missing arg = treat as index 1.
    if (Game::Lua::IsNumber(L, 1)) {
        const int idx = static_cast<int>(Game::Lua::ToNumber(L, 1));
        if (idx != 1)
            return 0;
    }

    const uint32_t guidLo = *reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_SEND_MAIL_ITEM_GUID_LO));
    const uint32_t guidHi = *reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_SEND_MAIL_ITEM_GUID_HI));
    if (guidLo == 0 && guidHi == 0)
        return 0;

    auto fn = reinterpret_cast<GetObjectByGuid_t>(Offsets::FUN_GET_OBJECT_BY_GUID);
    auto *cgItem = static_cast<const uint8_t *>(
        fn(Offsets::OBJECT_TYPE_ITEM, nullptr, guidLo, guidHi, 0));
    if (cgItem == nullptr)
        return 0;

    const char *link = Item::Link::FromCGItem(cgItem);
    if (link == nullptr || *link == '\0')
        return 0;
    const int itemID = Item::ID::FromCGItem(cgItem);
    Game::Lua::PushString(L, link);
    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    return 2;
}

const Game::Doc::Field kGetSendMailItemLinkArgs[] = {
    Game::Doc::Opt("attachmentIndex", "luaIndex", nullptr,
                   "Only 1 returns an item; a mail has one attachment."),
};
const Game::Doc::Field kGetSendMailItemLinkRets[] = {
    Game::Doc::Opt("itemLink", "string", nullptr,
                   "Nil, with itemID, when no item is attached."),
    Game::Doc::Opt("itemID", "number", nullptr, "A ClassicAPI extension."),
};
const Game::Doc::Function kGetSendMailItemLink{
    "Returns the item link of the item attached to the mail that the player writes.",
    kGetSendMailItemLinkArgs, kGetSendMailItemLinkRets, "MailGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetSendMailItemLink",
                                       &Script_GetSendMailItemLink, &kGetSendMailItemLink);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Mail::SendMailItemLink
