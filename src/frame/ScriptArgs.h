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

namespace Frame::ScriptArgs {

// Runs a frame-script handler with arbitrary Lua values as its arguments —
// the engine's runners can only pass %d/%u/%f/%s. `nargs` values at absolute
// stack index `firstArgIdx` become `arg1..argN` (globals, saved and restored
// around the call) and, while modern script args are on, the positional
// `(self, ...)`. Sets `this`, runs under the engine's script error handler,
// and leaves the stack as it found it. For scripts the engine itself never
// fires, such as OnAttributeChanged (4.3.4: RunScript with 2 stack args).
void RunWithValues(void *L, int handlerRef, void *frame, int firstArgIdx, int nargs);

// RunWithValues for a script slot {handler, context}, inside the slot's
// execution context: VAR_FRAMESCRIPT_EXEC_CONTEXT is set from slot[1] for the
// call and restored after, with the depth count kept, as the engine's own
// fire wrapper 0x00702690 does for OnTextChanged, OnTextSet and the other
// scripts it runs. Does nothing for an empty slot.
void RunSlotWithValues(void *L, const uint32_t *slot, void *frame, int firstArgIdx, int nargs);

} // namespace Frame::ScriptArgs
