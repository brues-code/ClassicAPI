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

// `C_Container.SetItemSearch(searchString)`,
// `C_Container.IsContainerFiltered(containerIndex)` and
// `INVENTORY_SEARCH_UPDATE` — 4.3.4's bag search (`CGContainerInfo`,
// SetItemSearch 0x009BB230, IsItemFiltered 0x009BE790, ItemRecIsFiltered
// 0x009BD3C0, Script_IsContainerFiltered 0x009BE8D0). It also feeds the
// `isFiltered` field of `C_Container.GetContainerItemInfo`.
//
// One search covers every container. `SetItemSearch` folds the text once
// (case, and accents for the client's locale) and fires the event when the
// folded text changes; an empty string clears it. An item matches when the
// folded text is a substring of any of these, in 4.3.4's order:
//   1. the name of an equipment set that holds this very item (by GUID, so a
//      second copy of the same itemID does not count);
//   2. its full name, random suffix included;
//   3. its subclass's short name ("Bow", "Cloth") — skipped for cloaks and
//      for the rows flagged ITEMSUBCLASS_FLAG_NO_SEARCH_NAME (the melee
//      weapons), as 4.3.4 skips them;
//   4. its equip slot's name ("Head", "Two-Hand").
// An item whose static data isn't cached counts as a match, as in 4.3.4.
// Nothing re-checks when that data arrives: UIs re-read on BAG_UPDATE and on
// INVENTORY_SEARCH_UPDATE, which is all 4.3.4's FrameXML does.
//
// Two departures from 4.3.4:
//   - 4.3.4 folds the search text with its CHARACTER count as the byte size,
//     which cuts a non-ASCII search short; this folds the whole string.
//   - The keyring (-2) is searched like any bag: 1.12 still has it, and
//     GetContainerItemInfo already reports its slots. 4.3.4 had dropped it,
//     so its IsContainerFiltered reported it filtered.
//
// The search lives in the engine in 4.3.4, not the UI, so it survives
// /reload; logging out clears it without the event (4.3.4's LeaveWorld).

#include "container/Search.h"

#include "Game.h"
#include "Offsets.h"
#include "equipmentset/Data.h"
#include "event/Custom.h"
#include "item/CGItem.h"
#include "item/ID.h"
#include "item/Link.h"
#include "item/Location.h"
#include "item/Record.h"
#include "item/TypeInfo.h"
#include "unit/Identity.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <string>
#include <string_view>

