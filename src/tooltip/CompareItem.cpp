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

// `GameTooltip:SetHyperlinkCompareItem("itemLink" [, offset, shiftButton, comparisonTooltip])`
// — a backport of the 3.3.5+ tooltip method that powers item-comparison
// ("shopping") tooltips. The compared item comes from `itemLink` when
// given; otherwise it's taken from the item the `comparisonTooltip` (4th
// positional arg — the tooltip FrameXML's GameTooltip_ShowCompareItem
// passes) is displaying, so the method works purely tooltip-to-tooltip
// with no link, matching the retail call. Called on a tooltip frame (typically
// ShoppingTooltip1/2, but works on any GameTooltip-type frame), it:
//
//   1. Parses the hovered item link and finds which equipment slot(s)
//      that item would occupy (via its inventoryType).
//   2. Picks the slot indicated by `offset` (1-based; two-slot items —
//      rings, trinkets, one-hand weapons — expose slots at offset 1 and
//      2, so a caller iterates offset = 1 .. returnValue).
//   3. Fills the tooltip with the item currently EQUIPPED in that slot,
//      led by a grey "Currently Equipped" header — done by calling the
//      engine's own item-tooltip builder (FUN_GAMETOOLTIP_BUILD_ITEM) in
//      its header mode. No Lua roundtrip.
//   4. Appends colored per-stat delta lines — yellow ITEM_DELTA_DESCRIPTION
//      header, then green for a gain / red for a loss — showing how the
//      hovered item compares to the equipped one. The delta is
//      `hovered − equipped`, computed by the shared `Item::StatAccum`
//      engine (the same one behind `C_Item.GetItemStatDelta`); the lines
//      are added with the engine's raw add-line (FUN_GAMETOOLTIP_ADD_LINE).
//
// Returns the number of comparison slots for the hovered item (0 if it
// isn't equippable, isn't cached yet, the tooltip object can't be
// resolved, or nothing is equipped in the chosen slot) — a ClassicAPI
// extension over the retail signature, so a caller knows whether a second
// offset is worth querying.
//
// Everything is done with direct engine calls (the builder, clear, and
// add-line functions the engine uses itself) — no `lua_pcall` into
// registered Lua methods. The tooltip's final size/show is left to the
// caller (matching retail `SetHyperlinkCompareItem`, which populates but
// doesn't show). The 60-line pool from Tooltip::LinePool keeps the
// appended deltas under the AddLine cap.
//
// `shiftButton`: gates the stat-change breakdown. Deltas show by default
// (`true`, `1`, `nil`, or omitted) and are hidden only for an explicit
// `false` or `0` — just the grey header + the equipped item's tooltip.
// Both the boolean and the vanilla 1/nil convention are accepted —
// mirroring FrameXML's GameTooltip_ShowCompareItem.

#include "Game.h"
#include "Offsets.h"
#include "item/Arg.h"
#include "item/CGItem.h"
#include "item/ID.h"
#include "item/Location.h"
#include "item/Record.h"
#include "item/StatAccum.h"
#include "item/TooltipItem.h"
#include "tooltip/SetEvents.h"
#include "ui/ColorData.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Tooltip::Compare {

