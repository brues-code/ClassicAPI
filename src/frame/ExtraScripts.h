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
#include <unordered_map>

// Script types the 1.12 engine does not know, made SetScript / GetScript /
// HookScript-able (and loadable from XML <Scripts>) like its own.
//
// Every frame type's script-name resolver calls the base frame resolver
// FUN_FRAME_SCRIPT_RESOLVER first; the engine stores a handler in, and fires
// it from, the 8-byte {handler, context} slot the resolver returns. A frame
// has no spare slot, so for a name the engine does not know this module's
// co-hook on that resolver hands out an external per-frame cell. MinHook
// allows one hook per address, so this module owns it and every extra script
// declares itself at file scope:
//
//   const Frame::ExtraScripts::Script g_onFoo{"onfoo", Offsets::VAR_FRAME_LUA_TYPE_ID};
//
// and fires with Frame::ScriptArgs::RunSlotWithValues(L, g_onFoo.Slot(frame), ...).
// Cells are dropped on /reload, when every handler ref dies with the Lua state.
namespace Frame::ExtraScripts {

class Script {
  public:
    // `nameLower`: the script name in lower case (names match case-insensitively,
    // as the engine's do). `typeIdVar`: the VAR_*_LUA_TYPE_ID of the class that has
    // the script, VAR_FRAME_LUA_TYPE_ID for every frame.
    Script(const char *nameLower, uintptr_t typeIdVar);

    // The frame's slot, or nullptr when no handler was ever set on it.
    uint32_t *Slot(const void *frame) const;

    const char *nameLower;
    uintptr_t typeIdVar;
    Script *next;

    struct Cell {
        uint32_t v[2]; // {handler ref, exec context}
    };
    // Node-based, so a cell's address stays valid while the engine holds it.
    mutable std::unordered_map<const void *, Cell> cells;
};

} // namespace Frame::ExtraScripts
