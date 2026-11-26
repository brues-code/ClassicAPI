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

// The C_ActionBar reads 3.80.2 has in C (`_classic_titan_` 3.80.2.70522
// ActionBarFrameDocumentation; the same arguments and returns in every later
// client), answering both ranges: slots 1..120 from the engine's own per-slot
// functions and caches, 121..360 from Action::Extended's store through
// Action::ExtendedState's mirrors of those functions. The 1.12 globals
// (GetActionCooldown, IsUsableAction, …) are not touched.
//
// Shape rules from 3.80.2's Lua functions (decompiled; GetActionCooldown
// FUN_140b1c090, GetActionCharges FUN_140b1be10, GetActionCooldownDuration
// FUN_140b1c310 → FUN_1417fb7f0, GetActionDisplayCount FUN_140b1c5a0,
// GetActionUseCount FUN_140b1d120, IsHelpfulAction FUN_140b1f2e0,
// IsHarmfulAction at 0x140b1f1f0):
//   - a non-number actionID raises the usage error; a slot outside the slots
//     returns nothing (every "RequiresValidActionSlot" read), except
//     IsHelpfulAction / IsHarmfulAction, which answer false.
//   - a macro resolved to an item by `#showtooltip` / `#show` answers that
//     item's cooldown, count and consumable test in both ranges, as the
//     1.12 globals do for 1..120 (Action::ItemState).
//
//   GetActionCooldown       SpellCooldownInfo (Spell::Cooldown::PushInfo):
//                           1..120 FUN_ACTION_SLOT_COOLDOWN, ours
//                           ExtendedState::CooldownFor. isOnGCD for a spell
//                           (the history's global leg), nil for an item, as
//                           3.80.2 sets it only when a category decided.
//   GetActionCooldownDuration(actionID [, ignoreGCD])
//                           a duration object of that cooldown; zero when it
//                           is not active, or when ignoreGCD and it is the GCD.
//   GetActionCharges        SpellChargeInfo. 1.12 has no charges: what 3.80.2's
//                           FUN_1417fb0b0 builds for no charge category (all 0,
//                           chargeModRate 1, isActive false).
//   IsUsableAction          the cached pair: 1..120 VAR_ACTION_USABLE /
//                           VAR_ACTION_NO_MANA (IsUsableAction's 0x004E5230),
//                           ours ExtendedState::CachedUsable.
//   IsActionInRange(actionID [, target])
//                           Action::Range::Answer with the target's GUID; nil
//                           when there is no answer.
//   HasRangeRequirements    FUN_ACTION_SLOT_HAS_RANGE / ExtendedState::HasRange.
//   GetActionText           1..120 Script_GetActionText (EquipmentSet's co-hook
//                           adds the set name); ours the same two branches.
//                           Nothing when there is no text.
//   GetActionUseCount       the cached count: VAR_ACTION_ITEM_COUNTS for an item
//                           word (GetActionCount's 0x004E6C70), ours
//                           ExtendedState::CachedCount.
//   GetActionDisplayCount(actionID [, maxDisplayCount, replacementString])
//                           3.80.2's rule: a consumable or stackable action,
//                           or one with no item and a count, shows its count,
//                           or replacementString (default "*") past
//                           maxDisplayCount (default 9999); anything else shows
//                           its charges when it has 2 or more, else "".
//   IsCurrentAction / IsAutoRepeatAction / IsAttackAction / IsEquippedAction
//                           FUN_ACTION_SLOT_IS_* / the ExtendedState mirrors.
//   IsConsumableAction      FUN_ACTION_SLOT_IS_CONSUMABLE / its item rule.
//   IsItemAction            the 0x80000000 tag.
//   IsHelpfulAction / IsHarmfulAction(actionID, useNeutral)
//                           3.80.2: a spell or item action (harmful also takes
//                           an attack or ranged-repeat action) is classified by
//                           its resolved spell — harmful first, then helpful,
//                           else neutral (FUN_141414330's order) — and a
//                           neutral spell counts when useNeutral is set. The
//                           classifier is this project's Spell::Info one.

