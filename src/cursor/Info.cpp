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

// `GetCursorInfo()` — backport of the modern WoW global. Surfaces
// whatever the player has picked up (item, money, spell, macro,
// merchant slot) as a tuple. Read from the same cursor-state globals
// the engine's `CursorHasItem` / `CursorHasSpell` / `GetCursorMoney`
// consult — see the cursor type table in [[src/Offsets.h]].
//
// Return shape per type (matches modern Lua API where it makes sense
// for 1.12's actual storage):
//
//   nothing on cursor       — nil
//   "item", itemID, link    — bag item (type 1; link is the full
//                             per-instance form via the engine's link
//                             builder, including enchantID and
//                             random-suffix decoration)
//   "item", itemID          — drag-source item (types 5/6/7 trade /
//                             mail / action bar; type 9 equipment
//                             slot) — only the itemID is in cursor
//                             storage, no live CGItem to feed the
//                             link builder
//   "money", amount         — type 2; amount in copper
//   "spell", slot, bookType, spellID
//                           — type 3; bookType is "spell" or "pet"
//                             (1.12 only writes player-spell IDs to
//                             this slot, so bookType is always
//                             "spell" here; the cursor's "pet
//                             action" path is a distinct type)
//   "macro", index          — type 8; 1-based macro index
//   "merchant", index       — type 10; 1-based merchant-roster index
//
// Type 4 (pet action) is not surfaced — doesn't map cleanly to a
// modern `GetCursorInfo` string. Consumers can fall back to
// `CursorHasItem` / engine `PickupX` calls when they need to detect
// that.

#include "cursor/Info.h"

#include "Game.h"
#include "Offsets.h"
#include "item/ID.h"
#include "item/Link.h"
#include "item/Location.h"
#include "item/QualityColor.h"
#include "item/Record.h"
#include "spell/Lookup.h"

#include <cstdint>
#include <cstdio>

