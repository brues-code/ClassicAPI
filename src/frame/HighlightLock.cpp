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

// The highlight lock on every Frame: SetHighlightLocked(locked),
// IsHighlightLocked(), LockHighlight() and UnlockHighlight(), as the 3.80.2
// client has all four on Frame (0x140A8C7F0, 0x140A890D0, 0x140A89EA0,
// 0x140A8F370; docs/notes/gamepad-input-region-api-3802.md §5). 1.12 has
// only the Button pair, but every frame has the lock: the virtual
// SetHighlightLocked (VTBL_FRAME_SET_HIGHLIGHT_LOCKED) the Button pair
// calls, which holds the HIGHLIGHT layer on while locked; a button's own
// override also refreshes its label. So the Frame methods call that slot,
// and IsHighlightLocked reads the lock it stores.
//
// SetHighlightLocked needs its argument (any value but nil, read as
// lua_toboolean reads it).

#include "Game.h"
#include "Offsets.h"
#include "lua/Arg.h"

namespace Frame::HighlightLock {

namespace {

using SetLocked_t = void(__thiscall *)(void *frame, int locked);

void SetLocked(void *frame, bool locked) {
    const void *vtable = Game::Read<const void *>(frame, 0);
    Game::Read<SetLocked_t>(vtable, Offsets::VTBL_FRAME_SET_HIGHLIGHT_LOCKED)(frame, locked ? 1 : 0);
}

int __fastcall Script_SetHighlightLocked(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    bool locked = false;
    if (!Game::Lua::Arg::Bool(L, 2, "Usage: self:SetHighlightLocked(locked)", locked))
        return 0;
    SetLocked(frame, locked);
    return 0;
}

int __fastcall Script_IsHighlightLocked(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, Game::Read<int>(frame, Offsets::OFF_FRAME_HIGHLIGHT_LOCKED) != 0);
    return 1;
}

int __fastcall Script_LockHighlight(void *L) {
    if (void *frame = Game::Lua::ResolveFrame(L))
        SetLocked(frame, true);
    return 0;
}

int __fastcall Script_UnlockHighlight(void *L) {
    if (void *frame = Game::Lua::ResolveFrame(L))
        SetLocked(frame, false);
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetHighlightLocked", &Script_SetHighlightLocked},
    {"IsHighlightLocked", &Script_IsHighlightLocked},
    {"LockHighlight", &Script_LockHighlight},
    {"UnlockHighlight", &Script_UnlockHighlight},
};

const Game::Doc::Field kLockedArg[] = {Game::Doc::Req("locked", "bool")};
const Game::Doc::Field kLockedRet[] = {Game::Doc::Req("locked", "bool")};
const Game::Doc::Function kSetHighlightLocked{
    "Sets whether the frame's HIGHLIGHT layer stays shown whether or not the cursor is over it.", kLockedArg, {}};
const Game::Doc::Function kIsHighlightLocked{"Returns whether the frame's highlight is locked on.", {}, kLockedRet};
const Game::Doc::Function kLockHighlight{"Locks the frame's highlight on.", {}, {}};
const Game::Doc::Function kUnlockHighlight{
    "Unlocks the frame's highlight; it stays shown only while the cursor is over the frame.", {}, {}};

const Game::Doc::Method g_methodDocs[] = {
    {"SetHighlightLocked", &kSetHighlightLocked},
    {"IsHighlightLocked", &kIsHighlightLocked},
    {"LockHighlight", &kLockHighlight},
    {"UnlockHighlight", &kUnlockHighlight},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_methods,
                                    g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::HighlightLock