#include "Game.h"
#include "Offsets.h"
#include "action/Extended.h"
#include "action/ExtendedState.h"
#include "action/Range.h"
#include "action/Slot.h"
#include "action/SlotItem.h"
#include "duration/Object.h"
#include "equipmentset/Action.h"
#include "item/Cooldown.h"
#include "macro/ShowTooltip.h"
#include "spell/Cooldown.h"
#include "spell/Info.h"
#include "spell/Lookup.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstdio>

namespace Action::BarReads {

namespace {

using SlotCooldown_t = void(__fastcall *)(uint32_t slot0, uint32_t *start, uint32_t *duration,
                                          uint32_t *enable);
using SlotTest_t = bool(__fastcall *)(uint32_t slot0);
using MacroIDToEntry_t = const uint8_t *(__fastcall *)(uint32_t macroID);

constexpr int kDefaultMaxDisplayCount = 9999;
constexpr const char *kDefaultReplacement = "*";

bool EngineSlot(int slot0) { return slot0 >= 0 && slot0 < Offsets::ACTION_TABLE_MAX_SLOTS; }

bool SlotTest(uintptr_t fn, int slot0) {
    return reinterpret_cast<SlotTest_t>(fn)(static_cast<uint32_t>(slot0));
}

uint32_t ItemOf(uint32_t word) {
    return word != 0 && (word & Offsets::ACTION_TYPE_MASK) == Offsets::ACTION_TYPE_ITEM_BY_ID
               ? word & Offsets::ACTION_PAYLOAD_MASK_ITEM_BY_ID
               : 0;
}

// The item a macro in `slot0` shows, or 0 (Action::ItemState's test).
uint32_t MacroItem(int slot0) {
    Macro::ShowTooltip::Info info;
    if (Macro::ShowTooltip::ForSlot(slot0, &info) &&
        info.target == Macro::ShowTooltip::Target::Item)
        return static_cast<uint32_t>(info.itemID);
    return 0;
}

// The 0-based slot of stack[1]; false after the usage error when it is not a
// number. `*valid` says whether it names a slot.
bool ActionArg(void *L, const char *usage, int *slot0, bool *valid) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, usage);
        return false;
    }
    *slot0 = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    *valid = Action::Slot::Valid(*slot0);
    return true;
}

// --- Cooldown -------------------------------------------------------------------

struct Cooldown {
    uint32_t startMs = 0;
    uint32_t durationMs = 0;
    bool enabled = false;
    bool hasGCD = false; // a spell decided it, so isOnGCD is reported
    bool onGCD = false;
};

Cooldown SlotCooldown(int slot0) {
    Cooldown cd;
    if (const uint32_t item = MacroItem(slot0)) {
        uint32_t enable = 0;
        Item::Cooldown::Query(static_cast<int>(item), &cd.startMs, &cd.durationMs, &enable);
        cd.enabled = enable != 0;
        return cd;
    }
    int spellID = 0;
    bool pet = false;
    if (EngineSlot(slot0)) {
        uint32_t enable = 0;
        reinterpret_cast<SlotCooldown_t>(Offsets::FUN_ACTION_SLOT_COOLDOWN)(
            static_cast<uint32_t>(slot0), &cd.startMs, &cd.durationMs, &enable);
        cd.enabled = enable != 0;
        if (ItemOf(Action::Slot::Word(slot0)) == 0) {
            uint32_t book = 0;
            const uint32_t spell = Action::Slot::SpellFor(slot0, &book);
            if (static_cast<int>(spell) > 0 && ::Spell::Lookup::RecordForID(static_cast<int>(spell))) {
                spellID = static_cast<int>(spell);
                pet = book != 0;
            }
        }
    } else {
        const ExtendedState::Cooldown ours = ExtendedState::CooldownFor(Extended::Get(slot0));
        cd.startMs = ours.startMs;
        cd.durationMs = ours.durationMs;
        cd.enabled = ours.enabled;
        spellID = ours.spellID;
        pet = ours.pet;
    }
    if (spellID > 0) {
        cd.hasGCD = true;
        cd.onGCD = ::Spell::Cooldown::ReportsGlobalCooldown(spellID, pet, cd.startMs, cd.durationMs);
    }
    return cd;
}