namespace {

using Item::StatAccum::Accum;

// The engine's item-tooltip builder. __thiscall(self = tooltip object).
// With headerFlag != 0 it skips its leading clear and emits the grey
// "Currently Equipped" header; the item itself is looked up from itemID +
// guid regardless. See OFF/FUN notes in Offsets.h.
using BuildItemTooltip_t = void(__thiscall *)(void *self, uint32_t itemID, const void *guid,
                                              const void *guid2, int a4, int a5, int a6,
                                              int headerFlag, int a8, int a9);
// Raw add-line. Color args are pointers to a 4-byte packed {b,g,r,a}.
using AddLine_t = void(__thiscall *)(void *self, const char *left, const char *right,
                                     const void *leftColor, const void *rightColor, int wrap);
using SetLineText_t = void(__thiscall *)(void *fontString, const char *text, int flag);
using SetLineColor_t = void(__thiscall *)(void *fontString, const void *colorBGRA);

// inventoryType (ItemStats_C m_inventoryType) → equipment slot ID(s).
// Slot IDs are the engine's canonical INVSLOT values (1 = Head … 19 =
// Tabard). Two-slot types (finger, trinket, one-hand weapon) fill both
// entries and return 2. Returns 0 for non-equippable types (bag, ammo,
// quiver, non-equip). `out` must hold 2 ints.
int SlotsForInvType(int invType, int *out) {
    switch (invType) {
    case 1:  out[0] = 1;  return 1;              // HEAD
    case 2:  out[0] = 2;  return 1;              // NECK
    case 3:  out[0] = 3;  return 1;              // SHOULDER
    case 4:  out[0] = 4;  return 1;              // BODY (shirt)
    case 5:  case 20: out[0] = 5; return 1;      // CHEST / ROBE
    case 6:  out[0] = 6;  return 1;              // WAIST
    case 7:  out[0] = 7;  return 1;              // LEGS
    case 8:  out[0] = 8;  return 1;              // FEET
    case 9:  out[0] = 9;  return 1;              // WRIST
    case 10: out[0] = 10; return 1;              // HANDS
    case 11: out[0] = 11; out[1] = 12; return 2; // FINGER
    case 12: out[0] = 13; out[1] = 14; return 2; // TRINKET
    case 16: out[0] = 15; return 1;              // CLOAK (back)
    case 13: out[0] = 16; out[1] = 17; return 2; // ONE-HAND WEAPON (main/off)
    case 17: out[0] = 16; return 1;              // TWO-HAND WEAPON
    case 21: out[0] = 16; return 1;              // WEAPONMAINHAND
    case 14: out[0] = 17; return 1;              // SHIELD (off hand)
    case 22: out[0] = 17; return 1;              // WEAPONOFFHAND
    case 23: out[0] = 17; return 1;              // HOLDABLE (off hand)
    case 15: out[0] = 18; return 1;              // RANGED
    case 25: out[0] = 18; return 1;              // THROWN
    case 26: out[0] = 18; return 1;              // RANGEDRIGHT (wands)
    case 28: out[0] = 18; return 1;              // RELIC
    case 19: out[0] = 19; return 1;              // TABARD
    default: return 0;                           // 0 non-equip, 18 bag, 24 ammo, 27 quiver
    }
}

// English fallback labels, used when the client doesn't define the
// modern `_G[key]` global-string (vanilla 1.12 largely doesn't). Phrasing
// mirrors pfUI's eqcompare so the deltas read consistently on this
// client. Percent stats carry NO "%" here — `IsPercentStat` appends the
// "%" to the value ("-5% Block"), so the unit is correct regardless of
// which label (Blizzard `_SHORT` or this fallback) is used.
struct KeyLabel {
    const char *key;
    const char *label;
};
constexpr KeyLabel kFallbackLabels[] = {
    {"ITEM_MOD_MANA_SHORT", "Mana"},
    {"ITEM_MOD_HEALTH_SHORT", "Health"},
    {"ITEM_MOD_AGILITY_SHORT", "Agility"},
    {"ITEM_MOD_STRENGTH_SHORT", "Strength"},
    {"ITEM_MOD_INTELLECT_SHORT", "Intellect"},
    {"ITEM_MOD_SPIRIT_SHORT", "Spirit"},
    {"ITEM_MOD_STAMINA_SHORT", "Stamina"},
    {"RESISTANCE0_NAME", "Armor"},
    {"RESISTANCE1_NAME", "Holy Resistance"},
    {"RESISTANCE2_NAME", "Fire Resistance"},
    {"RESISTANCE3_NAME", "Nature Resistance"},
    {"RESISTANCE4_NAME", "Frost Resistance"},
    {"RESISTANCE5_NAME", "Shadow Resistance"},
    {"RESISTANCE6_NAME", "Arcane Resistance"},
    {"ITEM_MOD_ATTACK_POWER_SHORT", "Attack Power"},
    {"ITEM_MOD_RANGED_ATTACK_POWER_SHORT", "Ranged Attack Power"},
    {"ITEM_MOD_CRIT_MELEE_RATING", "Melee Crit"},
    {"ITEM_MOD_CRIT_RANGED_RATING", "Ranged Crit"},
    {"ITEM_MOD_HIT_MELEE_RATING", "Melee Hit"},
    {"ITEM_MOD_HIT_RANGED_RATING", "Ranged Hit"},
    {"ITEM_MOD_HIT_SPELL_RATING", "Spell Hit"},
    {"ITEM_MOD_CRIT_SPELL_RATING", "Spell Crit"},
    {"ITEM_MOD_SPELL_DAMAGE_DONE_SHORT", "Spell Damage"},
    {"ITEM_MOD_SPELL_HEALING_DONE_SHORT", "Spell Healing"},
    {"ITEM_MOD_MANA_REGENERATION", "Mana Regen"},
    {"ITEM_MOD_DEFENSE_SKILL_RATING", "Defense"},
    {"ITEM_MOD_DODGE_RATING", "Dodge"},
    {"ITEM_MOD_PARRY_RATING", "Parry"},
    {"ITEM_MOD_BLOCK_RATING", "Block"},
    {"ITEM_MOD_BLOCK_VALUE", "Block Value"},
    {"ITEM_MOD_DAMAGE_PER_SECOND_SHORT", "Damage Per Second"},
    {"ITEM_DELTA_DESCRIPTION",
     "If you replace this item, the following stat changes will occur:"},
};

// Percent-valued stat keys: the block/dodge/parry *chances* and the
// crit/hit percentages. Vanilla stores these as native percentages (a
// value of 5 means 5%), so the delta must read "-5% Block", not "-5
// Block". The "%" is attached to the NUMBER rather than the label, so it
// stays correct in every locale and requires no `_SHORT` change.
// NB: ITEM_MOD_DEFENSE_SKILL_RATING (skill points) and
// ITEM_MOD_BLOCK_VALUE (flat block amount) are NOT percentages.
bool IsPercentStat(const char *key) {
    static const char *const kPercent[] = {
        "ITEM_MOD_CRIT_MELEE_RATING",  "ITEM_MOD_CRIT_RANGED_RATING",
        "ITEM_MOD_CRIT_SPELL_RATING",  "ITEM_MOD_HIT_MELEE_RATING",
        "ITEM_MOD_HIT_RANGED_RATING",  "ITEM_MOD_HIT_SPELL_RATING",
        "ITEM_MOD_DODGE_RATING",       "ITEM_MOD_PARRY_RATING",
        "ITEM_MOD_BLOCK_RATING",
    };
    for (const char *k : kPercent)
        if (std::strcmp(k, key) == 0)
            return true;
    return false;
}

const char *FallbackLabel(const char *key) {
    for (const auto &e : kFallbackLabels)
        if (std::strcmp(e.key, key) == 0)
            return e.label;
    return key; // last resort — raw key beats crashing
}

// True if `s` contains a printf conversion (`%d`, `%s`, `%1$s`, …) as
// opposed to a literal `%` used as a unit. A `%` at end-of-string, before
// a space, or escaped as `%%` is treated as literal. Lets locale labels
// like "Dodge %" through while still rejecting a format-string global
// such as "+%d Strength" (which would render garbage via snprintf "%s").
bool HasFormatSpecifier(const char *s) {
    for (const char *p = std::strchr(s, '%'); p != nullptr;
         p = std::strchr(p + 1, '%')) {
        const char n = p[1];
        if (n == '%') { // escaped "%%" — skip both, keep scanning
            ++p;
            continue;
        }
        if (n != '\0' && n != ' ')
            return true; // "%d", "%s", "%1$..." → real conversion
    }
    return false;
}

bool EndsWithShort(const char *key) {
    const size_t n = std::strlen(key);
    return n >= 6 && std::strcmp(key + n - 6, "_SHORT") == 0;
}

// Raw `_G[key]` string fetch — no format-specifier filtering. Direct
// globals-table access, no pcall. Returns whether a non-empty string was
// copied into `out`.
bool TryGlobalRaw(void *L, const char *key, char *out, int outSize) {
    const int saved = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, key);
    Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
    const char *g = (Game::Lua::Type(L, -1) == Game::Lua::TYPE_STRING)
                        ? Game::Lua::ToString(L, -1)
                        : nullptr;
    const bool ok = (g != nullptr && *g != '\0');
    if (ok)
        std::snprintf(out, outSize, "%s", g);
    Game::Lua::SetTop(L, saved);
    return ok;
}

