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

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

// Every script slot a frame holds — 4.3.4's per-class GetScriptTime walks
// its own slot members; 1.12 exposes a frame's slots only by name, through
// the script-name resolver at vtable slot 0xC (FUN_FRAME_SCRIPT_RESOLVER and
// each type's extension of it). `ForEach` asks it for every name the client's
// frame types define (benilla-re ui/xmltypes "Handler names by type", plus
// the base object's OnEvent and GameTooltip's three).
//
// Scripts ClassicAPI adds keep their slots outside the frame, and their
// resolver co-hooks CREATE a slot when asked for a name — right for
// SetScript, wrong for a read that visits every frame. So each such store
// registers an enumerator over the slots it already holds:
//   static const Frame::ScriptSlots::AutoRegister _slots{&ForEachSlot};

namespace Frame::ScriptSlots {

// `slot` is the {handler ref, exec context} pair; the handler may be 0.
using Visit = void (*)(const uint32_t *slot, void *ctx);
using Enumerator = void (*)(const void *frame, Visit visit, void *ctx);

struct AutoRegister {
    explicit AutoRegister(Enumerator fn);
    Enumerator fn;
    AutoRegister *next;
};

void ForEach(void *frame, Visit visit, void *ctx);

// Releases the handler ref in `slot` when one is set (FrameScriptReleaseScript,
// FUN_SCRIPT_RELEASE, `__stdcall(slot)`), as a frame's destructor does. The
// slot keeps its value; a caller that reuses it writes the new one.
inline void Release(uint32_t *slot) {
    using Fn = void(__stdcall *)(uint32_t *slot);
    reinterpret_cast<Fn>(Offsets::FUN_SCRIPT_RELEASE)(slot);
}

// Fires the handler in `slot` (a {handler ref, exec context} pair, or null)
// the way the engine fires its input scripts: through its variadic front
// FUN_FRAME_RUN_SCRIPT_VARIADIC (`__cdecl(frame, slot, fmt, ...)`), so the
// exec context comes from slot[1], the hooked runner sees it, and modern
// script args apply. `fmt` is one of the engine's own literals
// (VAR_SCRIPT_FMT_*) and `args` match it: a `%s` takes a `const char *`, a
// `%f` a double. Nothing runs for an empty slot. The Lua top is restored
// after, so a fire from inside running C code leaves its stack as it was.
template <class... Args> void Fire(void *frame, uint32_t *slot, uintptr_t fmt, Args... args) {
    using Forward_t = void(__cdecl *)(void *frame, uint32_t *slot, const char *fmt, ...);
    if (slot == nullptr || slot[0] == 0)
        return;
    void *L = Game::Lua::State();
    const int top = L != nullptr ? Game::Lua::GetTop(L) : 0;
    reinterpret_cast<Forward_t>(Offsets::FUN_FRAME_RUN_SCRIPT_VARIADIC)(
        frame, slot, reinterpret_cast<const char *>(fmt), args...);
    if (L != nullptr)
        Game::Lua::SetTop(L, top);
}

} // namespace Frame::ScriptSlots