int __fastcall Script_GetActionCooldown(void *L) {
    int slot0 = 0;
    bool valid = false;
    if (!ActionArg(L, "Usage: local cooldownInfo = C_ActionBar.GetActionCooldown(actionID)", &slot0,
                   &valid) ||
        !valid)
        return 0;
    const Cooldown cd = SlotCooldown(slot0);
    ::Spell::Cooldown::PushInfo(L, cd.startMs, cd.durationMs, cd.enabled,
                                cd.hasGCD ? &cd.onGCD : nullptr);
    return 1;
}

int __fastcall Script_GetActionCooldownDuration(void *L) {
    int slot0 = 0;
    bool valid = false;
    if (!ActionArg(L,
                   "Usage: local duration = C_ActionBar.GetActionCooldownDuration(actionID [, "
                   "ignoreGCD])",
                   &slot0, &valid) ||
        !valid)
        return 0;
    const bool ignoreGCD = Game::Lua::ToBoolean(L, 2) != 0;
    const Cooldown cd = SlotCooldown(slot0);
    const bool active = cd.enabled && cd.startMs != 0 && cd.durationMs != 0;
    if (!active || (ignoreGCD && cd.hasGCD && cd.onGCD))
        Duration::Object::PushFromStart(L, 0, 0);
    else
        Duration::Object::PushFromStart(L, cd.startMs, cd.durationMs);
    return 1;
}

// SpellChargeInfo for an action with no charge category.
void PushNoCharges(void *L) {
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "currentCharges", 0);
    Game::Lua::SetFieldNumber(L, "maxCharges", 0);
    Game::Lua::SetFieldNumber(L, "cooldownStartTime", 0);
    Game::Lua::SetFieldNumber(L, "cooldownDuration", 0);
    Game::Lua::SetFieldNumber(L, "chargeModRate", 1.0);
    Game::Lua::SetFieldBool(L, "isActive", false);
}

int __fastcall Script_GetActionCharges(void *L) {
    int slot0 = 0;
    bool valid = false;
    if (!ActionArg(L, "Usage: local chargeInfo = C_ActionBar.GetActionCharges(actionID)", &slot0,
                   &valid) ||
        !valid)
        return 0;
    PushNoCharges(L);
    return 1;
}

// --- Usable / range ---------------------------------------------------------------

int __fastcall Script_IsUsableAction(void *L) {
    int slot0 = 0;
    bool valid = false;
    if (!ActionArg(L, "Usage: local isUsable, isLackingResources = C_ActionBar.IsUsableAction(actionID)",
                   &slot0, &valid) ||
        !valid)
        return 0;
    bool usable = false;
    bool noMana = false;
    if (EngineSlot(slot0)) {
        usable = reinterpret_cast<const uint32_t *>(
                     static_cast<uintptr_t>(Offsets::VAR_ACTION_USABLE))[slot0] != 0;
        noMana = reinterpret_cast<const uint32_t *>(
                     static_cast<uintptr_t>(Offsets::VAR_ACTION_NO_MANA))[slot0] != 0;
    } else {
        usable = ExtendedState::CachedUsable(slot0, &noMana);
    }
    Game::Lua::PushBool(L, usable);
    Game::Lua::PushBool(L, noMana);
    return 2;
}

int __fastcall Script_IsActionInRange(void *L) {
    int slot0 = 0;
    bool valid = false;
    if (!ActionArg(L, "Usage: local isInRange = C_ActionBar.IsActionInRange(actionID [, target])",
                   &slot0, &valid) ||
        !valid)
        return 0;
    uint64_t unit = 0;
    if (Game::Lua::IsString(L, 2))
        unit = Unit::Identity::GuidForToken(Game::Lua::ToString(L, 2));
    int inRange = 0;
    if (Action::Range::Answer(slot0, unit, &inRange) == 0)
        Game::Lua::PushNil(L);
    else
        Game::Lua::PushBool(L, inRange != 0);
    return 1;
}