// Copies `_G[key]` into `out` iff it's a plain, non-empty, non-format
// string. Returns whether it took.
bool TryGlobalLabel(void *L, const char *key, char *out, int outSize) {
    char tmp[192];
    if (!TryGlobalRaw(L, key, tmp, sizeof(tmp)) || HasFormatSpecifier(tmp))
        return false;
    std::snprintf(out, outSize, "%s", tmp);
    return true;
}

// Formats a signed value token (e.g. "-5") as a localized percentage via
// the PERCENTAGE_STRING global (typically "%s%%" → "-5%"). The locale
// format is NEVER passed to snprintf — we substitute `num` into its first
// conversion slot (`%s`/`%d`) by hand and collapse `%%` → `%`, so a
// hostile or unexpected format can't misread the argument list. Falls back
// to a plain "%" suffix when the global is absent.
void FormatPercent(void *L, const char *num, char *out, int outSize) {
    char fmt[64];
    if (!TryGlobalRaw(L, "PERCENTAGE_STRING", fmt, sizeof(fmt))) {
        std::snprintf(out, outSize, "%s%%", num);
        return;
    }
    int o = 0;
    bool inserted = false;
    for (const char *p = fmt; *p != '\0' && o < outSize - 1; ++p) {
        if (p[0] == '%' && p[1] == '%') { // literal percent
            out[o++] = '%';
            ++p;
        } else if (p[0] == '%' && p[1] != '\0' && !inserted) { // value slot
            for (const char *q = num; *q != '\0' && o < outSize - 1; ++q)
                out[o++] = *q;
            ++p; // consume the conversion letter (s / d)
            inserted = true;
        } else {
            out[o++] = *p;
        }
    }
    out[o] = '\0';
}