namespace Container::Search {

namespace {

// 4.3.4's fold buffers hold 512 UTF-16 units; names and search text fit.
constexpr size_t kFoldCap = 512;

// The folded search text; empty means no search.
std::u16string g_search;

// 4.3.4's ToLower (0x004E6930): ASCII, Latin-1 0xC0..0xDE, Œ, Cyrillic.
char16_t ToLower(char16_t c) {
    if (static_cast<uint16_t>(c - u'A') <= 25 || static_cast<uint16_t>(c - 0xC0) <= 30)
        return static_cast<char16_t>(c + 32);
    if (c == 0x152)
        return 0x153;
    if (static_cast<uint16_t>(c - 0x410) <= 31)
        return static_cast<char16_t>(c + 32);
    if (c == 0x401)
        return 0x451;
    return c;
}

// Appends the lowercased `c` with 4.3.4's accent folding for `locale`
// (frFR 2, deDE 3, esES/esMX 6/7, ruRU 8); æ, œ and ß become two letters.
// Returns the new length.
size_t AppendFolded(int locale, char16_t c, char16_t *out, size_t n) {
    switch (locale) {
    case 2: // frFR
        switch (c) {
        case 0xE0: case 0xE2: case 0xE4: c = u'a'; break;
        case 0xE6: out[n++] = u'a'; c = u'e'; break;
        case 0xE7: c = u'c'; break;
        case 0xE8: case 0xE9: case 0xEA: case 0xEB: c = u'e'; break;
        case 0xEE: case 0xEF: c = u'i'; break;
        case 0xF2: case 0xF3: case 0xF4: case 0xF6: c = u'o'; break;
        case 0x153: out[n++] = u'o'; c = u'e'; break;
        case 0xF9: case 0xFA: case 0xFB: case 0xFC: c = u'u'; break;
        }
        break;
    case 3: // deDE
        if (c == 0xDF) {
            out[n++] = u's';
            c = u's';
        }
        break;
    case 6: // esES
    case 7: // esMX
        switch (c) {
        case 0xE1: c = u'a'; break;
        case 0xE9: c = u'e'; break;
        case 0xED: c = u'i'; break;
        case 0xF3: c = u'o'; break;
        case 0xFA: case 0xFC: c = u'u'; break;
        case 0xF1: c = u'n'; break;
        }
        break;
    case 8: // ruRU
        if (c == 0x451)
            c = 0x435;
        break;
    }
    out[n++] = c;
    return n;
}

// 4.3.4's CaseAccentInsensitiveParse (0x004E6990): UTF-8 → UTF-16 through the
// engine's converter, lowercased, then accent-folded. Writes `out`
// NUL-terminated and returns its length. 4.3.4 leaves koKR/zhCN/zhTW
// unlowercased and lets its case-insensitive SStrStrUTF8I match them; this
// lowercases every locale so a plain compare matches the same way.
size_t Fold(const char *text, char16_t (&out)[kFoldCap]) {
    using Convert_t = int(__stdcall *)(char16_t *dst, uint32_t cap, const char *src,
                                       uint32_t len, uint32_t *written, uint32_t *read);
    char16_t wide[kFoldCap] = {};
    // One unit short of the buffer: a full conversion stores no terminator.
    reinterpret_cast<Convert_t>(Offsets::FUN_SUNI_CONVERT_UTF8_TO_16)(
        wide, kFoldCap - 1, text, 0x7FFFFFFF, nullptr, nullptr);
    const int locale =
        *reinterpret_cast<const int *>(static_cast<uintptr_t>(Offsets::VAR_LOCALE_INDEX));
    size_t n = 0;
    for (size_t i = 0; wide[i] != 0 && n + 2 < kFoldCap; ++i)
        n = AppendFolded(locale, ToLower(wide[i]), out, n);
    out[n] = 0;
    return n;
}

// Whether the folded `text` contains the search.
bool Contains(const char *text) {
    if (text == nullptr || text[0] == '\0')
        return false;
    char16_t folded[kFoldCap];
    const size_t n = Fold(text, folded);
    return std::u16string_view(folded, n).find(g_search) != std::u16string_view::npos;
}

// Whether an equipment set that holds this very item has a matching name.
// The GUID test goes first: it is cheap, and most items are in no set.
bool InMatchingSet(const uint8_t *item) {
    const uint64_t guid = Item::Guid(item);
    if (guid == EquipmentSet::GUID_EMPTY || guid == EquipmentSet::GUID_IGNORED)
        return false;
    for (const EquipmentSet::Set &set : EquipmentSet::Data::All()) {
        const uint64_t *end = set.items + EquipmentSet::SLOT_COUNT;
        if (std::find(set.items, end, guid) != end && Contains(set.name.c_str()))
            return true;
    }
    return false;
}

// IsItemFiltered for an active search, with the equipment sets loaded.
bool Filtered(const uint8_t *item) {
    if (InMatchingSet(item))
        return false;
    const int itemID = Item::ID::FromCGItem(item);
    const uint8_t *record =
        itemID > 0 ? Item::PeekRecord(static_cast<uint32_t>(itemID)) : nullptr;
    if (record == nullptr)
        return false;
    char name[256];
    if (Item::Link::NameFromCGItem(item, name, sizeof(name)) && Contains(name))
        return false;
    const int invType = Game::Read<int>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);
    const uint8_t *subClass = Item::TypeInfo::FindSubClass(
        Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_CLASS),
        Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_SUBCLASS));
    if (subClass != nullptr && invType != Offsets::INVTYPE_CLOAK &&
        (Game::Read<uint32_t>(subClass, Offsets::OFF_ITEMSUBCLASS_FLAGS) &
         Offsets::ITEMSUBCLASS_FLAG_NO_SEARCH_NAME) == 0 &&
        Contains(Item::TypeInfo::SubClassShortName(subClass)))
        return false;
    return !Contains(Item::TypeInfo::InvTypeName(invType));
}

} // namespace