int __fastcall Script_HasRangeRequirements(void *L) {
    int slot0 = 0;
    bool valid = false;
    if (!ActionArg(L,
                   "Usage: local hasRangeRequirements = C_ActionBar.HasRangeRequirements(actionID)",
                   &slot0, &valid) ||
        !valid)
        return 0;
    Game::Lua::PushBool(L, EngineSlot(slot0)
                               ? SlotTest(Offsets::FUN_ACTION_SLOT_HAS_RANGE, slot0)
                               : ExtendedState::HasRange(Extended::Get(slot0)));
    return 1;
}

// --- Text and counts ----------------------------------------------------------------

// Script_GetActionText's two branches (0x004E7050) plus EquipmentSet's set
// name, for one of our words: a set's name, a live macro's name, else none.
const char *WordText(uint32_t word) {
    if (const uint32_t setID = EquipmentSet::Action::WordSetID(word))
        return EquipmentSet::Action::Name(setID);
    if (word == 0 || (word & Offsets::ACTION_TYPE_MASK) != Offsets::ACTION_TYPE_BAG_OR_MACRO)
        return nullptr;
    const uint8_t *entry = reinterpret_cast<MacroIDToEntry_t>(Offsets::FUN_MACRO_ID_TO_ENTRY)(
        word & Offsets::ACTION_PAYLOAD_MASK_BAG_OR_MACRO);
    return entry != nullptr ? reinterpret_cast<const char *>(entry + Offsets::OFF_MACRO_NAME)
                            : nullptr;
}

int __fastcall Script_GetActionText(void *L) {
    int slot0 = 0;
    bool valid = false;
    if (!ActionArg(L, "Usage: local text = C_ActionBar.GetActionText(actionID)", &slot0, &valid) ||
        !valid)
        return 0;
    if (EngineSlot(slot0)) {
        const int top = Game::Lua::GetTop(L);
        Game::Lua::SetTop(L, 1);
        Game::Lua::CallScript(Offsets::FUN_SCRIPT_GET_ACTION_TEXT, L);
        if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_STRING) {
            Game::Lua::SetTop(L, top);
            return 0;
        }
        return 1;
    }
    const char *text = WordText(Extended::Get(slot0));
    if (text == nullptr)
        return 0;
    Game::Lua::PushString(L, text);
    return 1;
}

uint32_t UseCount(int slot0) {
    if (const uint32_t item = MacroItem(slot0))
        return reinterpret_cast<uint32_t(__fastcall *)(uint32_t)>(Offsets::FUN_ACTION_ITEM_COUNT)(item);
    if (EngineSlot(slot0))
        return ItemOf(Action::Slot::Word(slot0)) != 0
                   ? reinterpret_cast<const uint32_t *>(
                         static_cast<uintptr_t>(Offsets::VAR_ACTION_ITEM_COUNTS))[slot0]
                   : 0;
    return ExtendedState::CachedCount(slot0);
}

bool IsConsumable(int slot0) {
    if (const uint32_t item = MacroItem(slot0))
        return ExtendedState::IsConsumableItem(item);
    if (EngineSlot(slot0))
        return SlotTest(Offsets::FUN_ACTION_SLOT_IS_CONSUMABLE, slot0);
    const uint32_t item = ItemOf(Extended::Get(slot0));
    return item != 0 && ExtendedState::IsConsumableItem(item);
}

int __fastcall Script_GetActionUseCount(void *L) {
    int slot0 = 0;
    bool valid = false;
    if (!ActionArg(L, "Usage: local count = C_ActionBar.GetActionUseCount(actionID)", &slot0,
                   &valid) ||
        !valid)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(UseCount(slot0)));
    return 1;
}

