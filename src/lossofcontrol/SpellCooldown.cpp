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

// The per-spell view of `C_LossOfControl`: the control-loss effect that stops
// THIS spell, shaped as a cooldown (`GetTime()`-epoch seconds) so an action
// button can draw the loss-of-control swipe over it.
// `LossOfControl::LockoutForSpell` decides which effects block which spells
// (3.80.2's FUN_14197f180); this file only converts ticks and builds the Lua
// shapes.
//
// As 3.80.2 has them:
//   C_Spell.GetSpellLossOfControlCooldownInfo(spellIdentifier)
//       -> SpellLossOfControlInfo, nil for an unknown spell (0x140dc8940)
//   C_Spell.GetSpellLossOfControlCooldownDuration(spellIdentifier)
//       -> duration object, zero when nothing blocks (0x140dc8730)
//   C_ActionBar.GetActionLossOfControlCooldownInfo(actionID) /
//   C_ActionBar.GetActionLossOfControlCooldownDuration(actionID)
//       -> the same for an action slot's spell; nothing outside the slots
//   GetSpellLossOfControlCooldown(spellID | slot, bookType | name[, rank])
//       -> start, duration; 0, 0 when nothing blocks; nothing when the
//          arguments name no spell (0x1417547b0)
// SpellLossOfControlInfo: isActive needs a start and a duration, so an effect
// with no timer (start now, duration 0) is not active;
// shouldReplaceNormalCooldown when the effect ends after the spell's own
// cooldown; modRate 1 (1.12 has no cooldown rate).
//
// Kept beside them (retail): C_Spell.GetSpellLossOfControlCooldown and the
// C_SpellBook getters.
//
// An effect whose end is known but whose start is not is reported as
// starting now and lasting the remaining time.

#include "Lockout.h"

#include "Game.h"
#include "Offsets.h"
#include "duration/Object.h"
#include "spell/Arg.h"
#include "spell/Cooldown.h"
#include "spell/Lookup.h"
#include "time/Clock.h"

#include <cstdint>
#include <cstdio>

namespace LossOfControl::SpellCooldown {

namespace {

using Time::Clock::NowMs;

struct Lockout {
    bool found;
    uint32_t startMs;
    uint32_t endMs;
};

Lockout ForSpell(int spellID) {
    Lockout lock{false, 0, 0};
    if (spellID <= 0 || !LockoutForSpell(spellID, &lock.startMs, &lock.endMs))
        return lock;
    lock.found = true;
    if (lock.startMs == 0)
        lock.startMs = NowMs(); // end known, start not: swipe from now
    return lock;
}

double StartSeconds(const Lockout &lock) {
    return lock.found ? static_cast<double>(lock.startMs) * 0.001 : 0.0;
}

uint32_t DurationMs(const Lockout &lock) {
    return lock.found ? Time::Clock::Elapsed(lock.startMs, lock.endMs) : 0;
}

double DurationSeconds(const Lockout &lock) {
    return static_cast<double>(DurationMs(lock)) * 0.001;
}

bool IsActive(const Lockout &lock) {
    return lock.found && lock.startMs != 0 && DurationMs(lock) != 0;
}

// True when the lockout outlasts the spell's own cooldown — the case where
// a button should draw the loss-of-control swipe instead of the normal one.
bool OutlastsNormalCooldown(int spellID, const Lockout &lock) {
    if (!lock.found)
        return false;
    uint32_t cdStart = 0, cdDuration = 0;
    bool enabled = false;
    if (!Spell::Cooldown::Query(spellID, &cdStart, &cdDuration, &enabled) || cdDuration == 0)
        return true;
    const uint32_t now = NowMs();
    return Time::Clock::Remaining(now, lock.endMs) >
           Time::Clock::Remaining(now, cdStart + cdDuration);
}

void PushInfo(void *L, int spellID) {
    const Lockout lock = ForSpell(spellID);
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "startTime", StartSeconds(lock));
    Game::Lua::SetFieldNumber(L, "duration", DurationSeconds(lock));
    Game::Lua::SetFieldNumber(L, "modRate", 1.0);
    Game::Lua::SetFieldBool(L, "isActive", IsActive(lock));
    Game::Lua::SetFieldBool(L, "shouldReplaceNormalCooldown",
                            OutlastsNormalCooldown(spellID, lock));
}

// A duration object for the block; zero when it is not active.
void PushDuration(void *L, int spellID) {
    const Lockout lock = ForSpell(spellID);
    if (IsActive(lock))
        Duration::Object::PushFromStart(L, lock.startMs, DurationMs(lock));
    else
        Duration::Object::PushFromStart(L, 0, 0);
}

// The spell of an action slot (FUN_ACTION_SLOT_TO_SPELL, every action
// reader's resolver), 0 for a pet spell, as 3.80.2 answers 0 for one; false
// for a slot outside 1..120.
using SlotToSpell_t = uint32_t(__fastcall *)(uint32_t slot0, uint32_t *outPetFlag);

bool ActionSpell(void *L, const char *usage, int &spellID) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::ArgError(L, 1, usage);
        return false;
    }
    const int slot0 = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    if (slot0 < 0 || slot0 >= Offsets::ACTION_TABLE_MAX_SLOTS)
        return false;
    uint32_t pet = 0;
    const uint32_t id = reinterpret_cast<SlotToSpell_t>(
        static_cast<uintptr_t>(Offsets::FUN_ACTION_SLOT_TO_SPELL))(static_cast<uint32_t>(slot0),
                                                                   &pet);
    spellID = pet != 0 ? 0 : static_cast<int>(id);
    return true;
}