bool IsItemFiltered(const uint8_t *cgItem) {
    if (g_search.empty() || cgItem == nullptr)
        return false;
    EquipmentSet::Data::EnsureLoaded();
    return Filtered(cgItem);
}

namespace {

const Game::Doc::Event kSearchUpdateDoc{
    "Container", "Fires when the bag search text changes.", {}};
const Event::Custom::AutoReserve _evt{"INVENTORY_SEARCH_UPDATE", &kSearchUpdateDoc};

// How many slots of `bagID` to walk. PackBagSlot bounds a bag by the bag's
// own size, but the backpack and the main bank are ranges of the player's
// inventory that it bounds only by the whole inventory's size, so those two
// get their fixed sizes. The keyring is the inventory's last range, so the
// whole-inventory bound is its own. Any other index is no container.
int SlotsToWalk(int bagID) {
    if (bagID == Offsets::BAG_INDEX_BACKPACK)
        return Offsets::BACKPACK_NUM_SLOTS;
    if (bagID == Offsets::BAG_INDEX_BANK)
        return Offsets::INVMGR_BANK_MAIN_LAST_SLOT - Offsets::INVMGR_BANK_MAIN_FIRST_SLOT + 1;
    if (bagID == Offsets::BAG_INDEX_KEYRING ||
        (bagID >= Offsets::BAG_INDEX_BAG_FIRST && bagID <= Offsets::BAG_INDEX_BANK_BAG_LAST))
        return INT_MAX;
    return 0;
}

int __fastcall Script_SetItemSearch(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Container.SetItemSearch(searchString)");
        return 0;
    }
    char16_t folded[kFoldCap];
    const size_t n = Fold(Game::Lua::ToString(L, 1), folded);
    // 4.3.4 skips text equal to the current search ignoring case, and an
    // empty string while none is set. Equal folded text matches the same
    // items, so it is skipped here too.
    if (std::u16string_view(folded, n) == g_search)
        return 0;
    g_search.assign(folded, n);
    Event::Custom::Fire(_evt.Slot(), "");
    return 0;
}

// True when a search is active and no item in the container matches it —
// so an empty bag, an unknown index and a closed bank all read as filtered
// (the bank's items and its bags' GUIDs are gated on the bank being open).
int __fastcall Script_IsContainerFiltered(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Container.IsContainerFiltered(containerIndex)");
        return 0;
    }
    if (g_search.empty() || Unit::Identity::PlayerInventoryManager() == nullptr) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const int bagID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    EquipmentSet::Data::EnsureLoaded();
    const int slots = SlotsToWalk(bagID);
    Item::Location::BagSlotDetail d;
    for (int slot = 1;
         slot <= slots && Item::Location::ResolveBagSlotDetail(L, bagID, slot, &d); ++slot) {
        if (d.item != nullptr && !Filtered(d.item)) {
            Game::Lua::PushBool(L, false);
            return 1;
        }
    }
    Game::Lua::PushBool(L, true);
    return 1;
}

void ClearOnLogout() { g_search.clear(); }
const Game::GlueModuleAutoRegister _clearOnLogout{&ClearOnLogout};

const Game::Doc::Field kSetItemSearchArgs[] = {
    Game::Doc::Req("searchString", "cstring", "An empty string clears the search."),
};
const Game::Doc::Function kSetItemSearch{
    "Sets the text that the items in bags and the bank are matched against.",
    kSetItemSearchArgs, {}};

const Game::Doc::Field kIsContainerFilteredArgs[] = {
    Game::Doc::Req("containerIndex", "BagIndex"),
};
const Game::Doc::Field kIsContainerFilteredRets[] = {
    Game::Doc::Req("isFiltered", "bool",
                   "True when a search is active and no item in the container matches it."),
};
const Game::Doc::Function kIsContainerFiltered{
    "Whether the bag search hides every item in a container.", kIsContainerFilteredArgs,
    kIsContainerFilteredRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Container", "SetItemSearch", &Script_SetItemSearch,
                                     &kSetItemSearch);
    Game::Lua::RegisterTableFunction("C_Container", "IsContainerFiltered",
                                     &Script_IsContainerFiltered, &kIsContainerFiltered);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::Search