int __fastcall Script_GetActionDisplayCount(void *L) {
    const char *usage = "Usage: local displayCount = C_ActionBar.GetActionDisplayCount(actionID [, "
                        "maxDisplayCount, replacementString])";
    int slot0 = 0;
    bool valid = false;
    if (!ActionArg(L, usage, &slot0, &valid))
        return 0;
    int maxDisplayCount = kDefaultMaxDisplayCount;
    if (Game::Lua::Type(L, 2) > Game::Lua::TYPE_NIL) {
        if (!Game::Lua::IsNumber(L, 2)) {
            Game::Lua::Error(L, usage);
            return 0;
        }
        maxDisplayCount = static_cast<int>(Game::Lua::ToNumber(L, 2));
    }
    const char *replacement = kDefaultReplacement;
    if (Game::Lua::Type(L, 3) > Game::Lua::TYPE_NIL) {
        if (!Game::Lua::IsString(L, 3)) {
            Game::Lua::Error(L, usage);
            return 0;
        }
        replacement = Game::Lua::ToString(L, 3);
    }
    if (!valid)
        return 0;
    const int count = static_cast<int>(UseCount(slot0));
    char text[16] = "";
    if (!IsConsumable(slot0) && !Action::SlotItem::IsStackable(slot0) &&
        (Action::SlotItem::ItemForSlot(slot0) != 0 || count < 1)) {
        // The charges branch: 1.12 has no charges, so maxCharges is never 2.
    } else if (maxDisplayCount < count) {
        Game::Lua::PushString(L, replacement);
        return 1;
    } else {
        std::snprintf(text, sizeof text, "%d", count);
    }
    Game::Lua::PushString(L, text);
    return 1;
}

// --- Bool tests ---------------------------------------------------------------------

using WordTest_t = bool (*)(uint32_t word);

int BoolRead(void *L, const char *usage, uintptr_t engineFn, WordTest_t ours) {
    int slot0 = 0;
    bool valid = false;
    if (!ActionArg(L, usage, &slot0, &valid) || !valid)
        return 0;
    Game::Lua::PushBool(L, EngineSlot(slot0) ? SlotTest(engineFn, slot0)
                                             : ours(Extended::Get(slot0)));
    return 1;
}

int __fastcall Script_IsCurrentAction(void *L) {
    return BoolRead(L, "Usage: local isCurrentAction = C_ActionBar.IsCurrentAction(actionID)",
                    Offsets::FUN_ACTION_SLOT_IS_CURRENT, &ExtendedState::IsCurrent);
}

int __fastcall Script_IsAutoRepeatAction(void *L) {
    return BoolRead(L, "Usage: local isAutoRepeatAction = C_ActionBar.IsAutoRepeatAction(actionID)",
                    Offsets::FUN_ACTION_SLOT_IS_AUTO_REPEAT, &ExtendedState::IsAutoRepeat);
}

int __fastcall Script_IsAttackAction(void *L) {
    return BoolRead(L, "Usage: local isAttackAction = C_ActionBar.IsAttackAction(actionID)",
                    Offsets::FUN_ACTION_SLOT_IS_ATTACK, &ExtendedState::IsAttack);
}

int __fastcall Script_IsEquippedAction(void *L) {
    return BoolRead(L, "Usage: local isEquippedAction = C_ActionBar.IsEquippedAction(actionID)",
                    Offsets::FUN_ACTION_SLOT_IS_EQUIPPED, &ExtendedState::IsEquipped);
}

int __fastcall Script_IsConsumableAction(void *L) {
    int slot0 = 0;
    bool valid = false;
    if (!ActionArg(L, "Usage: local isConsumableAction = C_ActionBar.IsConsumableAction(actionID)",
                   &slot0, &valid) ||
        !valid)
        return 0;
    Game::Lua::PushBool(L, IsConsumable(slot0));
    return 1;
}

