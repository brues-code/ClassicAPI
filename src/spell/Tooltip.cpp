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
#include "spell/Lookup.h"
#include "spell/Tooltip.h"

#include <cstdint>

namespace Spell::Tooltip {

// Spell.dbc record offsets used by GetSpell. Kept local rather than
// pulled from Spell::Info — that module keeps these as `static
// constexpr` privates and exposing them just for two readers here
// would force a header change.

using BuildSpellTooltip_t = void(__thiscall *)(void *thisObj, int spellID, int arg2, int arg3,
                                               int isPet, int showRank, int arg6, int arg7);

void ShowByID(void *L, int spellID) {
    if (spellID <= 0)
        return;
    void *tooltipObj = Game::Lua::ResolveTooltip(L);
    if (tooltipObj == nullptr)
        return;
    auto BuildSpellTooltip =
        reinterpret_cast<BuildSpellTooltip_t>(Offsets::FUN_GAMETOOLTIP_BUILD_SPELL_TOOLTIP);
    BuildSpellTooltip(tooltipObj, spellID, 0, 0, 0, /*showRank=*/1, 0, 0);
}

static int __fastcall Script_GameTooltipSetSpellByID(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: GameTooltipSetSpellByID(self, spellID)");
        return 0;
    }
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: GameTooltipSetSpellByID(self, spellID)");
        return 0;
    }
    const int spellID = static_cast<int>(Game::Lua::ToNumber(L, 2));
    ShowByID(L, spellID);
    return 0;
}

// `GameTooltip:AddSpellByID(spellID)` — APPENDS a spell's tooltip to the
// current one instead of clearing+rebuilding (the `SetSpellByID` behavior).
// The natural pair to `SetSpellByID`; lets callers compose tooltips like
// `AddLine(talentName) + AddSpellByID(rankSpellID)`.
//
// `BuildSpellTooltip`'s last arg (`param_7`) gates the clear: `0` calls the
// per-tooltip Clear (`FUN_00530050`) first — that's `SetSpellByID`; non-zero
// SKIPS the clear and appends. The append path is the engine's talent
// "next rank" preview, so it (a) emits a `"\nNext rank:"` header line
// instead of the spell name, and (b) does NOT stash the displayed spellID at
// `+0x39C` (so `GetSpell` keeps reflecting the base tooltip — correct for an
// append). We fix (a) by overwriting that header line — which lands at index
// `numLines`-before-the-build (0-based) — with the real spell name via the
// line pool's left-text FontString. Verified against `FUN_0052E610`.
void AppendByID(void *L, int spellID) {
    if (spellID <= 0)
        return;
    void *tooltipObj = Game::Lua::ResolveTooltip(L);
    if (tooltipObj == nullptr)
        return;
    auto *tt = static_cast<uint8_t *>(tooltipObj);

    const int before = Game::Read<int>(
        tt, Offsets::OFF_GAMETOOLTIP_NUM_LINES);

    auto BuildSpellTooltip =
        reinterpret_cast<BuildSpellTooltip_t>(Offsets::FUN_GAMETOOLTIP_BUILD_SPELL_TOOLTIP);
    BuildSpellTooltip(tooltipObj, spellID, 0, 0, 0, 0, 0, /*param_7 (append)=*/1);

    const int after = Game::Read<int>(
        tt, Offsets::OFF_GAMETOOLTIP_NUM_LINES);
    if (after <= before)
        return; // nothing appended (bad spellID / missing Spell.dbc record)

    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return;
    const int locale = Game::Read<int>(Offsets::VAR_LOCALE_INDEX);
    const char *name = Game::Read<const char *>(
        record, Offsets::OFF_SPELL_NAMES + locale * 4);
    if (name == nullptr || name[0] == '\0')
        return;

    // Left-text FontString array: descriptor at +0x324, data ptr at +0x8.
    auto **textLeft = Game::Read<void **>(
        tt, Offsets::OFF_GAMETOOLTIP_TEXTLEFT_DESC + 8);
    if (textLeft == nullptr)
        return;
    void *fs = textLeft[before];
    if (fs == nullptr)
        return;
    using SetText_t = void(__thiscall *)(void *fs, const char *text, int flag);
    reinterpret_cast<SetText_t>(Offsets::FUN_FONTSTRING_SET_TEXT)(fs, name, 0);
}

static int __fastcall Script_GameTooltipAddSpellByID(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE ||
        !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: GameTooltip:AddSpellByID(spellID)");
        return 0;
    }
    AppendByID(L, static_cast<int>(Game::Lua::ToNumber(L, 2)));
    return 0;
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

static void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_GAMETOOLTIP_METHOD_REGISTRY),
        g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::Tooltip
