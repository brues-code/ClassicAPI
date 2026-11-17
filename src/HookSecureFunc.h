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

// The one hook wrapper `hooksecurefunc` and `Frame:HookScript` both install —
// 4.3.4 uses its FrameScript_HookWrapper for both, so a hooked function and a
// hooked script behave the same way.
namespace HookSecureFunc {

// The wrapper closure body. Upvalue 1 = the original, upvalue 2 = the hook.
// Runs the original with the wrapper's args (its errors propagate), then the
// hook with the same args under the engine's script error handler, and
// returns the original's results. Exposed so readers can recognize the
// closure by its C function.
int __fastcall Wrapper(void *L);

// Pops the hook (top) and the original (below it) and pushes the wrapper
// closure over them. The original and the wrapper carry `origWord`, the
// hook `hookWord` (context words, 1.12's taint); the current context is
// left as it was.
void PushWrapper(void *L, uint32_t origWord, uint32_t hookWord);

// HookScript's store once GetScript has run: the stack is (self, type,
// hook, old), `old` the script's handler or nil. `hookWord` is the context
// that was current before GetScript (GetScript changes it), `scriptWord`
// the context the script's slot holds. `setScript` is the object's
// SetScript, which stores stack 3 with the current context. Stores the hook,
// or the wrapper over (old, hook) under the script's own context, and
// returns setScript's result.
int SetHookedScript(void *L, uint32_t hookWord, uint32_t scriptWord,
                    int(__fastcall *setScript)(void *L));

} // namespace HookSecureFunc