int __fastcall Script_IsItemAction(void *L) {
    int slot0 = 0;
    bool valid = false;
    if (!ActionArg(L, "Usage: local isItemAction = C_ActionBar.IsItemAction(actionID)", &slot0,
                   &valid) ||
        !valid)
        return 0;
    Game::Lua::PushBool(L, ItemOf(Action::Slot::Word(slot0)) != 0);
    return 1;
}

enum Disposition { kNeutral, kHelpful, kHarmful };

// 3.80.2's helpful / harmful action test.
int Classified(void *L, const char *usage, bool harmful) {
    int slot0 = 0;
    bool valid = false;
    if (!ActionArg(L, usage, &slot0, &valid))
        return 0;
    const bool useNeutral = Game::Lua::ToBoolean(L, 2) != 0;
    bool answer = false;
    const uint32_t word = Action::Slot::Word(slot0);
    const bool spellOrItem =
        word != 0 && ((word & Offsets::ACTION_TYPE_MASK) == Offsets::ACTION_TYPE_SPELL ||
                      (word & Offsets::ACTION_TYPE_MASK) == Offsets::ACTION_TYPE_ITEM_BY_ID);
    const bool attack = harmful && (EngineSlot(slot0)
                                        ? SlotTest(Offsets::FUN_ACTION_SLOT_IS_ATTACK, slot0)
                                        : ExtendedState::IsAttack(word));
    if (valid && (spellOrItem || attack || (harmful && ExtendedState::IsRangedRepeat(word)))) {
        uint32_t book = 0;
        const int spellID = static_cast<int>(Action::Slot::SpellFor(slot0, &book));
        const Disposition d = ::Spell::Info::IsHarmful(spellID)   ? kHarmful
                              : ::Spell::Info::IsHelpful(spellID) ? kHelpful
                                                                  : kNeutral;
        answer = d == (harmful ? kHarmful : kHelpful) || (d == kNeutral && useNeutral);
    }
    Game::Lua::PushBool(L, answer);
    return 1;
}

int __fastcall Script_IsHelpfulAction(void *L) {
    return Classified(L, "Usage: local isHelpful = C_ActionBar.IsHelpfulAction(actionID, useNeutral)",
                      false);
}

int __fastcall Script_IsHarmfulAction(void *L) {
    return Classified(L, "Usage: local isHarmful = C_ActionBar.IsHarmfulAction(actionID, useNeutral)",
                      true);
}

// --- Documentation ----------------------------------------------------------------

const Game::Doc::Field kActionArgs[] = {
    Game::Doc::Req("actionID", "luaIndex", "The action slot, 1 to 360."),
};

const Game::Doc::Field kCooldownRets[] = {
    Game::Doc::Opt("cooldownInfo", "SpellCooldownInfo", nullptr,
                   "Nothing for a slot outside 1 to 360."),
};
const Game::Doc::Function kGetActionCooldown{
    "Returns the cooldown of an action: its spell's, or its item's.", kActionArgs, kCooldownRets};

const Game::Doc::Field kCooldownDurationArgs[] = {
    Game::Doc::Req("actionID", "luaIndex", "The action slot, 1 to 360."),
    Game::Doc::Opt("ignoreGCD", "bool", "false",
                   "When true, a cooldown that is only the global cooldown reads as zero."),
};
const Game::Doc::Field kDurationRets[] = {
    Game::Doc::Opt("duration", "LuaDurationObject", nullptr,
                   "Zero when no cooldown runs; nothing for a slot outside 1 to 360."),
};
const Game::Doc::Function kGetActionCooldownDuration{
    "Returns the cooldown of an action as a duration object.", kCooldownDurationArgs,
    kDurationRets};

const Game::Doc::Field kChargeInfoFields[] = {
    Game::Doc::Req("currentCharges", "number", "Always 0."),
    Game::Doc::Req("maxCharges", "number", "Always 0."),
    Game::Doc::Req("cooldownStartTime", "number", "Always 0."),
    Game::Doc::Req("cooldownDuration", "number", "Always 0."),
    Game::Doc::Req("chargeModRate", "number", "Always 1."),
    Game::Doc::Req("isActive", "bool", "Always false."),
};
const Game::Doc::Structure kSpellChargeInfo{"SpellChargeInfo", "Spell", kChargeInfoFields,
                                            "The charges of a spell or action. No spell has "
                                            "charges here."};
