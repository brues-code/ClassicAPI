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
#include "spell/Cooldown.h"
#include "spell/Lookup.h"
#include "spell/Tooltip.h"
#include "time/Clock.h"
#include "tooltip/Lines.h"

#include <cstdint>

namespace Spell::Tooltip {

namespace {

// CGTooltip::BuildSpell — see FUN_GAMETOOLTIP_BUILD_SPELL_TOOLTIP.
using BuildSpell_t = uint32_t(__thiscall *)(void *tooltip, int spellID, int compact,
                                            uint32_t cooldownMs, int pet, int showRank,
                                            const void *talent, int rebuild);

uint32_t BuildSpell(void *tooltip, int spellID, uint32_t cooldownMs, bool pet, bool showRank,
                    bool rebuild) {
    return reinterpret_cast<BuildSpell_t>(Offsets::FUN_GAMETOOLTIP_BUILD_SPELL_TOOLTIP)(
        tooltip, spellID, 0, cooldownMs, pet ? 1 : 0, showRank ? 1 : 0, nullptr, rebuild ? 1 : 0);
}

// The cooldown argument 4.3.4's Script_SetSpellByID computes: the time left
// when the spell's cooldown is enabled, running and started, else 0.
uint32_t CooldownRemainingMs(int spellID, bool pet) {
    uint32_t start = 0, duration = 0;
    bool enabled = false;
    if (!Spell::Cooldown::Query(spellID, pet, &start, &duration, &enabled) || !enabled ||
        duration == 0 || start == 0)
        return 0;
    return Time::Clock::Remaining(start + duration);
}

// The builder's append mode heads the block with its talent "Next rank:"
// line, where 4.3.4's append (no talent) adds the ordinary name line. Rewrite
// that line in place with the engine's own add-line — the same
// `AddDoubleLine(name, subtext, text, gray)` the builder's non-append path
// makes — by pointing the line cursor back at it for the one call.
void RewriteAppendHeader(void *tooltip, int headerLine, int spellID, bool showSubtext) {
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr ||
        (Game::Read<uint32_t>(record, Offsets::OFF_SPELL_RECORD_ATTRIBUTES) &
         Offsets::SPELL_ATTR_TRADESPELL) != 0)
        return; // a recipe appends its created item's tooltip, which has no header
    const int locale = Game::Read<int>(Offsets::VAR_LOCALE_INDEX);
    const char *name = Game::Read<const char *>(record, Offsets::OFF_SPELL_NAMES + locale * 4);
    if (name == nullptr || name[0] == '\0')
        return;
    const char *subtext =
        showSubtext ? Game::Read<const char *>(record, Offsets::OFF_SPELL_RECORD_RANK + locale * 4)
                    : nullptr;

    const uint32_t textColor = Game::Read<uint32_t>(Offsets::VAR_TOOLTIP_TEXT_COLOR);
    const uint32_t grayColor = Game::Read<uint32_t>(Offsets::VAR_TOOLTIP_GRAY_COLOR);
    int *numLines = Game::Ptr<int>(tooltip, Offsets::OFF_GAMETOOLTIP_NUM_LINES);
    const int after = *numLines;
    *numLines = headerLine;
    ::Tooltip::Lines::AddDouble(tooltip, name, subtext, textColor, grayColor);
    *numLines = after;
}


// 4.3.4's Script_SetSpellByID / Script_AddSpellByID (Tooltip_Scripts.cpp
// 0x004733C0 / 0x00473530), which differ only in `append`: (spellID
// [, isPet [, showSubtext]]), the spell's cooldown passed to the builder,
// 1 when it added the cooldown line (FrameXML's refresh cue), else nil.
// Both raise the same message, SetSpellByID's.
int SetOrAddSpellByID(void *L, bool append) {
    void *tooltip = Game::Lua::ResolveTooltip(L);
    if (tooltip == nullptr)
        return 0;
    int spellID = 0;
    if (!Game::Lua::IsNumber(L, 2) ||
        (spellID = static_cast<int>(Game::Lua::ToNumber(L, 2))) < 0) {
        Game::Lua::Error(L, "Invalid spell ID in %s:SetSpellByID",
                         Game::Lua::ObjectDisplayName(tooltip));
        return 0;
    }
    const bool isPet = Game::Lua::ToFlag(L, 3);
    const bool showSubtext = Game::Lua::ToFlag(L, 4);

    const int before = Game::Read<int>(tooltip, Offsets::OFF_GAMETOOLTIP_NUM_LINES);
    const uint32_t result = BuildSpell(tooltip, spellID, CooldownRemainingMs(spellID, isPet),
                                       isPet, showSubtext, append);
    if (append && Game::Read<int>(tooltip, Offsets::OFF_GAMETOOLTIP_NUM_LINES) > before)
        RewriteAppendHeader(tooltip, before, spellID, showSubtext);

    if (result != 0)
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

int __fastcall Script_GameTooltipSetSpellByID(void *L) { return SetOrAddSpellByID(L, false); }

int __fastcall Script_GameTooltipAddSpellByID(void *L) { return SetOrAddSpellByID(L, true); }

} // namespace

void ShowByID(void *L, int spellID) {
    if (spellID <= 0)
        return;
    void *tooltipObj = Game::Lua::ResolveTooltip(L);
    if (tooltipObj == nullptr)
        return;
    BuildSpell(tooltipObj, spellID, 0, false, /*showRank=*/true, false);
}

// `GameTooltip:GetSpell()` → (name, rank, spellID) for whichever spell
// the tooltip is currently displaying, or nothing if it isn't showing
// a spell. BuildSpellTooltip writes the spellID to `tooltip+0x39C`;
// the per-tooltip Clear at FUN_00530050 zeroes the same slot on
// Hide/before-redraw, so a non-zero read means the spell tooltip is
// live. Name + rank come from the Spell.dbc record's locale-string
// arrays at +0x1E0 and +0x204 (locale index at VAR_LOCALE_INDEX).
//
// Aura-tooltip coverage (SetUnitBuff / SetUnitDebuff / SetPlayerBuff)
// was attempted via a MinHook on the inner aura builder at
// FUN_0052F880; that consistently crashed in pfUI-style
// hooksecurefunc paths, with the trampoline executing from corrupted
// JIT memory. Reverted pending a different design. For now,
// aura-path tooltips return nothing here.
static int __fastcall Script_GameTooltipGetSpell(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: GameTooltip:GetSpell()");
        return 0;
    }
    void *tooltipObj = Game::Lua::ResolveTooltip(L);
    if (tooltipObj == nullptr)
        return 0;

