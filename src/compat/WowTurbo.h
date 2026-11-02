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

// wow_turbo (github.com/athei/wow-mods) detours ~380 engine functions to Rust
// reimplementations and yields any function another mod has already hooked:
// its installer refuses a prologue that fails its signature. It installs from
// DllMain, but ClassicAPI installs from `Load`, which VanillaFixes calls after
// every DllMain, so wow_turbo never sees our hooks and always installs first.
// MinHook then copies wow_turbo's jump into our trampoline, and our hook calls
// the Rust body instead of the engine's. That loses everything that lives in
// the stock body: Frame::ScriptArgs' pcall redirects in both script runners,
// and InlineTexture / IndentedWordWrap in the text builder, whose Rust copy
// calls its own emitter.
//
// `Reclaim` gives the stock prologue back before we hook, the state wow_turbo
// itself produces when it loads after a mod that hooked first. wow_turbo keeps
// its original in place, logs "prologue overwritten ... by ClassicAPI.dll"
// and does not re-patch these entries.
//
// The DllMain core hooks need no reclaim: the only one wow_turbo also
// replaces is the integrity scan, which both make the same no-op.

namespace Compat::WowTurbo {

// Restores the engine's own first instruction at `function` when it is a
// `jmp` into wow_turbo.dll. No-op when wow_turbo is not loaded or the
// prologue is anything else.
void Reclaim(uintptr_t function);

} // namespace Compat::WowTurbo