const Game::Doc::Field kChargesRets[] = {
    Game::Doc::Opt("chargeInfo", "SpellChargeInfo", nullptr, "Nothing for a slot outside 1 to 360."),
};
const Game::Doc::Function kGetActionCharges{"Returns the charges of an action.", kActionArgs,
                                            kChargesRets};

const Game::Doc::Field kUsableRets[] = {
    Game::Doc::Opt("isUsable", "bool", nullptr, "Nothing for a slot outside 1 to 360."),
    Game::Doc::Opt("isLackingResources", "bool", nullptr,
                   "True when the action fails only for lack of mana, rage, energy or health."),
};
const Game::Doc::Function kIsUsableAction{"Returns whether an action can be used now.",
                                          kActionArgs, kUsableRets};

const Game::Doc::Field kRangeArgs[] = {
    Game::Doc::Req("actionID", "luaIndex", "The action slot, 1 to 360."),
    Game::Doc::Opt("target", "UnitToken", nullptr,
                   "The unit to check against; by default the macro's unit or the target."),
};
const Game::Doc::Field kRangeRets[] = {
    Game::Doc::Opt("isInRange", "bool", nullptr,
                   "Nil when range cannot be determined, for example with no target."),
};
const Game::Doc::Function kIsActionInRange{"Returns whether an action's target is in range.",
                                           kRangeArgs, kRangeRets};

const Game::Doc::Field kHasRangeRets[] = {
    Game::Doc::Opt("hasRangeRequirements", "bool", nullptr, "Nothing for a slot outside 1 to 360."),
};
const Game::Doc::Function kHasRangeRequirements{
    "Returns whether an action's spell has a minimum or maximum range.", kActionArgs,
    kHasRangeRets};

const Game::Doc::Field kTextRets[] = {
    Game::Doc::Opt("text", "string", nullptr,
                   "A macro's or an equipment set's name; nothing for any other action."),
};
const Game::Doc::Function kGetActionText{"Returns the text an action button shows.", kActionArgs,
                                         kTextRets};

const Game::Doc::Field kUseCountRets[] = {
    Game::Doc::Opt("count", "number", nullptr,
                   "How many of the action's item you carry; 0 for an action with no item."),
};
const Game::Doc::Function kGetActionUseCount{"Returns how many of an action's item you carry.",
                                             kActionArgs, kUseCountRets};

const Game::Doc::Field kDisplayCountArgs[] = {
    Game::Doc::Req("actionID", "luaIndex", "The action slot, 1 to 360."),
    Game::Doc::Opt("maxDisplayCount", "number", "9999"),
    Game::Doc::Opt("replacementString", "cstring", "\"*\"",
                   "Shown in place of a count above maxDisplayCount."),
};
const Game::Doc::Field kDisplayCountRets[] = {
    Game::Doc::Opt("displayCount", "string", nullptr,
                   "The count of a consumable or stacking item; an empty string otherwise."),
};
const Game::Doc::Function kGetActionDisplayCount{"Returns the count text an action button shows.",
                                                 kDisplayCountArgs, kDisplayCountRets};

const Game::Doc::Field kCurrentRets[] = {
    Game::Doc::Req("isCurrentAction", "bool",
                   "True while its spell is cast or awaits a target, its form or trade skill "
                   "window is up, its item is used, or attack is on."),
};
const Game::Doc::Function kIsCurrentAction{"Returns whether an action is in progress.",
                                           kActionArgs, kCurrentRets};

const Game::Doc::Field kAutoRepeatRets[] = {
    Game::Doc::Req("isAutoRepeatAction", "bool"),
};
const Game::Doc::Function kIsAutoRepeatAction{
    "Returns whether an action's spell is the one repeating now, such as Auto Shot.", kActionArgs,
    kAutoRepeatRets};