// Resolve a display label for a stat `key` into `out`, respecting
// localization. The rating/regen tokens GetItemStats keys on
// (`ITEM_MOD_CRIT_MELEE_RATING`, …) carry a "%s" *description* as their
// own global; their `_SHORT` sibling is the compact label a delta wants.
// So for a key that isn't already `_SHORT`, prefer `_G[key.."_SHORT"]`:
//   1. `_G[key.."_SHORT"]` — the compact label (skipped if `key` already
//      ends in `_SHORT`, e.g. the primary stats, or has no `_SHORT`
//      variant like `RESISTANCE*_NAME` / `CURRENTLY_EQUIPPED`).
//   2. `_G[key]` — the key itself (primary stats resolve here; rating
//      tokens only land here if no `_SHORT` global exists).
//   3. Built-in English `FallbackLabel(key)`.
// Preferring `_SHORT` first (rather than only when `_G[key]` fails) means
// a server/addon defining the base rating token as a plain string can't
// shadow the compact `_SHORT` label. StatAccum keys stay the canonical
// GetItemStats tokens; this `_SHORT` mapping is display-only.
void LabelFor(void *L, const char *key, char *out, int outSize) {
    if (!EndsWithShort(key)) {
        char shortKey[160];
        std::snprintf(shortKey, sizeof(shortKey), "%s_SHORT", key);
        if (TryGlobalLabel(L, shortKey, out, outSize))
            return;
    }
    if (TryGlobalLabel(L, key, out, outSize))
        return;
    std::snprintf(out, outSize, "%s", FallbackLabel(key));
}

// Delta colors — the canonical GlobalColor.dbc tags modern WoW uses for
// item comparison, pulled from UI::ColorData (its packed argb is already
// the {b,g,r,a} order the tooltip line-color setter wants).
constexpr uint32_t kGain = UI::ColorData::ByTag("INCREASE_STAT_COLOR"); // green
constexpr uint32_t kLoss = UI::ColorData::ByTag("DECREASE_STAT_COLOR"); // red
constexpr uint32_t kHeader = UI::ColorData::ByTag("NORMAL_FONT_COLOR"); // header yellow

