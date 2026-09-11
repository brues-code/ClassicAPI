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
// model needed. Item classification reuses `Locations::FindGUID`,
// which walks the player's flat inventory + bag invMgrs the same way
// `GetItemCount` does, so bank items count as "in inventory" without
// needing the bank window open.
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

#include "Game.h"
#include "Offsets.h"
#include "equipmentset/Data.h"
#include "equipmentset/Locations.h"
#include "equipmentset/Set.h"
#include "item/Record.h"

#include <cstdint>

namespace EquipmentSet::Tooltip {

namespace {

using AddLine_t = void(__thiscall *)(void *self, const char *left, const char *right,
                                     const void *leftColor, const void *rightColor, int wrap);
using ClearTooltip_t = void(__fastcall *)(void *self);
using ShowScript_t = int(__fastcall *)(void *L); // engine's Script_Show

// Packs r,g,b (0..1) into the 0xAARRGGBB value FUN_GAMETOOLTIP_ADD_LINE's
// color arg wants — its little-endian bytes are the {b,g,r,a} the engine's
// line-color setter reads. Alpha is always opaque.
uint32_t Pack(double r, double g, double b) {
    auto ch = [](double v) {
        const int i = static_cast<int>(v * 255.0 + 0.5);
        return static_cast<uint32_t>(i < 0 ? 0 : (i > 255 ? 255 : i));
    };
    return 0xFF000000u | (ch(r) << 16) | (ch(g) << 8) | ch(b);
}

// Left-aligned colored line via the engine's raw add-line. The packed
// color is a local so its address stays valid across the call.
void AddColoredLine(void *self, const char *text, double r, double g, double b) {
    const uint32_t color = Pack(r, g, b);
    reinterpret_cast<AddLine_t>(Offsets::FUN_GAMETOOLTIP_ADD_LINE)(
        self, text, nullptr, &color, nullptr, 0);
}

// Adds one localized "N unit" line: `string.format(_G[globalName] or
// fallback, n)` (the project's GlobalString localize-or-fallback helper,
// with the C `fallback` for servers stripped of the standard strings),
// then the native colored add-line.
void AddLocalizedLineInt(void *L, void *self, const char *globalName,
                         const char *fallback, int n, double r, double g, double b) {
    const int savedTop = Game::Lua::GetTop(L);
    Game::Lua::PushLocalizedFormatInt(L, globalName, fallback, n);
    const char *text = Game::Lua::ToString(L, -1);
    if (text != nullptr)
        AddColoredLine(self, text, r, g, b);
    Game::Lua::SetTop(L, savedTop);
}

struct Tally {
    int equipped;
    int inInventory;
    int ignored;
    int missing;
    // ItemIDs of slots classified as missing. Indexed 0..missing-1.
    // Capacity matches SLOT_COUNT (worst case: every slot is missing).
    // Stays 0 for entries loaded from pre-itemID-format files.
    uint32_t missingItemIDs[SLOT_COUNT];
};

// Walks a set's slots, classifying each non-empty entry into the
// equipped / in-inventory / ignored / missing buckets via
// `Locations::FindGUID`. For missing slots, also records the saved
// itemID so the tooltip can resolve names. Stack-free.
Tally TallySet(const Set &s) {
    Tally t{};
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const uint64_t g = s.items[i];
        if (g == GUID_IGNORED) {
            t.ignored++;
            continue;
        }
        if (g == GUID_EMPTY)
            continue;
        const int loc = Locations::FindGUID(g);
        if (loc == 0) {
            // Live GUID isn't in the player's object map. The saved
            // itemID (if any) is the only handle we have for naming.
            t.missingItemIDs[t.missing++] = s.itemIDs[i];
            continue;
        }
        // LOC_BAGS covers backpack / player bags / bank bags; LOC_BANK
        // alone (no LOC_BAGS) covers main bank slots. Both count as
        // "in inventory" for the tooltip summary — matches the modern
        // category where everything-but-equipped is one bucket.
        if ((loc & (LOC_BAGS | LOC_BANK)) != 0) {
            t.inInventory++;
        } else {
            t.equipped++;
        }
    }
    return t;
}

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

    const Tally t = TallySet(*s);
    const int totalSlots = t.equipped + t.inInventory + t.missing;

    // Header — set name, white. Clear first so it lands as line 0
    // (the clear resets the line count the add-line body indexes from).
    reinterpret_cast<ClearTooltip_t>(Offsets::FUN_GAMETOOLTIP_CLEAR)(self);
    AddColoredLine(self, s->name.c_str(), 1.0, 1.0, 1.0);

    // Body lines — try Blizzard's localized FORMAT_* globals first.
    // Same key names retail FrameXML uses (`ITEMS_VARIABLE_QUANTITY`,
    // `ITEMS_EQUIPPED`, ...); addons that need their own wording can
    // override by reassigning `_G[name]` at runtime. The fallbacks
    // only fire on servers stripped of the standard GlobalStrings.
    AddLocalizedLineInt(L, self, "ITEMS_VARIABLE_QUANTITY", "%d items",
                        totalSlots, 1.0, 1.0, 1.0);

    if (t.equipped > 0) {
        AddLocalizedLineInt(L, self, "ITEMS_EQUIPPED", "%d equipped",
                            t.equipped, 0.0, 1.0, 0.0); // green
    }
    if (t.inInventory > 0) {
        AddLocalizedLineInt(L, self, "ITEMS_IN_INVENTORY", "%d in inventory",
                            t.inInventory, 1.0, 1.0, 1.0);
    }
    if (t.ignored > 0) {
        AddLocalizedLineInt(L, self, "ITEM_SLOTS_IGNORED", "%d slots ignored",
                            t.ignored, 0.5, 0.5, 0.5); // gray
    }
    // Missing slots: list each by name using the itemID we stored at
    // save time, then look the name up in the item cache. Matches the
    // 4.3.4 behavior of one `ITEM_MISSING` line per missing item.
    // Slots loaded from pre-itemID files have `itemID == 0`; for those
    // we can't render a name, so we fall back to summarizing them as
    // a `(N unnamed missing)` line below.
    int unnamedMissing = 0;
    for (int i = 0; i < t.missing; ++i) {
        const uint32_t id = t.missingItemIDs[i];
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
            AddColoredLine(self, line, 1.0, 0.0, 0.0); // red
        Game::Lua::SetTop(L, savedTop);
    }
    if (unnamedMissing > 0) {
        // Pre-itemID-format files or items the cache has no record of.
        // No Blizzard format global maps cleanly here; addons can
        // override with `_G.CLASSICAPI_EQUIPMENTSET_MISSING`.
        AddLocalizedLineInt(L, self, "CLASSICAPI_EQUIPMENTSET_MISSING",
                            "%d missing", unnamedMissing, 1.0, 0.0, 0.0);
    }

    // Show ourselves — self is still at Lua stack index 1 (Script_Show
    // reads it there). Retail's SetEquipmentSet shows internally too.
    reinterpret_cast<ShowScript_t>(Offsets::FUN_SCRIPT_FRAME_SHOW)(L);
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetEquipmentSet", &Script_GameTooltipSetEquipmentSet},
};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_GAMETOOLTIP_METHOD_REGISTRY),
        g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace EquipmentSet::Tooltip
