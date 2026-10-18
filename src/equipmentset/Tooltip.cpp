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

// `GameTooltip:SetEquipmentSet(name)` — modern method that fills the
// tooltip with summary info for a named equipment set. Mirrors the
// 4.3.4 implementation at `0x0046E690`, which dispatches to an inner
// builder (`FUN_0046CEB0`) producing this shape:
//
//   [Set Name]                          (header)
//   N items                              (ITEMS_VARIABLE_QUANTITY)
//   N equipped                           (ITEMS_EQUIPPED, green)
//   N in inventory                       (ITEMS_IN_INVENTORY)
//   N slots ignored                      (ITEM_SLOTS_IGNORED, gray)
//   N missing                            (ITEM_MISSING, red — counted,
//                                         not listed individually)
//
// We back the implementation with the same equipment-set storage
// `C_EquipmentSet.*` already uses (`EquipmentSet::Data`) — no new data
// model needed. The counts are `Locations::GetStatus`, 4.3.4's
// `GetSetStatus`, shared with `C_EquipmentSet.GetEquipmentSetInfo`.
//
// Stock 4.3.4 enumerates each missing item by name (looks up the
// itemID via the engine's full item-cache name resolver). Our storage
// records only GUIDs, so we count missing items but don't reproduce
// each line — listing names would require persisting itemIDs
// alongside GUIDs in the WTF file, a storage-format change we punt
// on. Modern addons that need the missing-item names can iterate
// `C_EquipmentSet.GetItemIDs` + `C_EquipmentSet.GetItemLocations`
// themselves.
//
// Tooltip lines are built with the engine's own primitives — the
// per-tooltip clear (FUN_GAMETOOLTIP_CLEAR) and the raw add-line
// (FUN_GAMETOOLTIP_ADD_LINE), the same native path SetTotem and
// SetHyperlinkCompareItem use; no Lua method dispatch. It shows itself
// at the end (Script_Show), matching retail: PaperDollFrame.lua sets the
// anchor then calls SetEquipmentSet with no trailing :Show().

#include "equipmentset/Tooltip.h"

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "equipmentset/Data.h"
#include "equipmentset/Locations.h"
#include "equipmentset/Set.h"
#include "equipmentset/Text.h"
#include "item/Record.h"
#include "tooltip/Lines.h"
#include "ui/ColorData.h"

#include <cstdint>
#include <string>

namespace EquipmentSet::Tooltip {

namespace {

// Line colors, packed 0xAARRGGBB as ::Tooltip::Lines::Add wants.
constexpr uint32_t kWhite = 0xFFFFFFFFu;
constexpr uint32_t kGreen = 0xFF00FF00u;
constexpr uint32_t kGray = 0xFF808080u;
constexpr uint32_t kRed = 0xFFFF0000u;
// 4.3.4's tooltip title color, which its "Equipment Sets:" line uses.
constexpr uint32_t kNormal = UI::ColorData::ByTag("NORMAL_FONT_COLOR");

// Adds one localized "N unit" line: `string.format(_G[globalName] or
// fallback, n)` (the project's GlobalString localize-or-fallback helper,
// with the C `fallback` for servers stripped of the standard strings),
// then the native colored add-line.
void AddLocalizedLineInt(void *L, void *self, const char *globalName,
                         const char *fallback, int n, const uint32_t &color) {
    const int savedTop = Game::Lua::GetTop(L);
    Game::Lua::PushLocalizedFormatInt(L, globalName, fallback, n);
    const char *text = Game::Lua::ToString(L, -1);
    if (text != nullptr)
        ::Tooltip::Lines::Add(self, text, color);
    Game::Lua::SetTop(L, savedTop);
}

} // namespace

int __fastcall Script_GameTooltipSetEquipmentSet(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: GameTooltip:SetEquipmentSet(\"setName\")");
        return 0;
    }
    if (Game::Lua::Type(L, 2) != Game::Lua::TYPE_STRING) {
        Game::Lua::Error(L, "Usage: GameTooltip:SetEquipmentSet(\"setName\")");
        return 0;
    }
    const char *setName = Game::Lua::ToString(L, 2);
    if (setName == nullptr)
        return 0;

    const Set *s = Data::FindByName(setName);
    if (s == nullptr)
        return 0;

    void *self = Game::Lua::ResolveTooltip(L);
    if (self == nullptr)
        return 0;

    const Locations::Status t = Locations::GetStatus(*s);
    const int totalSlots = t.total;

    // Header — set name, white. Clear first so it lands as line 0
    // (the clear resets the line count the add-line body indexes from).
    ::Tooltip::Lines::Clear(self);
    ::Tooltip::Lines::Add(self, s->name.c_str(), kWhite);