// self:AddLine equivalent — left-aligned colored line via the engine's
// raw add-line. `color` must outlive the call (we pass its address).
void AddColoredLine(void *self, const char *text, const uint32_t &color) {
    auto add = reinterpret_cast<AddLine_t>(Offsets::FUN_GAMETOOLTIP_ADD_LINE);
    add(self, text, nullptr, &color, nullptr, 0);
}

// Appends the yellow ITEM_DELTA_DESCRIPTION header + one line per non-zero
// stat and a DPS line. No-op when nothing differs (identical items /
// self-compare add no delta section).
void RenderDeltas(void *self, void *L, const Accum *acc, double dpsDelta) {
    const bool hasDps = (dpsDelta > 1e-4 || dpsDelta < -1e-4);
    bool hasAny = hasDps;
    for (int i = 0; !hasAny && i < Item::StatAccum::kCount; ++i)
        hasAny = (acc[i].value != 0);
    if (!hasAny)
        return;

    char label[192];
    char line[256];

    LabelFor(L, "ITEM_DELTA_DESCRIPTION", label, sizeof(label));
    AddColoredLine(self, label, kHeader);

    if (hasDps) {
        LabelFor(L, "ITEM_MOD_DAMAGE_PER_SECOND_SHORT", label, sizeof(label));
        std::snprintf(line, sizeof(line), "%s%.1f %s", dpsDelta > 0 ? "+" : "",
                      dpsDelta, label);
        AddColoredLine(self, line, dpsDelta > 0 ? kGain : kLoss);
    }

    for (int i = 0; i < Item::StatAccum::kCount; ++i) {
        const long v = acc[i].value;
        if (v == 0)
            continue;
        LabelFor(L, acc[i].key, label, sizeof(label));
        char num[48];
        std::snprintf(num, sizeof(num), "%s%ld", v > 0 ? "+" : "", v);
        if (IsPercentStat(acc[i].key)) {
            char pct[64];
            FormatPercent(L, num, pct, sizeof(pct));
            std::snprintf(num, sizeof(num), "%s", pct);
        }
        std::snprintf(line, sizeof(line), "%s %s", num, label);
        AddColoredLine(self, line, v > 0 ? kGain : kLoss);
    }
}

// --- "Currently Equipped" header ---------------------------------------
//
// The engine's builder can emit this header itself (FUN_0052B650 param_7),
// but that path merges it into the item's name line and force-wraps it.
// So we build the item cleanly and prepend the header the way vanilla
// addons (pfUI eqcompare) do — shift every built line's text+color down
// one slot and claim line 0 — but entirely in C via the engine's line
// FontStrings, no Lua method calls.

const char *FSText(const void *fs) {
    if (fs == nullptr)
        return nullptr;
    return Game::Read<const char *>(fs, Offsets::OFF_FONTSTRING_TEXT);
}

// Whether a line's FontString is actually shown. The tooltip clear hides
// unused cells but keeps their (now stale) text, so this — not FSText —
// is what decides whether a cell carries live content to shift.
bool FSVisible(const void *fs) {
    if (fs == nullptr)
        return false;
    return Game::Read<int>(fs, Offsets::OFF_FONTSTRING_VISIBLE) != 0;
}

// Reads a FontString's current color into `*out` (4-byte {b,g,r,a}).
// Returns false when the color storage isn't set, so the caller leaves
// the destination's existing color alone.
bool FSColor(const void *fs, uint32_t *out) {
    if (fs == nullptr)
        return false;
    auto *slot = Game::Read<const uint32_t *>(fs, Offsets::OFF_FONTSTRING_COLOR_PTR);
    if (slot == nullptr)
        return false;
    *out = *slot;
    return true;
}

void FSSetText(void *fs, const char *text) {
    if (fs != nullptr)
        reinterpret_cast<SetLineText_t>(Offsets::FUN_FONTSTRING_SET_TEXT)(fs, text, 0);
}

