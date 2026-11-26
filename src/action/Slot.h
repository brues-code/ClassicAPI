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
// the `0x40000000` tag; the macro-slot map says which of those ids still
// name a macro (the rest are buttons whose macro is gone). Pure C — no Lua
// stack.

namespace Action::Slot {

// 1-based macro slot holding `macroID` — the same linear scan of
// `VAR_MACRO_SLOT_MAP` the engine's `FUN_MACRO_ID_TO_SLOT` does — or 0 when
// no macro has that id (0 is never a macroID).
int MacroSlotForID(uint32_t macroID);

// Whether `slot0` names an action slot: the engine's 0..119 or one of
// Action::Extended's 120..359.
bool Valid(int slot0);

// The action word in `slot0` for both ranges: VAR_ACTION_TABLE for the
// engine's slots, Action::Extended's store for ours; 0 for an empty or
// invalid slot.
uint32_t Word(int slot0);

// FUN_ACTION_SLOT_TO_SPELL for both ranges: the engine resolver for its slots,
// Action::Extended::SpellFor for ours. `*book` is written only when the
// resolver writes it (0 for a spell or an item, the macro's book for a live
// macro), so callers start it at the value they want for "no spell".
uint32_t SpellFor(int slot0, uint32_t *book);

// The macroID in action slot `slot0` (0-based, both ranges), or 0 when the
// slot is empty, isn't `0x40000000`-tagged, or its macro is gone.
uint32_t MacroIDForSlot(int slot0);

} // namespace Action::Slot
