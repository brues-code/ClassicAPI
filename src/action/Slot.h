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

#pragma once

#include <cstdint>

// Action-slot ↔ macro decoding shared by every module that has to answer
// "which macro sits in this action slot" (`GetActionInfo`, the
// `#showtooltip` display overrides). The action table stores macroIDs under
// the ambiguous `0x40000000` tag; the macro-slot map says which of those ids
// are macros (the rest are bag items). Pure C — no Lua stack.

namespace Action::Slot {

// 1-based macro slot holding `macroID` — the same linear scan of
// `VAR_MACRO_SLOT_MAP` the engine's `FUN_MACRO_ID_TO_SLOT` does — or 0 when
// no macro has that id (0 is never a macroID).
int MacroSlotForID(uint32_t macroID);

// The macroID in action slot `slot0` (0-based, `< ACTION_TABLE_MAX_SLOTS`),
// or 0 when the slot is empty, isn't `0x40000000`-tagged, or holds a bag
// item rather than a macro.
uint32_t MacroIDForSlot(int slot0);

} // namespace Action::Slot
