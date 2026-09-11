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

// The `?` icon rule at its engine home: a co-hook on the macro icon getter
// `FUN_MACRO_ICON_PATH` (`macroID → "Interface\Icons\<icon>"`). A macro whose
// icon is the question mark and whose `#showtooltip` / `#show` resolved to a
// spell or item reports that spell's or item's icon instead.
//
// This is where 3.3.5 keeps the rule too (its getter `FUN_00566ac0`), and for
// the same reason: the getter has every consumer behind it. In 1.12 those are
// the per-slot texture resolver (`FUN_ACTION_SLOT_TEXTURE`, behind
// `GetActionTexture`) and the macro pickup `FUN_MACRO_PICKUP`, which paints
// the cursor while a macro is dragged — the two call sites a scan of the
// binary finds.
//
// `GetMacroInfo` formats its own path and is left alone on purpose. It is the
// read side of the icon `C_Macro.EditMacro` writes, and the Macro UI seeds its
// icon selector from that return (`MacroFrame_Update` ->
// `MacroPopupFrame.selectedIconTexture`), so answering with a resolved icon
// there would tell the player they had picked an icon they never picked. Any
// caller that wants the icon a macro currently SHOWS asks for it instead,
// through `C_Macro.GetMacroIcon` below — which is this hook's own answer, so
// the rule stays in one place. `Util/MacroIconSelection.lua` uses it to paint
// the Macro UI's list buttons.
//
// The spell's active icon (toggle / stance up) is chosen through the engine's
// own per-slot test `FUN_ACTION_SPELL_ICON_ACTIVE`, on the first action slot
// holding the macro — that test reads the slot's spell, which for a macro
// slot is the very spell `Macro::ShowTooltip` cached. A macro on no bar (the
// Macro UI drag) uses the base icon.

#include "Game.h"
#include "Offsets.h"
#include "action/Slot.h"
#include "macro/ShowTooltip.h"

#include <cstdint>

namespace Macro::IconPath {

namespace {

using MacroIconPath_t = void(__fastcall *)(uint32_t macroID, char *out, uint32_t size);
using IconActive_t = int(__fastcall *)(uint32_t slot0);

MacroIconPath_t MacroIconPath_o = nullptr;

bool SpellIconActive(uint32_t macroID) {
    for (int slot0 = 0; slot0 < Offsets::ACTION_TABLE_MAX_SLOTS; ++slot0) {
        if (Action::Slot::MacroIDForSlot(slot0) != macroID)
            continue;
        return reinterpret_cast<IconActive_t>(Offsets::FUN_ACTION_SPELL_ICON_ACTIVE)(
                   static_cast<uint32_t>(slot0)) != 0;
    }
    return false;
}

void __fastcall MacroIconPath_h(uint32_t macroID, char *out, uint32_t size) {
    MacroIconPath_o(macroID, out, size);
    if (out == nullptr || size == 0 || out[0] == '\0')
        return;
    if (!Macro::ShowTooltip::HasQuestionMarkIcon(macroID))
        return;
    // Passive: one of this getter's two callers is `FUN_MACRO_PICKUP`, inside
    // the UseAction core, so the hook can run from a click or a drag. The
    // catch-up `Lookup` performs would evaluate conditions through
    // `SecureCmdOptionParse` and repaint — running Lua and dispatching
    // `ACTIONBAR_SLOT_CHANGED` to addon handlers from underneath the click.
    // The world tick does that work instead, and repaints the button when it
    // changes anything.
    Macro::ShowTooltip::Info info;
    if (!Macro::ShowTooltip::LookupPassive(macroID, &info))
        return;
    const bool active = info.target == Macro::ShowTooltip::Target::Spell && SpellIconActive(macroID);
    char resolved[0x104];
    if (Macro::ShowTooltip::ResolvedIconPath(info, active, resolved, sizeof(resolved))) {
        // Same bounded copy the getter itself performs into the caller's buffer.
        uint32_t i = 0;
        for (; i + 1 < size && resolved[i] != '\0'; ++i)
            out[i] = resolved[i];
        out[i] = '\0';
    }
}

const Game::HookAutoRegister _hookreg{
    Offsets::FUN_MACRO_ICON_PATH,
    reinterpret_cast<void *>(&MacroIconPath_h),
    reinterpret_cast<void **>(&MacroIconPath_o)};

using MacroSlotToEntry_t = const uint8_t *(__fastcall *)(unsigned slot0Based);

// `C_Macro.GetMacroIcon(macroSlot)` — the icon the macro currently shows: the
// spell's or item's when a `#showtooltip` / `#show` resolved and the macro's
// own icon is the question mark, else the macro's own. Nothing for an empty
// slot.
//
// Reads it through the getter this module hooks, so the answer is the same one
// the action button and the drag cursor get, active-icon choice included.
int __fastcall Script_GetMacroIcon(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Macro.GetMacroIcon(macroSlot)");
        return 0;
    }
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (slot < 1 || slot > Offsets::MACRO_SLOT_MAP_COUNT)
        return 0;
    const uint8_t *entry = reinterpret_cast<MacroSlotToEntry_t>(
        Offsets::FUN_MACRO_SLOT_TO_ENTRY)(static_cast<unsigned>(slot - 1));
    if (entry == nullptr)
        return 0;

    char path[0x104]; // the engine's own texture buffer size
    path[0] = '\0';
    // The hooked address, not the trampoline: the `?` rule lives in the hook.
    reinterpret_cast<MacroIconPath_t>(Offsets::FUN_MACRO_ICON_PATH)(
        *reinterpret_cast<const uint32_t *>(entry), path, sizeof(path));
    if (path[0] == '\0')
        return 0;
    Game::Lua::PushString(L, path);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Macro", "GetMacroIcon", &Script_GetMacroIcon);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Macro::IconPath
