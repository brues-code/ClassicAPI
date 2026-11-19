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

// GameTooltip:SetTalent(tabIndex, talentIndex[, isInspect[, isPet[,
// groupIndex[, isPreview]]]]): with isPreview, the talent tooltip shows the
// talent preview plan (Talent::Preview) in place of the learned ranks.
//
// The engine still builds the whole tooltip. SetTalent and its two builders
// read the learned state at a few sites (Offsets.h, at
// FUN_GAMETOOLTIP_BUILD_PASSIVE_TALENT): each rank scan asks KnowsSpell or
// KnowsHigherRank of a rank spell, the prerequisite lines ask the same of
// the prerequisite's rank spells, and the tier line reads the tab's points
// spent. During a preview build those sites answer from the plan: a talent's
// rank spell counts as known when the planned rank reaches it, and the tab
// has its planned points as well. So the name, the "Rank x/y" line, the
// descriptions, the next rank and the red requirement lines are the
// engine's, for the planned rank. Every other spell, and every build that
// is not a preview, gets the engine's answer.
//
// In place of the green "click to learn" line, a preview build ends with the
// add-point line when a point can be added to the plan, where the learn
// line would stand: in the last block, just before the builder's Show,
// which sizes the tooltip with it and runs OnShow after it.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "talent/Preview.h"
#include "tooltip/Lines.h"

#include <cstdint>
#include <initializer_list>

