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

// `C_Macro.SetMacroDisplay(macroSlot, value [, unit])` — the Lua door onto
// `Macro::ShowTooltip::Publish`, for an addon that has its own macro parser
// and wants the display done properly rather than by replacing the action
// globals. See the ShowTooltip header for what publishing buys.
//
// Three states, which is all a publisher needs:
//
//   SetMacroDisplay(1, "Frostbolt")   -- resolve and show this
//   SetMacroDisplay(1, false)         -- mine, nothing matched, show `?`
//   SetMacroDisplay(1, nil)           -- released, our own parse resumes
//
// The value takes exactly the forms a `#showtooltip` line takes, so a
// publisher hands over what it would have written in the macro and gets the
// item-before-spell precedence, `item:N`, links, inventory slots and
// `bag slot` for free.
//
// `unit` is the `[@unit]` the publisher's clause matched: the unit the
// button's range check uses. Unlike `SetMacroSpell`'s target it stays a
// token, resolved at every lookup (`PublishOptions::liveUnit`), so a
// publisher whose clause still matches need not republish when the
// mouseover, focus or target moves.
//
// `SetMacroSpell(macro, spell [, target])` and `SetMacroItem(macro, item
// [, target])` are 4.3.4's narrower doors onto the same publish: a spellbook
// spell only, or an item only, aimed at `target` (the unit the button's range
// check uses, see `Action::Range`). As in 4.3.4 the claim lasts until the
// macro is edited, and a name that resolves to nothing greys the button.

#include "Game.h"
#include "Offsets.h"
#include "macro/Arg.h"
#include "macro/ShowTooltip.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Macro::Display {

namespace {

int __fastcall Script_C_Macro_SetMacroDisplay(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Macro.SetMacroDisplay(macroSlot, value [, unit])");
        return 0;
    }
    const int macroSlot = static_cast<int>(Game::Lua::ToNumber(L, 1));

    // Released when the value is absent or nil; claimed-but-unresolved for a
    // false or an empty string. `lua_type` reports NONE past the top of the
    // stack, so test the top rather than the type alone.
    const bool released = Game::Lua::GetTop(L) < 2 ||
                          Game::Lua::Type(L, 2) == Game::Lua::TYPE_NIL;
    if (released) {
        Macro::ShowTooltip::Release(macroSlot);
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }

    const char *value = Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : nullptr;
    Macro::ShowTooltip::PublishOptions options;
    // A value that names nothing aims at nothing, so its unit is not read.
    if (value != nullptr && value[0] != '\0' && Game::Lua::IsString(L, 3)) {
        options.unit = Game::Lua::ToString(L, 3);
        options.liveUnit = true;
        // The engine's resolver vets the token: anything that is not one
        // raises its "Unknown unit name" error, exactly as UnitExists does,
        // before anything is published.
        Unit::Identity::GuidForToken(options.unit);
    }
    Game::Lua::PushBoolean(L, Macro::ShowTooltip::Publish(macroSlot, value, options) ? 1 : 0);
    return 1;
}

int SetMacroValue(void *L, Macro::ShowTooltip::Accept accept, const char *usage) {
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, usage);
        return 0;
    }
    const int macroSlot = Macro::Arg::Slot(L);
    if (macroSlot == 0)
        return 0;
    Macro::ShowTooltip::PublishOptions options;
    options.accept = accept;
    options.unit = Game::Lua::IsString(L, 3) ? Game::Lua::ToString(L, 3) : nullptr;
    options.untilEdit = true;
    if (Macro::ShowTooltip::Publish(macroSlot, Game::Lua::ToString(L, 2), options))
        Game::Lua::PushNumber(L, 1);
    else
        Game::Lua::PushNil(L);
    return 1;
}

int __fastcall Script_SetMacroSpell(void *L) {
    return SetMacroValue(L, Macro::ShowTooltip::Accept::Spell,
                         "Usage: SetMacroSpell(macro, spell [,target])");
}

int __fastcall Script_SetMacroItem(void *L) {
    return SetMacroValue(L, Macro::ShowTooltip::Accept::Item,
                         "Usage: SetMacroItem(macro, item [,target])");
}

const Game::Doc::Field kSetMacroSpellArgs[] = {
    Game::Doc::Req("macro", "string", "The macro's index or name."),
    Game::Doc::Req("spell", "string", "The spell's name, with an optional \"(Rank N)\"."),
    Game::Doc::Opt("target", "string", nullptr, "The unit that the button's range check uses."),
};
const Game::Doc::Field kSetMacroItemArgs[] = {
    Game::Doc::Req("macro", "string", "The macro's index or name."),
    Game::Doc::Req("item", "string", "The item's name, item:N or link."),
    Game::Doc::Opt("target", "string", nullptr, "The unit that the button's range check uses."),
};
const Game::Doc::Field kSetMacroValueRets[] = {
    Game::Doc::Opt("found", "number", nullptr, "1 when the name was found."),
};
const Game::Doc::Function kSetMacroSpell{
    "Sets the spell that a macro's action button shows, until the macro is edited.",
    kSetMacroSpellArgs, kSetMacroValueRets, "MacroGlobals"};
const Game::Doc::Function kSetMacroItem{
    "Sets the item that a macro's action button shows, until the macro is edited.",
    kSetMacroItemArgs, kSetMacroValueRets, "MacroGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("SetMacroSpell", &Script_SetMacroSpell, &kSetMacroSpell);
    Game::Lua::RegisterGlobalFunction("SetMacroItem", &Script_SetMacroItem, &kSetMacroItem);
    Game::Lua::RegisterTableFunction("C_Macro", "SetMacroDisplay",
                                     &Script_C_Macro_SetMacroDisplay);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Macro::Display