    const int spellID = Game::Read<int>(
        tooltipObj, Offsets::OFF_TOOLTIP_SPELL_ID);
    if (spellID <= 0)
        return 0;

    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return 0;

    const int locale = Game::Read<int>(Offsets::VAR_LOCALE_INDEX);
    const char *name = Game::Read<const char *>(
        record, Offsets::OFF_SPELL_NAMES + locale * 4);
    const char *rank = Game::Read<const char *>(
        record, Offsets::OFF_SPELL_RECORD_RANK + locale * 4);

    if (name == nullptr)
        return 0;
    Game::Lua::PushString(L, name);
    // Spell rank is optional in Spell.dbc — some spells (most racials,
    // some procs) have an empty rank slot. Surface that as an empty
    // string to match modern semantics; an absent rank is not nil.
    Game::Lua::PushString(L, rank != nullptr ? rank : "");
    Game::Lua::PushNumber(L, static_cast<double>(spellID));
    return 3;
}

// `GameTooltip:HasSpell()` — boolean companion to `GetSpell`. Returns
// true iff the tooltip is currently showing a cast spell tooltip.
// Aura tooltips (SetUnitBuff/SetUnitDebuff/SetPlayerBuff) leave
// `OFF_TOOLTIP_SPELL_ID` zero — see the note on `Script_GameTooltipGetSpell`.
static int __fastcall Script_GameTooltipHasSpell(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: GameTooltip:HasSpell()");
        return 0;
    }
    void *tooltipObj = Game::Lua::ResolveTooltip(L);
    if (tooltipObj == nullptr) {
        Game::Lua::PushBool(L, 0);
        return 1;
    }
    const int spellID = Game::Read<int>(
        tooltipObj, Offsets::OFF_TOOLTIP_SPELL_ID);
    Game::Lua::PushBool(L, spellID > 0);
    return 1;
}

static const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetSpellByID", &Script_GameTooltipSetSpellByID},
    {"AddSpellByID", &Script_GameTooltipAddSpellByID},
    {"GetSpell", &Script_GameTooltipGetSpell},
    {"HasSpell", &Script_GameTooltipHasSpell},
};

// --- Documentation ----------------------------------------------------------

static const Game::Doc::Field kSpellByIDArgs[] = {
    Game::Doc::Req("spellID", "number", "Any spell ID, learned or not."),
    Game::Doc::Opt("isPet", "boolean", "false", "Read cost, range and cooldown as the pet's spell."),
    Game::Doc::Opt("showSubtext", "boolean", "false", "Show the spell's rank to the right of its name."),
};
static const Game::Doc::Field kSpellByIDRets[] = {
    Game::Doc::Opt("hasCooldown", "number", nullptr,
                   "1 when the tooltip shows a cooldown that is counting down, else nil."),
};
static const Game::Doc::Function kSetSpellByID{
    "Fills the tooltip with a spell's tooltip.", kSpellByIDArgs, kSpellByIDRets};
static const Game::Doc::Function kAddSpellByID{
    "Appends a spell's tooltip to what the tooltip already shows.", kSpellByIDArgs,
    kSpellByIDRets};

static const Game::Doc::Field kGetSpellRets[] = {
    Game::Doc::Opt("name", "string", nullptr, "Nil when the tooltip shows no spell."),
    Game::Doc::Opt("rank", "string", nullptr, "Empty for a spell that has no rank."),
    Game::Doc::Opt("spellID", "number"),
};
static const Game::Doc::Function kGetSpell{
    "The spell the tooltip is showing. Aura tooltips report nothing.", {}, kGetSpellRets};

static const Game::Doc::Field kHasSpellRets[] = {
    Game::Doc::Req("hasSpell", "bool", "True while the tooltip shows a spell."),
};
static const Game::Doc::Function kHasSpell{
    "Whether the tooltip is showing a spell. Aura tooltips report false.",
    {}, kHasSpellRets};

static const Game::Doc::Method g_methodDocs[] = {
    {"SetSpellByID", &kSetSpellByID},
    {"AddSpellByID", &kAddSpellByID},
    {"GetSpell", &kGetSpell},
    {"HasSpell", &kHasSpell},
};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_GAMETOOLTIP_METHOD_REGISTRY),
        g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])),
        g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::Tooltip