const Game::Doc::Field kAttackRets[] = {
    Game::Doc::Req("isAttackAction", "bool"),
};
const Game::Doc::Function kIsAttackAction{"Returns whether an action is the melee Attack.",
                                          kActionArgs, kAttackRets};

const Game::Doc::Field kEquippedRets[] = {
    Game::Doc::Req("isEquippedAction", "bool"),
};
const Game::Doc::Function kIsEquippedAction{"Returns whether an action's item is equipped.",
                                            kActionArgs, kEquippedRets};

const Game::Doc::Field kConsumableRets[] = {
    Game::Doc::Req("isConsumableAction", "bool"),
};
const Game::Doc::Function kIsConsumableAction{
    "Returns whether an action uses up its item: ammunition, thrown weapons, or an item whose "
    "charges run out.",
    kActionArgs, kConsumableRets};

const Game::Doc::Field kItemRets[] = {
    Game::Doc::Req("isItemAction", "bool"),
};
const Game::Doc::Function kIsItemAction{"Returns whether an action is an item.", kActionArgs,
                                        kItemRets};

const Game::Doc::Field kDispositionArgs[] = {
    Game::Doc::Req("actionID", "luaIndex", "The action slot, 1 to 360."),
    Game::Doc::Req("useNeutral", "bool",
                   "When true, a spell that is neither helpful nor harmful also counts."),
};
const Game::Doc::Field kHelpfulRets[] = {
    Game::Doc::Req("isHelpful", "bool"),
};
const Game::Doc::Function kIsHelpfulAction{
    "Returns whether a spell or item action's spell is aimed at a friend.", kDispositionArgs,
    kHelpfulRets};
const Game::Doc::Field kHarmfulRets[] = {
    Game::Doc::Req("isHarmful", "bool"),
};
const Game::Doc::Function kIsHarmfulAction{
    "Returns whether a spell, item or attack action's spell is aimed at an enemy.",
    kDispositionArgs, kHarmfulRets};

void RegisterLuaFunctions() {
    const struct {
        const char *name;
        Game::Lua::CFunction fn;
        const Game::Doc::Function *doc;
    } kReads[] = {
        {"GetActionCooldown", &Script_GetActionCooldown, &kGetActionCooldown},
        {"GetActionCooldownDuration", &Script_GetActionCooldownDuration,
         &kGetActionCooldownDuration},
        {"GetActionCharges", &Script_GetActionCharges, &kGetActionCharges},
        {"IsUsableAction", &Script_IsUsableAction, &kIsUsableAction},
        {"IsActionInRange", &Script_IsActionInRange, &kIsActionInRange},
        {"HasRangeRequirements", &Script_HasRangeRequirements, &kHasRangeRequirements},
        {"GetActionText", &Script_GetActionText, &kGetActionText},
        {"GetActionUseCount", &Script_GetActionUseCount, &kGetActionUseCount},
        {"GetActionDisplayCount", &Script_GetActionDisplayCount, &kGetActionDisplayCount},
        {"IsCurrentAction", &Script_IsCurrentAction, &kIsCurrentAction},
        {"IsAutoRepeatAction", &Script_IsAutoRepeatAction, &kIsAutoRepeatAction},
        {"IsAttackAction", &Script_IsAttackAction, &kIsAttackAction},
        {"IsEquippedAction", &Script_IsEquippedAction, &kIsEquippedAction},
        {"IsConsumableAction", &Script_IsConsumableAction, &kIsConsumableAction},
        {"IsItemAction", &Script_IsItemAction, &kIsItemAction},
        {"IsHelpfulAction", &Script_IsHelpfulAction, &kIsHelpfulAction},
        {"IsHarmfulAction", &Script_IsHarmfulAction, &kIsHarmfulAction},
    };
    for (const auto &r : kReads)
        Game::Lua::RegisterTableFunction("C_ActionBar", r.name, r.fn, r.doc);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Action::BarReads