void FSSetColor(void *fs, const uint32_t &color) {
    if (fs != nullptr)
        reinterpret_cast<SetLineColor_t>(Offsets::FUN_FONTSTRING_SET_COLOR)(fs, &color);
}

using ShowHideFS_t = void(__fastcall *)(void *fs);

// Realize a FontString's shown state the way the engine's AddLine/clear
// pair do: set the desired-shown flag, then call SHOW or HIDE so the
// FontString gets positioned (or dropped from) the layout. Without this a
// cell moved into a previously-hidden slot never anchors.
void FSSetShown(void *fs, bool shown) {
    if (fs == nullptr)
        return;
    Game::Ref<int>(fs, Offsets::OFF_FONTSTRING_SHOWN_FLAG) = shown ? 1 : 0;
    reinterpret_cast<ShowHideFS_t>(
        shown ? Offsets::FUN_FONTSTRING_SHOW : Offsets::FUN_FONTSTRING_HIDE)(fs);
}

// Copies `src`'s content (text + color) onto `dst`, replicating the shown
// state so `dst` positions correctly; hides `dst` when `src` isn't a live
// (shown) cell. Keyed on visibility, not text, so stale text the clear
// left behind in a hidden cell isn't resurrected. One column of the shift.
void CopyLineCell(void *dst, const void *src) {
    if (dst == nullptr)
        return;
    if (FSVisible(src)) {
        const char *text = FSText(src);
        FSSetText(dst, text != nullptr ? text : "");
        // Always set a color: source's if it has one, else white — so a
        // stale color left on `dst` by a previous delta line is reset
        // (lines the builder leaves at the default have no readable color).
        uint32_t color = UI::ColorData::ByTag("WHITE_FONT_COLOR");
        FSColor(src, &color);
        FSSetColor(dst, color);
        FSSetShown(dst, true);
    } else {
        FSSetText(dst, "");
        FSSetShown(dst, false);
    }
}

void PrependCurrentlyEquipped(void *self, void *L) {
    auto *t = static_cast<uint8_t *>(self);
    int *numLines = Game::Ptr<int>(t, Offsets::OFF_GAMETOOLTIP_NUM_LINES);
    const int allocated =
        Game::Read<int>(t, Offsets::OFF_GAMETOOLTIP_NUM_LINES_ALLOC);
    const int n = *numLines;
    if (n <= 0 || n + 1 > allocated)
        return; // nothing built, or pool full (LinePool keeps 60, so rare)

    auto **leftArr = Game::Read<void **>(
        t, Offsets::OFF_GAMETOOLTIP_TEXTLEFT_DESC + 8);
    auto **rightArr = Game::Read<void **>(
        t, Offsets::OFF_GAMETOOLTIP_TEXTRIGHT_DESC + 8);
    auto *wrapArr = Game::Read<int *>(
        t, Offsets::OFF_GAMETOOLTIP_WRAPFLAG_DESC + 8);
    if (leftArr == nullptr || rightArr == nullptr || wrapArr == nullptr)
        return;

    // Shift lines [0..n-1] down to [1..n]. High-to-low so each source is
    // read before the next iteration overwrites it.
    for (int i = n - 1; i >= 0; --i) {
        CopyLineCell(leftArr[i + 1], leftArr[i]);
        CopyLineCell(rightArr[i + 1], rightArr[i]);
        wrapArr[i + 1] = wrapArr[i];
    }

    char header[128];
    LabelFor(L, "CURRENTLY_EQUIPPED", header, sizeof(header)); // native 1.12 string
    constexpr uint32_t grey = UI::ColorData::ByTag("GRAY_FONT_COLOR");
    FSSetText(leftArr[0], header);
    FSSetColor(leftArr[0], grey);
    FSSetShown(leftArr[0], true);
    FSSetText(rightArr[0], "");
    FSSetShown(rightArr[0], false);
    wrapArr[0] = 0;
    *numLines = n + 1;
}

