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
#include "dbc/Lookup.h"
#include "spell/Tooltip.h"

#include <cstdint>

namespace Talent::Tooltip {

// Walks every (tab, talent) entry the player has loaded and returns
// the (1-based) tab/idx pair where `TalentEntry+0x00 == talentID`.
// Returns false if the talent isn't in any of the player's tabs —
// 1.12 only loads the local player's class talent data, so other
// classes' talents aren't findable here. Same access pattern as
// `Talent::Info::Script_GetTalentIDByIndex` (#42), inverted.
static bool FindTalentByID(uint32_t talentID, int *outTab, int *outIdx) {
    const int tabCount = Game::Read<int>(Offsets::VAR_TALENT_TAB_COUNT);
    auto *tabs = Game::Read<const uint8_t *const *>(
        Offsets::VAR_TALENT_TAB_INFO_ARRAY);
    if (tabs == nullptr)
        return false;

    for (int t = 1; t <= tabCount; ++t) {
        const uint8_t *tabInfo = tabs[t - 1];
        if (tabInfo == nullptr)
            continue;
        const int numTalents = Game::Read<int>(
            tabInfo, Offsets::OFF_TABINFO_NUM_TALENTS);
        auto *talents = Game::Read<const uint8_t *>(
            tabInfo, Offsets::OFF_TABINFO_TALENT_ARRAY);
        if (talents == nullptr)
            continue;

        for (int i = 1; i <= numTalents; ++i) {
            const uint8_t *entry =
                talents + (i - 1) * Offsets::TALENT_ENTRY_STRIDE;
            const uint32_t id = *reinterpret_cast<const uint32_t *>(entry);
            if (id == talentID) {
                *outTab = t;
                *outIdx = i;
                return true;
            }
        }
    }
    return false;
}

// Looks up the rank-1 spellID for a given talentID via Talent.dbc.
// Returns 0 if the ID is out of range, the slot is unpopulated, or
// the rank-1 SpellRank is missing. Records pointer is at the standard
// DBC class shape (`+0x08` of class instance), indexed directly by
// talentID; sparse — many slots are NULL across classes.
static uint32_t LookupTalentRank1Spell(uint32_t talentID) {
    const uint8_t *rec = DBC::Record(
        Offsets::VAR_TALENT_DBC_RECORDS, Offsets::VAR_TALENT_DBC_COUNT,
        talentID);
    if (rec == nullptr)
        return 0;
    return Game::Read<uint32_t>(rec, Offsets::OFF_TALENT_SPELL_RANK);
}

// `GameTooltip:SetTalentByID(talentID)` — modern method that renders a
// talent tooltip from just a Talent.dbc primary key. The 1.12
// equivalent is `GameTooltip:SetTalent(tabIndex, talentIndex)`, which
// only renders talents from the local player's loaded tabs (one class
// only).
//
// Two-tier resolution:
//
//   1. **Player-class fast path.** Walk the local player's TabInfo
//      arrays for matching talentID. If found, dispatch to
//      `Script_GameTooltip_SetTalent` (registry slot 34, `0x00535170`)
//      with the resolved (tab, idx). Renders the rich talent tooltip
//      with rank info, prereqs, "click to learn next rank" — same as
//      hovering the talent in the talent UI.
//
//   2. **Cross-class fallback via Talent.dbc.** Look up the talent
//      record by ID in the Talent.dbc array at `[VAR_TALENT_DBC_RECORDS]`,
//      read the rank-1 spellID at `+0x10`, and dispatch the spell
//      tooltip via `Spell::Tooltip::ShowByID` (the same path
//      `GameTooltip:SetSpellByID` uses). Renders the spell info —
//      cast time, range, mana cost, description — without the talent-
//      specific decorations (rank counter, prereq lines), since those
//      live in the per-player engine machinery we can't reach
//      cross-class.
//
// Modern WoW's `SetTalentByID` shows a richer cross-class tooltip
// (talent name + "Rank 0/N" + spell description). Building that
// requires Lua-level GameTooltip:AddLine work; for now the spell
// tooltip is functionally equivalent for "what does this talent do?"
// queries, which is the dominant use case.
//
// Silent no-op when `talentID` doesn't match any record (Talent.dbc
// is sparse — talent IDs are not contiguous). Errors via `lua_error`
// only on type mismatch (non-table self, non-number talentID).
static int __fastcall Script_GameTooltipSetTalentByID(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: GameTooltip:SetTalentByID(talentID)");
        return 0;
    }
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: GameTooltip:SetTalentByID(talentID)");
        return 0;
    }
    const int talentID = static_cast<int>(Game::Lua::ToNumber(L, 2));
    if (talentID <= 0)
        return 0;

    // Tier 1 — player's class. Rich talent tooltip via SetTalent.
    int tab = 0, idx = 0;
    if (FindTalentByID(static_cast<uint32_t>(talentID), &tab, &idx)) {
        Game::Lua::SetTop(L, 1);
        Game::Lua::PushNumber(L, static_cast<double>(tab));
        Game::Lua::PushNumber(L, static_cast<double>(idx));
        using Script_t = int(__fastcall *)(void *L);
        auto fn = reinterpret_cast<Script_t>(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_TALENT);
        return fn(L);
    }

    // Tier 2 — cross-class via Talent.dbc + spell tooltip fallback.
    const uint32_t spellID = LookupTalentRank1Spell(static_cast<uint32_t>(talentID));
    if (spellID == 0)
        return 0;
    Spell::Tooltip::ShowByID(L, static_cast<int>(spellID));
    return 0;
}

static const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetTalentByID", &Script_GameTooltipSetTalentByID},
};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_GAMETOOLTIP_METHOD_REGISTRY), g_methods);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Talent::Tooltip
