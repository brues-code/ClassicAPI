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

// `GameTooltip:SetAction(slot)` for macro slots with a `#showtooltip`
// directive: the tooltip of the spell or item the directive resolved to,
// instead of the macro's name.
//
// Registered on the GameTooltip method registry in front of the engine's
// `SetAction` (most recent registration wins — the mechanism
// `Texture::Desaturation` relies on) and a strict superset of it: anything
// that isn't a macro with a directive of ours tail-calls the engine function
// with the untouched stack. `#show` is icon/count only, so it gets the macro's
// name — built here rather than delegated; see `ShowMacroName`.
//
//   - Spell: the engine's own spell branch, in its order — per-slot cooldown
//     (`FUN_ACTION_SLOT_COOLDOWN`, which for a macro slot is the cached
//     primary spell's, i.e. ours), clear, the `UberTooltips` brief flag, then
//     `FUN_GAMETOOLTIP_BUILD_SPELL_TOOLTIP(spellID, brief, remainingMs, 0, 1,
//     0, 0)`. Verified against the disassembly of `0x005322A0` (the seven
//     pushes at 0x00532655..0x00532672). A spellID with no `Spell.dbc` row
//     goes back to the engine instead: it would build nothing, and the macro's
//     name is a better answer than an empty frame — the same fallback 3.3.5's
//     macro tooltip takes when neither an item nor a spell resolved.
//   - Item: the engine's own Lua entry points by stack reshape — the equipped
//     instance via `SetInventoryItem("player", slot)`, a bag instance via
//     `SetBagItem(bag, slot)`, an item no longer carried via `SetHyperlink`
//     on a bare `item:` link. Same approach as `Item::Tooltip`'s
//     `SetInventoryItemByID`.
//   - Refresh: truthy when the builder wants another pass (a ticking cooldown)
//     or the directive is conditional — see `PushRefresh`.

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "item/Arg.h"
#include "item/Location.h"
#include "macro/ShowTooltip.h"
#include "spell/Lookup.h"
#include "time/Clock.h"

#include <cstdint>
#include <cstdio>

namespace Tooltip::SetAction {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);
using SlotCooldown_t = void(__fastcall *)(uint32_t slot0, int *start, int *duration,
                                          uint32_t *enable);
using TooltipClear_t = void(__thiscall *)(void *tooltip);
using BuildSpellTooltip_t = int(__thiscall *)(void *tooltip, int spellID, int brief,
                                              int cooldownRemainingMs, int a4, int showRank,
                                              int a6, int a7);

int CallScript(uintptr_t fn, void *L) {
    return reinterpret_cast<ScriptFn_t>(fn)(L);
}

