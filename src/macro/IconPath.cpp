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
// binary finds. `GetMacroInfo` formats its own path and is handled by
// `Macro::Info`.
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

} // namespace

} // namespace Macro::IconPath
