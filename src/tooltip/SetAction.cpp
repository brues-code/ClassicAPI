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
//   - Equipment set: `GameTooltip:SetEquipmentSet(name)`'s builder, as
//     3.3.5's `SetAction` (`FUN_00631000`) calls the set builder
//     `FUN_00622dd0` for an `0x20000000` slot. Always refreshes — the
//     equipped / in-bags counts move while the pointer rests.
//   - Refresh: truthy when the builder wants another pass (a ticking cooldown)
//     or the directive is conditional — see `PushRefresh`.
//   - Slots 121..360 (Action::Extended): everything above applies, and where
//     the engine function would run, its branches run on our word instead
//     (`ShowOurs`), since the engine reads VAR_ACTION_TABLE unbounded.

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "equipmentset/Action.h"
#include "equipmentset/Data.h"
#include "equipmentset/Tooltip.h"
#include "item/Arg.h"
#include "item/Location.h"
#include "macro/ShowTooltip.h"
#include "action/Extended.h"
#include "action/ExtendedState.h"
#include "item/Record.h"
#include "object/Resolve.h"
#include "spell/Lookup.h"
#include "time/Clock.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstdio>

namespace Tooltip::SetAction {

namespace {

using SlotCooldown_t = void(__fastcall *)(uint32_t slot0, uint32_t *start, uint32_t *duration,
                                          uint32_t *enable);
using TooltipClear_t = void(__thiscall *)(void *tooltip);
using BuildSpellTooltip_t = int(__thiscall *)(void *tooltip, int spellID, int brief,
                                              int cooldownRemainingMs, int a4, int showRank,
                                              int a6, int a7);

// FrameXML reads this return into `updateTooltip`: truthy keeps the tooltip
// re-reading while the cursor rests on the button. 3.3.5's macro tooltip
// returns `dynamic | built` (`FUN_00630d20`), so a directive carrying
// conditions refreshes even when the builder has nothing pending — its answer
// can change under a held modifier or a new mouseover with no event to say so.
int PushRefresh(void *L, bool refresh) {
    Game::Lua::PushFlag(L, refresh);
    return 1;
}

// The slot's cooldown, as the engine's SetAction reads it first: 1..120
// FUN_ACTION_SLOT_COOLDOWN, Action::Extended's slots its mirror. A macro
// slot's is its cached primary spell's.
struct SlotCooldown {
    uint32_t startMs = 0;
    uint32_t durationMs = 0;
    bool enabled = false;
    bool Running() const { return enabled && startMs != 0 && durationMs != 0; }
    int RemainingMs() const {
        return Running() ? static_cast<int>(Time::Clock::Remaining(Time::Clock::NowMs(),
                                                                    startMs + durationMs))
                         : 0;
    }
};

SlotCooldown CooldownOf(int slot0) {
    SlotCooldown cd;
    if (slot0 >= 0 && slot0 < Offsets::ACTION_TABLE_MAX_SLOTS) {
        uint32_t enable = 0;
        reinterpret_cast<SlotCooldown_t>(Offsets::FUN_ACTION_SLOT_COOLDOWN)(
            static_cast<uint32_t>(slot0), &cd.startMs, &cd.durationMs, &enable);
        cd.enabled = enable != 0;
    } else {
        const Action::ExtendedState::Cooldown ours =
            Action::ExtendedState::CooldownFor(Action::Extended::Get(slot0));
        cd.startMs = ours.startMs;
        cd.durationMs = ours.durationMs;
        cd.enabled = ours.enabled;
    }
    return cd;
}

// The engine shows the brief form unless the UberTooltips cvar is on.
int Brief() { return CVar::Factory::GetInt(CVar::Factory::Find("UberTooltips"), 0) != 0 ? 0 : 1; }

// The spell branch (0x00532618..0x0053268B): with the cooldown running the
// builder gets the ms left and its result decides the refresh; without one it
// gets 0 and the answer is nil.
int ShowSpell(void *L, void *tooltip, const SlotCooldown &cd, int spellID, bool conditional) {
    reinterpret_cast<TooltipClear_t>(Offsets::FUN_GAMETOOLTIP_CLEAR)(tooltip);
    const int built = reinterpret_cast<BuildSpellTooltip_t>(
        Offsets::FUN_GAMETOOLTIP_BUILD_SPELL_TOOLTIP)(tooltip, spellID, Brief(), cd.RemainingMs(), 0,
                                                       1, 0, 0);
    return PushRefresh(L, (cd.Running() && built != 0) || conditional);
}

// Script_GameTooltip_SetAction (0x005322A0) for one of Action::Extended's
// slots, branch for branch with its word (disassembled):
//   cooldown, clear; attack: the ATTACK global string as the one line, nil;
//   empty: nil; item carried (FUN_INVMGR_FIND_ITEM_BY_ID, flags 0): clear the
//   item fields, cooldown ms left, the charge total for a charged template,
//   FUN_GAMETOOLTIP_BUILD_ITEM(entry, &guid, &guid, 0, 0, 1, 0, 0, 0), refresh
//   from its result; item not carried: BUILD_ITEM with no GUID, nil; macro:
//   FUN_GAMETOOLTIP_SET_MACRO, nil; spell: ShowSpell.
// The ATTACK line is set through SetText, the clear + one line + Show the
// engine's attack branch runs.
int ShowOurs(void *L, int slot0) {
    using SetMacro_t = void(__thiscall *)(void *tooltip, uint32_t macroID);
    using ClearItemFields_t = void(__thiscall *)(void *fields);
    using FindItem_t = const uint8_t *(__thiscall *)(const void *invMgr, uint32_t itemID,
                                                     uint32_t flags);
    using CountCharges_t = int(__thiscall *)(const void *invMgr, uint32_t itemID, uint32_t flags);
    using BuildItem_t = int(__thiscall *)(void *tooltip, uint32_t itemID, const void *guid,
                                          const void *guid2, int a4, int a5, int a6, int headerFlag,
                                          int a8, int a9);

    void *tooltip = Game::Lua::ResolveTooltip(L);
    if (tooltip == nullptr)
        return 0;
    const uint32_t word = Action::Extended::Get(slot0);
    const SlotCooldown cd = CooldownOf(slot0);
    reinterpret_cast<TooltipClear_t>(Offsets::FUN_GAMETOOLTIP_CLEAR)(tooltip);

    if (Action::ExtendedState::IsAttack(word)) {
        Game::Lua::SetTop(L, 1); // keep self at stack[1]
        Game::Lua::PushString(L, Game::Lua::GlobalText("ATTACK"));
        Game::Lua::CallScript(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_TEXT, L);
        return PushRefresh(L, false);
    }
    if (word == 0)
        return PushRefresh(L, false);

    const uint32_t tag = word & Offsets::ACTION_TYPE_MASK;
    if (tag == Offsets::ACTION_TYPE_ITEM_BY_ID) {
        const uint32_t itemID = word & Offsets::ACTION_PAYLOAD_MASK_ITEM_BY_ID;
        const auto build = reinterpret_cast<BuildItem_t>(Offsets::FUN_GAMETOOLTIP_BUILD_ITEM);
        const uint8_t *invMgr =
            Unit::Identity::PlayerObject() != nullptr ? Unit::Identity::PlayerInventoryManager()
                                                      : nullptr;
        const uint8_t *item =
            invMgr != nullptr
                ? reinterpret_cast<FindItem_t>(Offsets::FUN_INVMGR_FIND_ITEM_BY_ID)(invMgr, itemID, 0)
                : nullptr;
        if (item == nullptr) {
            const uint64_t none = 0;
            build(tooltip, itemID, &none, &none, 0, 0, 0, 0, 0, 0);
            return PushRefresh(L, false);
        }
        uint8_t *fields = static_cast<uint8_t *>(tooltip) + Offsets::OFF_GAMETOOLTIP_ITEM_FIELDS;
        reinterpret_cast<ClearItemFields_t>(Offsets::FUN_GAMETOOLTIP_CLEAR_ITEM_FIELDS)(fields);
        Game::Ref<uint32_t>(tooltip, Offsets::OFF_GAMETOOLTIP_ITEM_COOLDOWN_MS) =
            static_cast<uint32_t>(cd.RemainingMs());
        if (const uint8_t *record = Item::PeekRecord(itemID)) {
            const int32_t charges = Game::Read<int32_t>(record, Offsets::OFF_ITEMSTATS_SPELL_CHARGES);
            if (charges != -1 && charges != 0)
                Game::Ref<int32_t>(tooltip, Offsets::OFF_GAMETOOLTIP_ITEM_CHARGES) =
                    reinterpret_cast<CountCharges_t>(Offsets::FUN_INVMGR_COUNT_CHARGES_BY_ID)(
                        invMgr, itemID, 0);
        }
        const uint64_t guid = ::Object::Guid(item);
        const int built = build(tooltip, ::Object::Entry(item), &guid, &guid, 0, 0, 1, 0, 0, 0);
        return PushRefresh(L, built != 0);
    }
    if (tag == Offsets::ACTION_TYPE_BAG_OR_MACRO) {
        reinterpret_cast<SetMacro_t>(Offsets::FUN_GAMETOOLTIP_SET_MACRO)(
            tooltip, word & Offsets::ACTION_PAYLOAD_MASK_BAG_OR_MACRO);
        return PushRefresh(L, false);
    }
    if (tag != Offsets::ACTION_TYPE_SPELL)
        return PushRefresh(L, false);
    uint32_t book = 0;
    return ShowSpell(L, tooltip, cd, static_cast<int>(Action::Extended::SpellFor(word, &book)),
                     false);
}

// The engine's SetAction for its slots (and its own errors), ours for 121..360.
int Default(void *L, int slot0) {
    if (Action::Extended::Owns(slot0))
        return ShowOurs(L, slot0);
    return Game::Lua::CallScript(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_ACTION, L);
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
    Game::Lua::CallScript(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_TEXT, L);
    // `SetText` pushes nothing, and a name never needs a second pass — the
    // engine answers nil for a macro tooltip too.
    return PushRefresh(L, false);
}

int ShowItem(void *L, int itemID, bool conditional) {
    Item::Arg::Resolved arg{itemID, 0, nullptr};
    Item::Location::ByGUIDResult found;
    Game::Lua::SetTop(L, 1); // keep self at stack[1]
    int n = 0;
    if (Item::Location::FindByArg(arg, &found)) {
        if (found.equipmentSlotIndex != 0) {
            Game::Lua::PushString(L, "player");
            Game::Lua::PushNumber(L, static_cast<double>(found.equipmentSlotIndex));
            n = Game::Lua::CallScript(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_INVENTORY_ITEM, L);
        } else {
            Game::Lua::PushNumber(L, static_cast<double>(found.bagID));
            Game::Lua::PushNumber(L, static_cast<double>(found.slotIndex));
            n = Game::Lua::CallScript(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_BAG_ITEM, L);
        }
    } else {
        char link[64];
        std::snprintf(link, sizeof(link), "item:%d:0:0:0:0:0:0:0", itemID);
        Game::Lua::PushString(L, link);
        n = Game::Lua::CallScript(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_HYPERLINK, L);
    }
    // The engine entry point answers the refresh question for itself; a
    // conditional directive needs it forced on top of whatever it said.
    return conditional ? PushRefresh(L, true) : n;
}

int __fastcall Script_SetAction(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE || !Game::Lua::IsNumber(L, 2))
        return Game::Lua::CallScript(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_ACTION, L); // engine's own errors
    const int slot0 = static_cast<int>(Game::Lua::ToNumber(L, 2)) - 1;