// FrameXML reads this return into `updateTooltip`: truthy keeps the tooltip
// re-reading while the cursor rests on the button. 3.3.5's macro tooltip
// returns `dynamic | built` (`FUN_00630d20`), so a directive carrying
// conditions refreshes even when the builder has nothing pending — its answer
// can change under a held modifier or a new mouseover with no event to say so.
int PushRefresh(void *L, bool refresh) {
    if (refresh)
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

int ShowSpell(void *L, void *tooltip, int slot0, int spellID, bool conditional) {
    int start = 0, duration = 0;
    uint32_t enable = 0;
    reinterpret_cast<SlotCooldown_t>(Offsets::FUN_ACTION_SLOT_COOLDOWN)(
        static_cast<uint32_t>(slot0), &start, &duration, &enable);
    reinterpret_cast<TooltipClear_t>(Offsets::FUN_GAMETOOLTIP_CLEAR)(tooltip);

    // The engine shows the brief form unless the UberTooltips cvar is on.
    const int brief =
        CVar::Factory::GetInt(CVar::Factory::Find("UberTooltips"), 0) != 0 ? 0 : 1;
    int remaining = 0;
    if (enable != 0 && start != 0 && duration != 0) {
        remaining = static_cast<int>(Time::Clock::Remaining(
            Time::Clock::NowMs(), static_cast<uint32_t>(start) + static_cast<uint32_t>(duration)));
    }
    const int built = reinterpret_cast<BuildSpellTooltip_t>(
        Offsets::FUN_GAMETOOLTIP_BUILD_SPELL_TOOLTIP)(tooltip, spellID, brief, remaining, 0, 1, 0, 0);
    return PushRefresh(L, built != 0 || conditional);
}

// `#show` is icon-only: the tooltip stays the macro's own name. We build that
// instead of delegating, because the engine's macro branch
// (`FUN_GAMETOOLTIP_SET_MACRO`) is detoured on this client by SuperWoW into a
// 3.3.5-style builder that replaces the tooltip with whatever spell sits in the
// macro's primary-spell cache — and for `#show` that is the spell WE put there,
// deliberately, so the icon, cooldown, range and usable state are right. Since
// the field cannot carry the `#show` / `#showtooltip` distinction, honoring the
// directive means owning this tooltip. `Script_SetText` runs the same sequence
// the engine's macro branch does (clear, one line, show).
int ShowMacroName(void *L, const char *name) {
    Game::Lua::SetTop(L, 1); // keep self at stack[1]
    Game::Lua::PushString(L, name);
    CallScript(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_TEXT, L);
    // `SetText` pushes nothing, and a name never needs a second pass — the
    // engine answers nil for a macro tooltip too.
    return PushRefresh(L, false);
}

int ShowItem(void *L, int itemID, bool conditional) {
    Item::Arg::Resolved arg{itemID, 0, nullptr};
    Item::Location::ByGUIDResult found;
    Game::Lua::SetTop(L, 1); // keep self at stack[1]
    int n = 0;
    if (Item::Location::FindByArgNoLua(arg, &found)) {
        if (found.equipmentSlotIndex != 0) {
            Game::Lua::PushString(L, "player");
            Game::Lua::PushNumber(L, static_cast<double>(found.equipmentSlotIndex));
            n = CallScript(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_INVENTORY_ITEM, L);
        } else {
            Game::Lua::PushNumber(L, static_cast<double>(found.bagID));
            Game::Lua::PushNumber(L, static_cast<double>(found.slotIndex));
            n = CallScript(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_BAG_ITEM, L);
        }
    } else {
        char link[64];
        std::snprintf(link, sizeof(link), "item:%d:0:0:0:0:0:0:0", itemID);
        Game::Lua::PushString(L, link);
        n = CallScript(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_HYPERLINK, L);
    }
    // The engine entry point answers the refresh question for itself; a
    // conditional directive needs it forced on top of whatever it said.
    return conditional ? PushRefresh(L, true) : n;
}

int __fastcall Script_SetAction(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE || !Game::Lua::IsNumber(L, 2))
        return CallScript(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_ACTION, L); // engine's own errors
    const int slot0 = static_cast<int>(Game::Lua::ToNumber(L, 2)) - 1;

    Macro::ShowTooltip::Info info;
    if (!Macro::ShowTooltip::ForSlot(slot0, &info))
        return CallScript(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_ACTION, L);

    if (info.kind == Macro::ShowTooltip::Kind::Show) {
        if (const char *name = Macro::ShowTooltip::MacroNameForSlot(slot0))
            return ShowMacroName(L, name);
        return CallScript(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_ACTION, L);
    }
    if (info.kind != Macro::ShowTooltip::Kind::ShowTooltip)
        return CallScript(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_ACTION, L);

    if (info.target == Macro::ShowTooltip::Target::Spell) {
        // A spellID with no `Spell.dbc` row builds nothing at all, which would
        // leave the macro with an empty frame where the engine would have shown
        // its name. An override must never be worse than what it replaces, so
        // anything we cannot describe goes back to the engine.
        void *tooltip = Game::Lua::ResolveTooltip(L);
        if (tooltip == nullptr ||
            ::Spell::Lookup::RecordForID(static_cast<int>(info.spellID)) == nullptr)
            return CallScript(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_ACTION, L);
        return ShowSpell(L, tooltip, slot0, static_cast<int>(info.spellID), info.conditional);
    }
    if (info.target == Macro::ShowTooltip::Target::Item)
        return ShowItem(L, info.itemID, info.conditional);
    return CallScript(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_ACTION, L);
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetAction", &Script_SetAction},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_GAMETOOLTIP_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Tooltip::SetAction
