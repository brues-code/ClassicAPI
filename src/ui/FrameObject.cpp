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

#include "ui/FrameObject.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace UI::FrameObject {

namespace {

using LuaRawGetI_t = void(__fastcall *)(void *L, int idx, int n);
using ScriptRegister_t = void(__fastcall *)(void *this_, void *edx_unused,
                                            const char *name);

} // namespace

// Delegate wrapper construction to the engine's own
// `FrameScript_Object::ScriptRegister`. First call for a never-seen frame
// builds a `{[0] = lightuserdata}` table with the framescript metatable,
// `luaL_ref`s it into the registry, and writes the refkey to `frame +
// 0x08` (`OFF_COBJECT_LUA_REF`). Subsequent calls find a
// populated refkey and just rawgeti.
//
// Why delegate instead of building our own wrapper: single source of
// truth. Every code path that pushes this frame — ours, addons'
// `CreateFrame(..., parent)` extracting the frame, the engine's own
// pushers — funnels through `rawgeti(REGISTRY, frame + 0x08)` and gets the
// same Lua table, so addon fields set on the wrapper don't get orphaned
// onto a divergent copy. See the long note in `nameplate/Info.cpp`'s
// original implementation for the pfUI-divergence story that motivated
// this.
//
// `ScriptRegister` unconditionally bumps the CObject refcount at `+0x04`,
// which pins the frame against GC. That's benign for the pooled/permanent
// frames we push here (nameplates, chat bubbles) — we call it at most
// once per frame pointer (gated by `refKey <= 0`), so the refcount tops
// out at one increment per pool slot.
//
// Defensive fallback: if the registry slot exists but isn't a table any
// more (freed elsewhere), re-register to rebuild a fresh wrapper. This
// shouldn't happen in practice — engines only unref on frame destruction.
void Push(void *L, void *frame) {
    auto rawgeti = reinterpret_cast<LuaRawGetI_t>(
        Offsets::FUN_FRAMESCRIPT_PUSH_OBJECT);
    auto scriptRegister = reinterpret_cast<ScriptRegister_t>(
        static_cast<uintptr_t>(Offsets::FUN_FRAMESCRIPT_OBJECT_SCRIPT_REGISTER));

    auto *cobj = static_cast<uint8_t *>(frame);
    int refKey = Game::Read<int>(cobj, Offsets::OFF_COBJECT_LUA_REF);
    if (refKey <= 0) {
        scriptRegister(frame, nullptr, nullptr);
        refKey = Game::Read<int>(cobj, Offsets::OFF_COBJECT_LUA_REF);
    }

    rawgeti(L, Game::Lua::REGISTRY_INDEX, refKey);
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE)
        return;
    Game::Lua::SetTop(L, -2);

    // Slot got freed somewhere — re-register to rebuild a fresh wrapper
    // and slot. Zero the refcount so `ScriptRegister` re-enters its build
    // branch.
    Game::Ref<int>(cobj, Offsets::OFF_COBJECT_LUA_REFCOUNT) = 0;
    scriptRegister(frame, nullptr, nullptr);
    refKey = Game::Read<int>(cobj, Offsets::OFF_COBJECT_LUA_REF);
    rawgeti(L, Game::Lua::REGISTRY_INDEX, refKey);
}

} // namespace UI::FrameObject