// Reads the live random-property (suffix) index off an equipped CGItem's
// descriptor — the same index ItemRandomProperties.dbc / ApplyRandomSuffix
// use. 0 for a plain item or a null descriptor.
int EquippedSuffix(const uint8_t *item) {
    auto *desc = Item::ObjectFields(item);
    if (desc == nullptr)
        return 0;
    return static_cast<int>(
        Game::Read<uint32_t>(desc, Offsets::OFF_DESCRIPTOR_RANDOM_PROPERTY));
}

uint64_t EquippedGUID(const uint8_t *item) {
    auto *instance = Item::InstanceBlock(item);
    if (instance == nullptr)
        return 0;
    return Game::Read<uint64_t>(instance, Offsets::OFF_INSTANCE_BLOCK_GUID);
}

int __fastcall Script_SetHyperlinkCompareItem(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(
            L, "Usage: GameTooltip:SetHyperlinkCompareItem(\"itemLink\" [, offset, shiftButton, comparisonTooltip])");
        return 0;
    }
    const int argc = Game::Lua::GetTop(L);

    auto bail = [&](int n) -> int {
        Game::Lua::SetTop(L, argc);
        Game::Lua::PushNumber(L, static_cast<double>(n));
        return 1;
    };

    // Compared ("hovered") item: prefer the explicit link (arg 2). When
    // it's absent, fall back to whatever the comparisonTooltip (arg 5) is
    // displaying — the builder stashes that tooltip's itemID/GUID, so we
    // recover the itemID directly and the suffix via the GUID's live
    // CGItem. This makes the method 1:1 with the retail call, which can be
    // driven purely tooltip-to-tooltip.
    int hoveredID = Item::Arg::ResolveItemID(L, 2);
    int hoveredSuffix = Item::StatAccum::ParseRandomSuffixFromLink(L, 2);
    if (hoveredID <= 0 && Game::Lua::Type(L, 5) == Game::Lua::TYPE_TABLE) {
        // Typed: `CurrentID` reads tooltip fields off this object, so a
        // non-tooltip here would read them out of unrelated memory. Silent —
        // the argument is optional and we already fall through without it.
        if (void *cmp = Game::Lua::ResolveTooltip(L, 5, /*raiseError=*/false)) {
            uint64_t g = 0;
            hoveredID = Item::TooltipItem::CurrentID(cmp, &g);
            if (g != 0) {
                const uint8_t *cg = Item::Location::ResolveByGUID(g);
                if (cg != nullptr)
                    hoveredSuffix = EquippedSuffix(cg);
            }
        }
    }
    const uint8_t *hoveredRec =
        hoveredID > 0 ? Item::PeekRecord(static_cast<uint32_t>(hoveredID)) : nullptr;
    if (hoveredRec == nullptr)
        return bail(0); // no link and no comparisonTooltip item, or not cached yet

    const int invType = static_cast<int>(
        Game::Read<uint32_t>(hoveredRec, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE));
    int slots[2] = {0, 0};
    const int nSlots = SlotsForInvType(invType, slots);
    if (nSlots == 0)
        return bail(0); // non-equippable

    int offset = (Game::Lua::Type(L, 3) == Game::Lua::TYPE_NUMBER)
                     ? static_cast<int>(Game::Lua::ToNumber(L, 3))
                     : 1;
    if (offset < 1)
        offset = 1;
    if (offset > nSlots)
        return bail(nSlots);
    const int slotID = slots[offset - 1];

    // shiftButton: gates the stat-change breakdown. Deltas show by default
    // — for `true`, `1`, `nil`, or omitted — and are hidden ONLY for an
    // explicit `false` or `0`. Accepting both the boolean and the vanilla
    // 1/nil convention matches how callers (and FrameXML's
    // GameTooltip_ShowCompareItem) forward the modified-click state.
    bool showDeltas = true;
    const int shiftType = Game::Lua::Type(L, 4);
    if (shiftType == Game::Lua::TYPE_BOOLEAN)
        showDeltas = Game::Lua::ToBoolean(L, 4) != 0;
    else if (shiftType == Game::Lua::TYPE_NUMBER)
        showDeltas = Game::Lua::ToNumber(L, 4) != 0.0;

    // What's equipped there? Nothing → no comparison; leave the tooltip
    // as the caller had it.
    const uint8_t *eqItem = Item::Location::ResolveEquipmentSlot(slotID);
    if (eqItem == nullptr)
        return bail(nSlots);
    const int equippedID = Item::ID::FromCGItem(eqItem);
    if (equippedID <= 0)
        return bail(nSlots);
    const int equippedSuffix = EquippedSuffix(eqItem);
    uint64_t equippedGUID = EquippedGUID(eqItem);

    // The tooltip's underlying CFrameScriptObject — the `this` for the
    // engine's builder / add-line thiscalls.
    void *self = Game::Lua::ResolveTooltip(L);
    if (self == nullptr)
        return bail(0);

    // Build the equipped item into the tooltip via the engine's own
    // item-tooltip builder (the call SetInventoryItem/SetHyperlink use),
    // proven arg shape from FUN_005353b0/FUN_00535700 — all flags 0. This
    // clears + fills the item cleanly (no wrap). The grey "Currently
    // Equipped" header is prepended afterwards. Suppress OnTooltipSetItem for
    // this internal build so a handler doesn't run mid-way through the
    // line-shift below (it fires for the *hovered* item's real Set* instead).
    {
        Tooltip::SetEvents::Suppressor noEvent;
        reinterpret_cast<BuildItemTooltip_t>(Offsets::FUN_GAMETOOLTIP_BUILD_ITEM)(
            self, static_cast<uint32_t>(equippedID), &equippedGUID, &equippedGUID, 0, 0, 0, 0, 0, 0);
    }
    PrependCurrentlyEquipped(self, L);

    // Delta = hovered − equipped, then render (unless suppressed).
    if (showDeltas) {
        Accum acc[Item::StatAccum::kCount];
        Item::StatAccum::Init(acc);
        Item::StatAccum::AccumulateRecord(acc, hoveredRec, +1);
        Item::StatAccum::ApplyRandomSuffix(acc, hoveredSuffix, +1);
        const uint8_t *equippedRec =
            Item::PeekRecord(static_cast<uint32_t>(equippedID));
        if (equippedRec != nullptr) {
            Item::StatAccum::AccumulateRecord(acc, equippedRec, -1);
            Item::StatAccum::ApplyRandomSuffix(acc, equippedSuffix, -1);
        }
        const double dpsDelta = Item::StatAccum::ComputeDPS(hoveredRec) -
                                (equippedRec ? Item::StatAccum::ComputeDPS(equippedRec) : 0.0);
        RenderDeltas(self, L, acc, dpsDelta);
    }
    return bail(nSlots);
}