int __fastcall Script_C_Spell_GetSpellLossOfControlCooldown(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    if (Spell::Lookup::RecordForID(spellID) == nullptr)
        return 0;
    const Lockout lock = ForSpell(spellID);
    Game::Lua::PushNumber(L, StartSeconds(lock));
    Game::Lua::PushNumber(L, DurationSeconds(lock));
    return 2;
}

int __fastcall Script_C_Spell_GetSpellLossOfControlCooldownInfo(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    if (Spell::Lookup::RecordForID(spellID) == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    PushInfo(L, spellID);
    return 1;
}

int __fastcall Script_C_Spell_GetSpellLossOfControlCooldownDuration(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    if (Spell::Lookup::RecordForID(spellID) == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    PushDuration(L, spellID);
    return 1;
}

int __fastcall Script_GetActionLossOfControlCooldownInfo(void *L) {
    int spellID = 0;
    if (!ActionSpell(L,
                     "Usage: local lossOfControlInfo = "
                     "C_ActionBar.GetActionLossOfControlCooldownInfo(actionID)",
                     spellID))
        return 0;
    PushInfo(L, spellID);
    return 1;
}

int __fastcall Script_GetActionLossOfControlCooldownDuration(void *L) {
    int spellID = 0;
    if (!ActionSpell(
            L, "Usage: local duration = C_ActionBar.GetActionLossOfControlCooldownDuration(actionID)",
            spellID))
        return 0;
    PushDuration(L, spellID);
    return 1;
}

// 3.80.2's argument parser (FUN_141752090): a lone number is a spellID; a
// number then a bookType is a spellbook slot (a slot out of range is spell 0);
// a name, with an optional rank as the second string, is looked up in the
// spellbook. A pet spell answers 0, 0. Anything else names no spell.
bool LegacySpellArg(void *L, int &spellID) {
    if (Game::Lua::IsNumber(L, 1)) {
        const int arg1 = static_cast<int>(Game::Lua::ToNumber(L, 1));
        if (Game::Lua::GetTop(L) == 1) {
            spellID = arg1;
            return true;
        }
        const int bookType = Spell::Lookup::BookTypeArg(L, 2);
        spellID = bookType != 0 ? 0 : Spell::Lookup::SpellbookSlotToID(arg1, 0);
        return true;
    }
    if (!Game::Lua::IsString(L, 1))
        return false;
    char name[256];
    if (Game::Lua::IsString(L, 2))
        std::snprintf(name, sizeof(name), "%s(%s)", Game::Lua::ToString(L, 1),
                      Game::Lua::ToString(L, 2));
    else
        std::snprintf(name, sizeof(name), "%s", Game::Lua::ToString(L, 1));
    int pet = 0;
    const int id = Spell::Arg::NameToSpellID(name, &pet);
    if (id <= 0)
        return false;
    spellID = pet != 0 ? 0 : id;
    return true;
}

int __fastcall Script_GetSpellLossOfControlCooldown(void *L) {
    int spellID = 0;
    if (!LegacySpellArg(L, spellID))
        return 0;
    const Lockout lock = ForSpell(spellID);
    Game::Lua::PushNumber(L, StartSeconds(lock));
    Game::Lua::PushNumber(L, DurationSeconds(lock));
    return 2;
}

int __fastcall Script_GetSpellBookItemLossOfControlCooldownInfo(void *L) {
    const int spellID = Spell::Lookup::SpellbookItemArgsToID(L, 1, 2);
    if (spellID <= 0)
        return 0; // empty / out-of-range slot -> nil
    PushInfo(L, spellID);
    return 1;
}

int __fastcall Script_GetSpellBookItemLossOfControlCooldownDuration(void *L) {
    const int spellID = Spell::Lookup::SpellbookItemArgsToID(L, 1, 2);
    if (spellID <= 0)
        return 0;
    Game::Lua::PushNumber(L, DurationSeconds(ForSpell(spellID)));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kSpellArgs[] = {
    Game::Doc::Req("spellIdentifier", "SpellIdentifier"),
};
const Game::Doc::Field kSpellRets[] = {
    Game::Doc::Opt("startTime", "number", nullptr,
                   "In GetTime() seconds; 0 when nothing blocks the spell; nil, with the "
                   "duration, for an unknown spell."),
    Game::Doc::Opt("duration", "number", nullptr,
                   "In seconds; 0 when nothing blocks the spell."),
};
const Game::Doc::Function kGetSpellLossOfControlCooldown{
    "Returns the loss-of-control effect that stops you from casting a spell, as a cooldown; "
    "the one that ends last when several do.",
    kSpellArgs, kSpellRets};

const Game::Doc::Field kInfoFields[] = {
    Game::Doc::Req("startTime", "number",
                   "In GetTime() seconds; 0 when nothing blocks the spell."),
    Game::Doc::Req("duration", "number", "In seconds; 0 when nothing blocks the spell."),
    Game::Doc::Req("modRate", "number", "Always 1."),
    Game::Doc::Req("isActive", "bool", "True while an effect with a timer blocks the spell."),
    Game::Doc::Req("shouldReplaceNormalCooldown", "bool",
                   "True when the effect outlasts the spell's own cooldown."),
};
const Game::Doc::Structure kSpellLossOfControlInfo{
    "SpellLossOfControlInfo", "SpellBook", kInfoFields,
    "The loss-of-control effect that blocks a spell, shaped as a cooldown."};

const Game::Doc::Field kSpellInfoRets[] = {
    Game::Doc::Opt("lossOfControlInfo", "SpellLossOfControlInfo", nullptr,
                   "Nil for an unknown spell."),
};
const Game::Doc::Function kGetSpellLossOfControlCooldownInfo{
    "Returns the loss-of-control effect that blocks a spell, as a cooldown.", kSpellArgs,
    kSpellInfoRets};

const Game::Doc::Field kSpellDurationRets[] = {
    Game::Doc::Opt("duration", "LuaDurationObject", nullptr,
                   "Zero when no effect with a timer blocks the spell; nil for an unknown "
                   "spell."),
};
const Game::Doc::Function kGetSpellLossOfControlCooldownDuration{
    "Returns the loss-of-control effect that blocks a spell as a duration object.", kSpellArgs,
    kSpellDurationRets};

const Game::Doc::Field kActionArgs[] = {
    Game::Doc::Req("actionID", "luaIndex", "The action slot, 1 to 120."),
};
const Game::Doc::Field kActionInfoRets[] = {
    Game::Doc::Opt("lossOfControlInfo", "SpellLossOfControlInfo", nullptr,
                   "Nothing for a slot outside 1 to 120."),
};
const Game::Doc::Function kGetActionLossOfControlCooldownInfo{
    "Returns the loss-of-control effect that blocks an action's spell, as a cooldown.",
    kActionArgs, kActionInfoRets};
const Game::Doc::Field kActionDurationRets[] = {
    Game::Doc::Opt("duration", "LuaDurationObject", nullptr,
                   "Nothing for a slot outside 1 to 120."),
};
const Game::Doc::Function kGetActionLossOfControlCooldownDuration{
    "Returns the loss-of-control effect that blocks an action's spell as a duration object.",
    kActionArgs, kActionDurationRets};

const Game::Doc::Field kLegacyArgs[] = {
    Game::Doc::Req("spell", "SpellIdentifier",
                   "A spell ID, a spellbook slot then its bookType, or a spell name then an "
                   "optional rank."),
};
const Game::Doc::Field kLegacyRets[] = {
    Game::Doc::Opt("start", "number", nullptr,
                   "In GetTime() seconds; 0 when nothing blocks the spell; nothing when the "
                   "arguments name no spell."),
    Game::Doc::Opt("duration", "number", nullptr, "In seconds; 0 when nothing blocks the spell."),
};
const Game::Doc::Function kGetSpellLossOfControlCooldownLegacy{
    "Returns the loss-of-control effect that blocks a spell, as a cooldown.", kLegacyArgs,
    kLegacyRets, "SpellGlobals"};

const Game::Doc::Field kSlotArgs[] = {
    Game::Doc::Req("spellBookItemSlotIndex", "luaIndex"),
    Game::Doc::Opt("spellBookItemSpellBank", "SpellBookSpellBank", "0", "Player by default."),
};
const Game::Doc::Field kInfoRets[] = {
    Game::Doc::Opt("lossOfControlInfo", "SpellLossOfControlInfo", nullptr,
                   "Nil for an empty slot."),
};
const Game::Doc::Function kGetSpellBookItemLossOfControlCooldownInfo{
    "Returns the loss-of-control effect that blocks the spell in a spellbook slot, as a "
    "cooldown.",
    kSlotArgs, kInfoRets};

const Game::Doc::Field kDurationRets[] = {
    Game::Doc::Opt("duration", "number", nullptr,
                   "In seconds; 0 when nothing blocks the spell, nil for an empty slot."),
};
const Game::Doc::Function kGetSpellBookItemLossOfControlCooldownDuration{
    "Returns how long the loss-of-control effect that blocks the spell in a spellbook slot "
    "lasts.",
    kSlotArgs, kDurationRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellLossOfControlCooldown",
                                     &Script_C_Spell_GetSpellLossOfControlCooldown,
                                     &kGetSpellLossOfControlCooldown);
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellLossOfControlCooldownInfo",
                                     &Script_C_Spell_GetSpellLossOfControlCooldownInfo,
                                     &kGetSpellLossOfControlCooldownInfo);
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellLossOfControlCooldownDuration",
                                     &Script_C_Spell_GetSpellLossOfControlCooldownDuration,
                                     &kGetSpellLossOfControlCooldownDuration);
    Game::Lua::RegisterTableFunction("C_ActionBar", "GetActionLossOfControlCooldownInfo",
                                     &Script_GetActionLossOfControlCooldownInfo,
                                     &kGetActionLossOfControlCooldownInfo);
    Game::Lua::RegisterTableFunction("C_ActionBar", "GetActionLossOfControlCooldownDuration",
                                     &Script_GetActionLossOfControlCooldownDuration,
                                     &kGetActionLossOfControlCooldownDuration);
    Game::Lua::RegisterGlobalFunction("GetSpellLossOfControlCooldown",
                                      &Script_GetSpellLossOfControlCooldown,
                                      &kGetSpellLossOfControlCooldownLegacy);
    Game::Lua::RegisterTableFunction("C_SpellBook", "GetSpellBookItemLossOfControlCooldownInfo",
                                     &Script_GetSpellBookItemLossOfControlCooldownInfo,
                                     &kGetSpellBookItemLossOfControlCooldownInfo);
    Game::Lua::RegisterTableFunction(
        "C_SpellBook", "GetSpellBookItemLossOfControlCooldownDuration",
        &Script_GetSpellBookItemLossOfControlCooldownDuration,
        &kGetSpellBookItemLossOfControlCooldownDuration);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace LossOfControl::SpellCooldown