    if (slot0 >= 0) {
        if (const uint32_t setID = EquipmentSet::Action::SlotSetID(static_cast<uint32_t>(slot0))) {
            const EquipmentSet::Set *set = EquipmentSet::Data::FindByID(setID);
            if (set == nullptr)
                return PushRefresh(L, false);
            Game::Lua::SetTop(L, 1); // keep self at stack[1]
            Game::Lua::PushString(L, set->name.c_str());
            EquipmentSet::Tooltip::Script_GameTooltipSetEquipmentSet(L);
            return PushRefresh(L, true);
        }
    }

    Macro::ShowTooltip::Info info;
    if (!Macro::ShowTooltip::ForSlot(slot0, &info))
        return Default(L, slot0);

    if (info.kind == Macro::ShowTooltip::Kind::Show) {
        if (const char *name = Macro::ShowTooltip::MacroNameForSlot(slot0))
            return ShowMacroName(L, name);
        return Default(L, slot0);
    }
    if (info.kind != Macro::ShowTooltip::Kind::ShowTooltip)
        return Default(L, slot0);

    if (info.target == Macro::ShowTooltip::Target::Spell) {
        // A spellID with no `Spell.dbc` row builds nothing at all, which would
        // leave the macro with an empty frame where the engine would have shown
        // its name. An override must never be worse than what it replaces, so
        // anything we cannot describe goes back to the engine.
        void *tooltip = Game::Lua::ResolveTooltip(L);
        if (tooltip == nullptr ||
            ::Spell::Lookup::RecordForID(static_cast<int>(info.spellID)) == nullptr)
            return Default(L, slot0);
        return ShowSpell(L, tooltip, CooldownOf(slot0), static_cast<int>(info.spellID),
                         info.conditional);
    }
    if (info.target == Macro::ShowTooltip::Target::Item)
        return ShowItem(L, info.itemID, info.conditional);
    return Default(L, slot0);
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetAction", &Script_SetAction},
};

const Game::Doc::Field kSetActionArgs[] = {
    Game::Doc::Req("slot", "luaIndex",
                   "The action slot, 1 to 360. For a macro with #showtooltip, the tooltip shows "
                   "the spell or item it names."),
};
const Game::Doc::Field kSetActionRets[] = {
    Game::Doc::Opt("refresh", "number", nullptr,
                   "1 when the tooltip should be set again soon, for example while a cooldown "
                   "runs; otherwise nil."),
};
const Game::Doc::Function kSetAction{"Shows the tooltip of an action bar slot.", kSetActionArgs,
                                     kSetActionRets};

const Game::Doc::Method g_methodDocs[] = {
    {"SetAction", &kSetAction},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_GAMETOOLTIP_METHOD_REGISTRY), g_methods,
        g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Tooltip::SetAction
