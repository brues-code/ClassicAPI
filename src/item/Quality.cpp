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
#include "item/Arg.h"
#include "item/Data.h"
#include "item/ID.h"
#include "item/Location.h"
#include "item/Record.h"

#include <cstdint>

namespace Item::Quality {

namespace {

int PushQualityForItemID(void *L, int itemID) {
    if (itemID <= 0)
        return 0;
    const uint8_t *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr) {
        Item::Data::WarmCache(static_cast<uint32_t>(itemID));
        return 0;
    }
    const uint32_t quality = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_QUALITY);
    Game::Lua::PushNumber(L, static_cast<double>(quality));
    return 1;
}

// Item-quality enum values from `Enum.ItemQuality` (modern WoW). These
// are the integer codes returned in the 4th slot of `GetItemInfo` /
// `C_Item.GetItemInfoInstant`'s quality field, and used by addons to
// gate UI behavior on rarity (e.g., "highlight rare-or-better drops").
//
// Values 0..5 (POOR..LEGENDARY) exist as actual item qualities in
// vanilla 1.12. Higher values (ARTIFACT, HEIRLOOM, WOWTOKEN) were
// introduced in later expansions — we still expose them so that
// addons backporting modern code (`if quality >= LE_ITEM_QUALITY_RARE
// then ...`) compile cleanly. Vanilla items will never carry those
// higher quality values, so the comparisons resolve to the right
// branch automatically.
struct QualityConstant {
    const char *name;
    int value;
};

constexpr QualityConstant kQualityConstants[] = {
    {"LE_ITEM_QUALITY_POOR",      0}, // gray   — junk
    {"LE_ITEM_QUALITY_COMMON",    1}, // white
    {"LE_ITEM_QUALITY_UNCOMMON",  2}, // green
    {"LE_ITEM_QUALITY_RARE",      3}, // blue
    {"LE_ITEM_QUALITY_EPIC",      4}, // purple
    {"LE_ITEM_QUALITY_LEGENDARY", 5}, // orange
    {"LE_ITEM_QUALITY_ARTIFACT",  6}, // gold     — TBC+ only
    {"LE_ITEM_QUALITY_HEIRLOOM",  7}, // light blue — WotLK+ only
    {"LE_ITEM_QUALITY_WOWTOKEN",  8}, // orange   — WoD+ only
};

} // namespace

// `C_Item.GetItemQuality(itemLocation)` — direct cache read for the
// equipped/bagged item's quality, no `GetItemInfo` chaining. Returns
// nil on empty / uncached / invalid input.
static int __fastcall Script_C_Item_GetItemQuality(void *L) {
    if (!Item::Location::IsLocationArg(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Item.GetItemQuality(itemLocation)");
        return 0;
    }
    return PushQualityForItemID(L, Item::ID::FromCGItem(Item::Location::Resolve(L, 1)));
}

// `C_Item.GetItemQualityByID(itemInfo)` — same as the above but takes
// the itemID (or `"item:NNN..."` string) directly. Returns nil for
// uncached items, firing a background cache fill so a follow-up call
// after `GET_ITEM_INFO_RECEIVED` returns the value.
static int __fastcall Script_C_Item_GetItemQualityByID(void *L) {
    return PushQualityForItemID(L, Item::Arg::ResolveItemID(L, 1));
}

// --- Documentation ----------------------------------------------------------

static const Game::Doc::Field kGetItemQualityArgs[] = {
    Game::Doc::Req("itemLocation", "ItemLocation", "An item location or an item GUID."),
};
static const Game::Doc::Field kGetItemQualityRets[] = {
    Game::Doc::Opt("itemQuality", "ItemQuality", nullptr,
                   "Nil for an empty slot or an item that is not cached."),
};
static const Game::Doc::Function kGetItemQuality{
    "Returns the quality of the item at a location.", kGetItemQualityArgs,
    kGetItemQualityRets};

static const Game::Doc::Field kGetItemQualityByIDArgs[] = {
    Game::Doc::Req("itemInfo", "ItemInfo", "An item ID or an item link."),
};
static const Game::Doc::Field kGetItemQualityByIDRets[] = {
    Game::Doc::Opt("itemQuality", "ItemQuality", nullptr, "Nil while the item is not cached."),
};
static const Game::Doc::Function kGetItemQualityByID{
    "Returns the quality of an item, and asks the server for an item that is not cached.",
    kGetItemQualityByIDArgs, kGetItemQualityByIDRets};

static void RegisterLuaFunctions() {
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    for (const auto &c : kQualityConstants)
        Game::Lua::SetGlobalNumber(L, c.name, static_cast<double>(c.value));
    Game::Lua::RegisterTableFunction("C_Item", "GetItemQuality", &Script_C_Item_GetItemQuality,
                                     &kGetItemQuality);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemQualityByID",
                                     &Script_C_Item_GetItemQualityByID, &kGetItemQualityByID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::Quality