namespace Talent::PreviewTooltip {

namespace {

using Knows_t = bool(__thiscall *)(const void *unit, uint32_t spellID);
using AddTextLine_t = void(__thiscall *)(void *tooltip, const char *text, const void *color,
                                         int wrap);
using Script_t = int(__fastcall *)(void *L);

// The preview build in progress: the talent the tooltip shows, and the
// player. Null outside one.
const uint8_t *g_row = nullptr;
const uint8_t *g_player = nullptr;

// Whether the plan reaches the rank of `spellID`: 1 or 0, or -1 when no
// preview build runs or the spell is not a rank of the player's talents.
int PlannedKnows(uint32_t spellID) {
    if (g_row == nullptr)
        return -1;
    int slot = 0;
    const uint8_t *row = Preview::RowOfRankSpell(spellID, &slot);
    if (row == nullptr)
        return -1;
    return Preview::PreviewRank(g_player, row) > slot ? 1 : 0;
}

bool __fastcall KnowsSpell_r(const void *unit, void *, uint32_t spellID) {
    const int planned = PlannedKnows(spellID);
    if (planned >= 0)
        return planned != 0;
    return reinterpret_cast<Knows_t>(Offsets::FUN_UNIT_KNOWS_SPELL)(unit, spellID);
}

bool __fastcall KnowsHigherRank_r(const void *unit, void *, uint32_t spellID) {
    const int planned = PlannedKnows(spellID);
    if (planned >= 0)
        return planned != 0;
    return reinterpret_cast<Knows_t>(Offsets::FUN_UNIT_KNOWS_HIGHER_RANK)(unit, spellID);
}

// The tier line's points spent, with the tab's planned points during a
// preview build.
int __cdecl PlannedTabPoints(const uint8_t *tabInfo) {
    return g_row != nullptr ? Preview::TabPlanned(g_player, tabInfo) : 0;
}

constexpr uintptr_t addrTabPointsResume = Offsets::SITE_TALENT_REQUIREMENTS_TAB_POINTS_RESUME;
const uint8_t kTabPointsRead[] = {0x8B, 0x48, 0x04, 0xEB, 0x9C}; // mov ecx,[eax+4]; jmp

// Stands in for `mov ecx,[eax+4]; jmp resume` (eax = the TabInfo): ecx gets
// the points spent plus PlannedTabPoints, every other register is kept.
__declspec(naked) void TabPointsThunk() {
    __asm {
        mov ecx, [eax + 4]
        pushad
        push eax
        call PlannedTabPoints
        add esp, 4
        add [esp + 24], eax ; the saved ecx
        popad
        add esp, 4 ; the call's return address
        push addrTabPointsResume
        ret
    }
}

// The builders' TOOLTIP_TALENT_LEARN line, which a preview build leaves out.
void __fastcall LearnLine_r(void *tooltip, void *, const char *text, const void *color, int wrap) {
    if (g_row != nullptr)
        return;
    reinterpret_cast<AddTextLine_t>(Offsets::FUN_GAMETOOLTIP_ADD_TEXT_LINE)(tooltip, text, color,
                                                                           wrap);
}

void AddPreviewLines(void *tooltip) {
    if (!Preview::CanChange(g_player, g_row, 1))
        return;
    const auto &green = *reinterpret_cast<const uint32_t *>(Offsets::VAR_TOOLTIP_GREEN_COLOR);
    ::Tooltip::Lines::Add(tooltip, Game::Lua::GlobalText("TALENT_TOOLTIP_ADDPREVIEWPOINT"), green);
}

// Before BuildSpell's Show, with its talent and rebuild arguments. SetTalent
// adds the next rank's block when it shows a rank short of the last, so the
// line goes in that block.
void __cdecl BeforeSpellShow(void *tooltip, const void *talent, int rebuild) {
    if (g_row == nullptr || talent != g_row)
        return;
    const int rank = Preview::PreviewRank(g_player, g_row);
    if (rebuild != 0 || rank == 0 || rank >= Preview::MaxRank(g_row))
        AddPreviewLines(tooltip);
}

// Before BuildPassiveTalent's Show; SetTalent is its only caller.
void __cdecl BeforePassiveShow(void *tooltip) {
    if (g_row != nullptr)
        AddPreviewLines(tooltip);
}

static_assert(Offsets::VTBL_FRAME_SHOW == 0x88, "the thunks call [vtable + 88h]");
const uint8_t kSpellShowCall[] = {0xFF, 0x92, 0x88, 0x00, 0x00, 0x00};   // call [edx+88h]
const uint8_t kPassiveShowCall[] = {0xFF, 0x90, 0x88, 0x00, 0x00, 0x00}; // call [eax+88h]

// Stand in for the Show call: run the helper, keep every register, and go
// on to Show with the call's own return address.
__declspec(naked) void SpellShowThunk() {
    __asm {
        pushad
        push dword ptr [ebx + 20h] ; rebuild
        push dword ptr [ebx + 1Ch] ; talent
        push ecx                   ; tooltip
        call BeforeSpellShow
        add esp, 12
        popad
        jmp dword ptr [edx + 88h]
    }
}

__declspec(naked) void PassiveShowThunk() {
    __asm {
        pushad
        push ecx
        call BeforePassiveShow
        add esp, 4
        popad
        jmp dword ptr [eax + 88h]
    }
}

Script_t g_origSetTalent = nullptr;

int __fastcall SetTalent_h(void *L) {
    // A tooltip handler can call SetTalent in the middle of a build.
    const uint8_t *outerRow = g_row;
    const uint8_t *outerPlayer = g_player;
    g_row = nullptr;
    if (Game::Lua::IsNumber(L, 2) && Game::Lua::IsNumber(L, 3) && Game::Lua::ToFlag(L, 7) &&
        !Game::Lua::ToFlag(L, 4) && Preview::IsPlayerGroup(L, 5, 6) &&
        Game::Lua::ResolveTooltip(L, 1, false) != nullptr) {
        g_player = Preview::Player();
        if (g_player != nullptr)
            g_row = Preview::TalentRow(static_cast<int>(Game::Lua::ToNumber(L, 2)) - 1,
                                       static_cast<int>(Game::Lua::ToNumber(L, 3)) - 1);
    }
    const int ret = g_origSetTalent(L);
    g_row = outerRow;
    g_player = outerPlayer;
    return ret;
}

const Game::HookAutoRegister _setTalentHook{Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_TALENT,
                                            reinterpret_cast<void *>(&SetTalent_h),
                                            reinterpret_cast<void **>(&g_origSetTalent)};

void Register() {
    const auto knows = reinterpret_cast<const void *>(&KnowsSpell_r);
    const auto higher = reinterpret_cast<const void *>(&KnowsHigherRank_r);
    for (uintptr_t site : {Offsets::CALL_SET_TALENT_KNOWS_SPELL,
                           Offsets::CALL_BUILD_SPELL_TALENT_KNOWS_SPELL,
                           Offsets::CALL_BUILD_PASSIVE_TALENT_KNOWS_SPELL,
                           Offsets::CALL_TALENT_REQUIREMENTS_KNOWS_SPELL})
        Common::RedirectCall(site, Offsets::FUN_UNIT_KNOWS_SPELL, knows);
    for (uintptr_t site : {Offsets::CALL_SET_TALENT_KNOWS_HIGHER_RANK,
                           Offsets::CALL_BUILD_SPELL_TALENT_KNOWS_HIGHER_RANK,
                           Offsets::CALL_BUILD_PASSIVE_TALENT_KNOWS_HIGHER_RANK,
                           Offsets::CALL_TALENT_REQUIREMENTS_KNOWS_HIGHER_RANK})
        Common::RedirectCall(site, Offsets::FUN_UNIT_KNOWS_HIGHER_RANK, higher);
    Common::ReplaceWithCall(Offsets::SITE_TALENT_REQUIREMENTS_TAB_POINTS, kTabPointsRead,
                            sizeof kTabPointsRead,
                            reinterpret_cast<const void *>(&TabPointsThunk));
    for (uintptr_t site :
         {Offsets::CALL_BUILD_SPELL_TALENT_LEARN_LINE, Offsets::CALL_BUILD_PASSIVE_TALENT_LEARN_LINE})
        Common::RedirectCall(site, Offsets::FUN_GAMETOOLTIP_ADD_TEXT_LINE,
                             reinterpret_cast<const void *>(&LearnLine_r));
    Common::ReplaceWithCall(Offsets::SITE_BUILD_SPELL_SHOW, kSpellShowCall, sizeof kSpellShowCall,
                            reinterpret_cast<const void *>(&SpellShowThunk));
    Common::ReplaceWithCall(Offsets::SITE_BUILD_PASSIVE_TALENT_SHOW, kPassiveShowCall,
                            sizeof kPassiveShowCall,
                            reinterpret_cast<const void *>(&PassiveShowThunk));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Talent::PreviewTooltip