    // Body lines — try Blizzard's localized FORMAT_* globals first.
    // Same key names retail FrameXML uses (`ITEMS_VARIABLE_QUANTITY`,
    // `ITEMS_EQUIPPED`, ...); addons that need their own wording can
    // override by reassigning `_G[name]` at runtime. The fallbacks
    // only fire on servers stripped of the standard GlobalStrings.
    AddLocalizedLineInt(L, self, "ITEMS_VARIABLE_QUANTITY", "%d items",
                        totalSlots, kWhite);

    if (t.equipped > 0) {
        AddLocalizedLineInt(L, self, "ITEMS_EQUIPPED", "%d equipped",
                            t.equipped, kGreen);
    }
    if (t.inInventory > 0) {
        AddLocalizedLineInt(L, self, "ITEMS_IN_INVENTORY", "%d in inventory",
                            t.inInventory, kWhite);
    }
    if (t.ignored > 0) {
        AddLocalizedLineInt(L, self, "ITEM_SLOTS_IGNORED", "%d slots ignored",
                            t.ignored, kGray);
    }
    // Missing slots: list each by name using the itemID we stored at
    // save time, then look the name up in the item cache. Matches the
    // 4.3.4 behavior of one `ITEM_MISSING` line per missing item.
    // Slots loaded from pre-itemID files have `itemID == 0`; for those
    // we can't render a name, so we fall back to summarizing them as
    // a `(N unnamed missing)` line below.
    int unnamedMissing = 0;
    for (int i = 0; i < t.missing; ++i) {
        const uint32_t id = s->itemIDs[t.missingSlots[i] - 1];
        if (id == 0) {
            unnamedMissing++;
            continue;
        }
        auto *record = Item::PeekRecord(id);
        if (record == nullptr) {
            unnamedMissing++;
            continue;
        }
        const char *name = Game::Read<const char *>(
            record, Offsets::OFF_ITEMSTATS_NAME);
        if (name == nullptr || name[0] == '\0') {
            unnamedMissing++;
            continue;
        }
        // Build "Missing: <name>" using Blizzard's `ITEM_MISSING` format
        // global. Wrap inline since AddLocalizedLineInt only handles
        // %d args — we need %s here.
        const int savedTop = Game::Lua::GetTop(L);
        Game::Lua::PushString(L, "format");
        Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
        Game::Lua::PushLocalizedString(L, "ITEM_MISSING", "Missing: %s");
        Game::Lua::PushString(L, name);
        Game::Lua::Call(L, 2, 1);
        const char *line = Game::Lua::ToString(L, -1);
        if (line != nullptr)
            ::Tooltip::Lines::Add(self, line, kRed);
        Game::Lua::SetTop(L, savedTop);
    }
    if (unnamedMissing > 0) {
        // Pre-itemID-format files or items the cache has no record of.
        // No Blizzard format global maps cleanly here; addons can
        // override with `_G.CLASSICAPI_EQUIPMENTSET_MISSING`.
        AddLocalizedLineInt(L, self, "CLASSICAPI_EQUIPMENTSET_MISSING",
                            "%d missing", unnamedMissing, kRed);
    }

    // Show ourselves — self is still at Lua stack index 1 (Script_Show
    // reads it there). Retail's SetEquipmentSet shows internally too.
    ::Tooltip::Lines::Show(L);
    return 0;
}

namespace {

CVar::Factory::Handle g_hideSetsCVar = nullptr;

} // namespace

void AfterItemBuild(void *self, const void *itemGuidPtr) {
    if (itemGuidPtr == nullptr || CVar::Factory::GetInt(g_hideSetsCVar) != 0)
        return;
    const uint64_t itemGuid = *static_cast<const uint64_t *>(itemGuidPtr);
    // The builder adds no lines when it bailed on an item-cache miss; it
    // runs again once the item arrives.
    if (itemGuid == 0 || Game::Read<int>(self, Offsets::OFF_GAMETOOLTIP_NUM_LINES) == 0)
        return;
    const std::string list = Text::SetList(itemGuid);
    if (list.empty())
        return;
    const std::string line = Text::FormatS(
        Text::Global("EQUIPMENT_SETS", "Equipment Sets: |cFFFFFFFF%s|r"), list);
    ::Tooltip::Lines::Add(self, line.c_str(), kNormal, /*wrap*/ true);
}

namespace {

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetEquipmentSet", &Script_GameTooltipSetEquipmentSet},
};

} // namespace

static void RegisterLuaFunctions() {
    // 4.3.4's switch for the item-tooltip line; dedups by name on /reload.
    g_hideSetsCVar = CVar::Factory::Register("dontShowEquipmentSetsOnItems", "0");
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_GAMETOOLTIP_METHOD_REGISTRY),
        g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace EquipmentSet::Tooltip