// `GameTooltip:IsEquippedItem()` → bool. True when the item the tooltip is
// currently displaying is equipped in a character-pane slot. The displayed
// item is resolved by Item::TooltipItem::CurrentID (which guards the stale
// item-GUID so unit / spell / gameobject tooltips read 0 and never
// false-positive). Backports the 3.3.5 GameTooltip method (registry
// index 63); the equipment walk mirrors C_Item.IsEquippedItem's itemID
// path.
int __fastcall Script_IsEquippedItem(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: GameTooltip:IsEquippedItem()");
        return 0;
    }
    void *self = Game::Lua::ResolveTooltip(L);
    const int itemID = self != nullptr ? Item::TooltipItem::CurrentID(self, nullptr) : 0;

    bool equipped = false;
    for (int slot = Offsets::EQUIPMENT_SLOT_FIRST;
         itemID > 0 && slot <= Offsets::EQUIPMENT_SLOT_LAST; ++slot) {
        const uint8_t *it = Item::Location::ResolveEquipmentSlot(slot);
        if (it != nullptr && Item::ID::FromCGItem(it) == itemID) {
            equipped = true;
            break;
        }
    }
    Game::Lua::PushBool(L, equipped);
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetHyperlinkCompareItem", &Script_SetHyperlinkCompareItem},
    {"IsEquippedItem", &Script_IsEquippedItem},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_GAMETOOLTIP_METHOD_REGISTRY),
        g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Tooltip::Compare