namespace Cursor::Info {

namespace {

// Engine signature is `__thiscall(this = typeFilter, guidLo, guidHi,
// unused)`. The "this" param is just a small integer filter — Ghidra
// shows it cast as `(void *)0x2` at every call site for items. The
// function reads only the first two stack args (`[ebp+8]` and
// `[ebp+0xC]`) but cleans 12 bytes via `ret 0xc`, so the caller must
// push a third dword for stack balance — engine callers push a
// scratch dword (`0x90`, `0x1C13`, etc.) for this. Declaring a 4th
// arg here makes MSVC emit the matching push; without it, stack
// imbalance of -4 bytes silently corrupts the caller's frame and
// (downstream) wedges things like chat input.
using ObjectFromGUID_t = uint8_t *(__thiscall *)(uintptr_t typeFilter,
                                                  uint32_t guidLo, uint32_t guidHi,
                                                  uint32_t unused);

constexpr uint32_t TYPE_EMPTY = 0;
constexpr uint32_t TYPE_BAG_ITEM = 1;
constexpr uint32_t TYPE_MONEY = 2;
constexpr uint32_t TYPE_SPELL = 3;
// Type 5 = merchant pickup. Written by `Script_PickupMerchantItem`
// (`0x004FB760`) via `FUN_004950F0(itemID, ?, slot0Based, 5)`. The
// itemID lands in `VAR_CURSOR_GENERIC_SLOT` and the 0-based merchant
// slot in `VAR_CURSOR_GENERIC_SOURCE` — we push slot+1 to Lua to
// match modern's 1-based merchant convention.
constexpr uint32_t TYPE_MERCHANT = 5;
// Types 6/7/9 — drag-source items from various pickup paths. All
// share the same storage trio. Type 7 is action-bar item pickup
// (`FUN_004E6130`), type 9 is equipment-slot pickup (`FUN_004C7300`),
// and type 6 is another drag-source path (mail or similar). In all
// cases the itemID is at `VAR_CURSOR_GENERIC_SLOT`. For modern API
// compat we surface them as `"item"`.
constexpr uint32_t TYPE_GENERIC_ITEM_LO = 6;
constexpr uint32_t TYPE_GENERIC_ITEM_HI = 7;
constexpr uint32_t TYPE_MACRO = 8;
constexpr uint32_t TYPE_INVENTORY_ITEM = 9;
// Type 4 is pet-action pickup and type 10 is stable-pet pickup
// (`FUN_00495010`, the source of the earlier misidentification as
// "merchant"). Neither has an equivalent `GetCursorInfo` return, so
// that function leaves both as nil — but `Enum.UICursorType` does name
// them, so `Current()` below reports them.
constexpr uint32_t TYPE_PET_ACTION = 4;
constexpr uint32_t TYPE_STABLE_PET = 10;

// `Enum.UICursorType` values, for `Current()`. The full enum is
// registered in [[cursor/Changed.cpp]]; only the handful the 1.12
// cursor can actually hold are named here.
constexpr int UI_DEFAULT = 0;
constexpr int UI_ITEM = 1;
constexpr int UI_MONEY = 2;
constexpr int UI_SPELL = 3;
constexpr int UI_PET_ACTION = 4;
constexpr int UI_MERCHANT = 5;
constexpr int UI_MACRO = 7;
constexpr int UI_PET = 9;

constexpr uint32_t GUID_TYPE_FILTER_ITEM = 0x2;

uint32_t ReadVar(uintptr_t va) {
    return *reinterpret_cast<const uint32_t *>(va);
}

// The macro cursor stores a macroID, not the slot Lua deals in (see
// `VAR_CURSOR_MACRO_ID`). Hand it back through the engine's own reverse
// scan of the slot map — the same conversion the legacy `CreateMacro`
// wrapper does to produce the index it returns to Lua. 0 when the
// macroID no longer maps to a slot, which is what an empty cursor
// reports too.
uint32_t CursorMacroSlot() {
    const uint32_t macroID = ReadVar(Offsets::VAR_CURSOR_MACRO_ID);
    if (macroID == 0)
        return 0;
    using IdToSlot_t = uint32_t(__fastcall *)(int macroID);
    const uint32_t slot0 = reinterpret_cast<IdToSlot_t>(
        static_cast<uintptr_t>(Offsets::FUN_MACRO_ID_TO_SLOT))(
        static_cast<int>(macroID));
    return (slot0 == 0xFFFFFFFFu) ? 0 : slot0 + 1;
}

int PushItem(void *L, const uint8_t *cgItem) {
    if (cgItem == nullptr)
        return 0;
    const int itemID = Item::ID::FromCGItem(cgItem);
    if (itemID <= 0)
        return 0;
    Game::Lua::PushString(L, "item");
    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    // Engine link builder works fine on cursored items — the cursor
    // leaves the instance block at `+0x08` and the m_objectFields
    // descriptor at `+0x114` intact, which is all the builder reads
    // (plus a virtual call into the sub-object at `+0x110`, whose
    // vftable is also untouched while held on the cursor). Earlier
    // crash repros were the stack-imbalance bug in the GUID resolver
    // — see Offsets::FUN_OBJECT_FROM_GUID.
    const char *link = Item::Link::FromCGItem(cgItem);
    if (link != nullptr && *link != '\0') {
        Game::Lua::PushString(L, link);
        return 3;
    }
    return 2;
}

int PushBagItemCursor(void *L) {
    const uint32_t lo = ReadVar(Offsets::VAR_CURSOR_ITEM_GUID_LO);
    const uint32_t hi = ReadVar(Offsets::VAR_CURSOR_ITEM_GUID_HI);
    if (lo == 0 && hi == 0)
        return 0;
    auto resolver = reinterpret_cast<ObjectFromGUID_t>(Offsets::FUN_OBJECT_FROM_GUID);
    const uint8_t *cgItem = resolver(GUID_TYPE_FILTER_ITEM, lo, hi, 0);
    return PushItem(L, cgItem);
}

// Builds a basic `"|cffRRGGBB|Hitem:N:0:0:0|h[Name]|h|r"` link from
// the cached ItemStats record for `itemID`. No enchant / random-
// suffix decoration — the cursor only stores the bare itemID for
// drag-source items (types 6/7/9) with no live `CGItem *` to read
// instance state from. Returns true and writes into `out` (size
// `outSize`) on success; returns false if the item isn't cached.
bool BuildBasicItemLink(uint32_t itemID, char *out, size_t outSize) {
    const uint8_t *record = Item::PeekRecord(itemID);
    if (record == nullptr) return false;
    const char *name = *reinterpret_cast<const char *const *>(
        record + Offsets::OFF_ITEMSTATS_NAME);
    if (name == nullptr || *name == '\0') return false;
    const uint32_t quality = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_QUALITY);
    const int n = std::snprintf(out, outSize,
        "%s|Hitem:%u:0:0:0|h[%s]|h|r",
        Item::QualityColor::Prefix(static_cast<int>(quality)),
        itemID, name);
    return n > 0 && static_cast<size_t>(n) < outSize;
}

// Shared path for types 6/7 (action bar / mail drag) and type 9
// (equipment slot). All three write the itemID at
// `VAR_CURSOR_GENERIC_SLOT` — see the type-table notes in
// [[src/Offsets.h]]. The cursor doesn't preserve a GUID for these,
// so the live CGItem isn't directly addressable here. To recover
// enchant / random-suffix decoration we walk the player's
// equipment + bags looking for the first CGItem with a matching
// itemID — same trick the action-bar tooltip uses to display
// enchanted bracers correctly when they're on an action bar. Falls
// back to a basic ID-only link when no instance is found (item was
// deleted, or it's a vendor-roster reference with no local copy).
int PushGenericItemCursor(void *L) {
    const uint32_t itemID = ReadVar(Offsets::VAR_CURSOR_GENERIC_SLOT);
    if (itemID == 0)
        return 0;
    Game::Lua::PushString(L, "item");
    Game::Lua::PushNumber(L, static_cast<double>(itemID));

    Item::Location::ByGUIDResult found;
    if (Item::Location::FindByItemID(L, static_cast<int>(itemID), &found)) {
        const char *link = Item::Link::FromCGItem(found.item);
        if (link != nullptr && *link != '\0') {
            Game::Lua::PushString(L, link);
            return 3;
        }
    }

    char linkBuf[256];
    if (BuildBasicItemLink(itemID, linkBuf, sizeof(linkBuf))) {
        Game::Lua::PushString(L, linkBuf);
        return 3;
    }
    return 2;
}

int PushMoneyCursor(void *L) {
    const uint32_t copper = ReadVar(Offsets::VAR_CURSOR_MONEY_COPPER);
    Game::Lua::PushString(L, "money");
    Game::Lua::PushNumber(L, static_cast<double>(copper));
    return 2;
}

int PushSpellCursor(void *L) {
    const uint32_t spellID = ReadVar(Offsets::VAR_CURSOR_SPELL_ID);
    if (spellID == 0)
        return 0;
    int bookType = 0;
    const int slot = Spell::Lookup::FindSpellbookSlot(
        static_cast<int>(spellID), &bookType);
    Game::Lua::PushString(L, "spell");
    // Slot can be 0 (rare — talent-passive auras the player has but
    // that have no spellbook entry). Push it anyway; the modern API's
    // contract is "slot may be 0 when there's no book row".
    Game::Lua::PushNumber(L, static_cast<double>(slot));
    Game::Lua::PushString(L, bookType == 1 ? "pet" : "spell");
    Game::Lua::PushNumber(L, static_cast<double>(spellID));
    return 4;
}

int PushMacroCursor(void *L) {
    // The 1-based slot, so the value feeds straight into `GetMacroInfo`
    // — the cursor global itself holds a macroID.
    const uint32_t slot = CursorMacroSlot();
    if (slot == 0)
        return 0;
    Game::Lua::PushString(L, "macro");
    Game::Lua::PushNumber(L, static_cast<double>(slot));
    return 2;
}

int PushMerchantCursor(void *L) {
    // Engine stores the 0-based merchant slot at
    // `VAR_CURSOR_GENERIC_SOURCE`. Convert to 1-based for the modern
    // Lua-API contract (`MerchantItemButton`s and friends are
    // 1-indexed).
    const uint32_t slot0 = ReadVar(Offsets::VAR_CURSOR_GENERIC_SOURCE);
    Game::Lua::PushString(L, "merchant");
    Game::Lua::PushNumber(L, static_cast<double>(slot0 + 1));
    return 2;
}

int __fastcall Script_GetCursorInfo(void *L) {
    const uint32_t type = ReadVar(Offsets::VAR_CURSOR_TYPE);
    if (type >= TYPE_GENERIC_ITEM_LO && type <= TYPE_GENERIC_ITEM_HI)
        return PushGenericItemCursor(L);
    switch (type) {
        case TYPE_BAG_ITEM:       return PushBagItemCursor(L);
        case TYPE_MERCHANT:       return PushMerchantCursor(L);
        case TYPE_INVENTORY_ITEM: return PushGenericItemCursor(L);
        case TYPE_MONEY:          return PushMoneyCursor(L);
        case TYPE_SPELL:          return PushSpellCursor(L);
        case TYPE_MACRO:          return PushMacroCursor(L);
        case TYPE_EMPTY:
        default:                  return 0;
    }
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetCursorInfo", &Script_GetCursorInfo);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

bool HasItem() {
    return ReadVar(Offsets::VAR_CURSOR_ITEM_GUID_LO) != 0 ||
           ReadVar(Offsets::VAR_CURSOR_ITEM_GUID_HI) != 0;
}

State Current() {
    const uint32_t type = ReadVar(Offsets::VAR_CURSOR_TYPE);
    switch (type) {
        case TYPE_BAG_ITEM: {
            const uint32_t lo = ReadVar(Offsets::VAR_CURSOR_ITEM_GUID_LO);
            const uint32_t hi = ReadVar(Offsets::VAR_CURSOR_ITEM_GUID_HI);
            if (lo == 0 && hi == 0)
                break; // type set but storage not written yet
            // Same resolve `Script_GetCursorInfo` does for this type —
            // FrameXML already calls into it from a CURSOR_UPDATE
            // handler, so the object manager is queryable at this point
            // in the dispatch.
            auto resolver =
                reinterpret_cast<ObjectFromGUID_t>(Offsets::FUN_OBJECT_FROM_GUID);
            const uint8_t *cgItem = resolver(GUID_TYPE_FILTER_ITEM, lo, hi, 0);
            const int itemID =
                (cgItem != nullptr) ? Item::ID::FromCGItem(cgItem) : 0;
            return {UI_ITEM, itemID > 0 ? static_cast<uint32_t>(itemID) : 0u};
        }
        case TYPE_GENERIC_ITEM_LO:
        case TYPE_GENERIC_ITEM_HI:
        case TYPE_INVENTORY_ITEM:
            return {UI_ITEM, ReadVar(Offsets::VAR_CURSOR_GENERIC_SLOT)};
        case TYPE_MONEY:
            return {UI_MONEY, ReadVar(Offsets::VAR_CURSOR_MONEY_COPPER)};
        case TYPE_SPELL:
            return {UI_SPELL, ReadVar(Offsets::VAR_CURSOR_SPELL_ID)};
        case TYPE_MERCHANT:
            // 1-based to match what `GetCursorInfo` pushes for this type.
            return {UI_MERCHANT,
                    ReadVar(Offsets::VAR_CURSOR_GENERIC_SOURCE) + 1};
        case TYPE_MACRO:
            return {UI_MACRO, CursorMacroSlot()};
        case TYPE_PET_ACTION:
            return {UI_PET_ACTION, 0};
        case TYPE_STABLE_PET:
            return {UI_PET, 0};
        case TYPE_EMPTY:
        default:
            break;
    }
    return {UI_DEFAULT, 0};
}

Raw ReadRaw() {
    return {ReadVar(Offsets::VAR_CURSOR_TYPE),
            ReadVar(Offsets::VAR_CURSOR_ITEM_GUID_LO),
            ReadVar(Offsets::VAR_CURSOR_ITEM_GUID_HI),
            ReadVar(Offsets::VAR_CURSOR_GENERIC_SLOT),
            ReadVar(Offsets::VAR_CURSOR_MONEY_COPPER),
            ReadVar(Offsets::VAR_CURSOR_SPELL_ID),
            ReadVar(Offsets::VAR_CURSOR_MACRO_ID)};
}

bool Same(const Raw &a, const Raw &b) {
    return a.type == b.type && a.guidLo == b.guidLo && a.guidHi == b.guidHi &&
           a.genericSlot == b.genericSlot && a.money == b.money &&
           a.spellID == b.spellID && a.macroID == b.macroID;
}

} // namespace Cursor::Info
