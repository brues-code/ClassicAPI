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

// `frame:SetPropagateKeyboardInput([propagate])` and
// `frame:GetPropagateKeyboardInput()`.
//
// The top hands a key down to the frames in its key lists, top stratum first,
// and stops at the first frame whose key-down slot answers nonzero; that frame
// then owns the key. A frame with an OnKeyDown (or OnKeyUp) script always
// answers 1, so a frame that only watches keys swallows every one of them,
// movement and bindings included. The same goes for OnChar and the character
// walk.
//
// 4.3.4 lets the frame pass the key on: SetPropagateKeyboardInput sets frame
// flag 0x200000, and the slots end with `return !(flags & 0x200000) ||
// IsProtected()` - the script still runs, and the slot answers "not taken",
// so the walk goes on to the frames below and the world frame's bindings. The
// frames that do it are the base CSimpleFrame's key-down, key-up and char
// slots and CSimpleEditBox's key-down and key-up (its char slot does not).
// Nothing is protected here, so the flag alone decides.
//
// 1.12's slots are the same shape, so each hook runs the engine's slot and
// clears its "taken" answer for a frame with the flag. Only key-down and char
// are hooked: the top's key-up handler (0x00765FD0) discards the slot's
// answer, so 4.3.4's key-up return has nothing to change here. As in 4.3.4,
// a frame that passes a key down on is not recorded as the key's owner, so
// its key up goes to the frame that took the key.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Frame::KeyboardPropagation {

namespace {

using KeySlot_t = int(__fastcall *)(void *frame, void *edx, void *event);
using SetFrameFlag_t = void(__fastcall *)(void *frame, void *edx, uint32_t mask, int on);

KeySlot_t g_origFrameOnChar = nullptr;
KeySlot_t g_origFrameOnKeyDown = nullptr;
KeySlot_t g_origEditBoxOnKeyDown = nullptr;

bool Propagates(void *frame) {
    return (Game::Read<uint32_t>(frame, Offsets::OFF_FRAME_FLAGS) & Offsets::FRAME_FLAG_PROPAGATE_KEYBOARD) != 0;
}

int __fastcall FrameOnChar_h(void *frame, void *edx, void *event) {
    const int taken = g_origFrameOnChar(frame, edx, event);
    return taken != 0 && Propagates(frame) ? 0 : taken;
}

int __fastcall FrameOnKeyDown_h(void *frame, void *edx, void *event) {
    const int taken = g_origFrameOnKeyDown(frame, edx, event);
    return taken != 0 && Propagates(frame) ? 0 : taken;
}

int __fastcall EditBoxOnKeyDown_h(void *frame, void *edx, void *event) {
    const int taken = g_origEditBoxOnKeyDown(frame, edx, event);
    return taken != 0 && Propagates(frame) ? 0 : taken;
}

const Game::HookAutoRegister _frameOnCharHook{Offsets::FUN_FRAME_ON_CHAR, reinterpret_cast<void *>(&FrameOnChar_h),
                                              reinterpret_cast<void **>(&g_origFrameOnChar)};
const Game::HookAutoRegister _frameOnKeyDownHook{Offsets::FUN_FRAME_ON_KEY_DOWN,
                                                 reinterpret_cast<void *>(&FrameOnKeyDown_h),
                                                 reinterpret_cast<void **>(&g_origFrameOnKeyDown)};
const Game::HookAutoRegister _editBoxOnKeyDownHook{Offsets::FUN_EDITBOX_ON_KEY_DOWN,
                                                   reinterpret_cast<void *>(&EditBoxOnKeyDown_h),
                                                   reinterpret_cast<void **>(&g_origEditBoxOnKeyDown)};

// 4.3.4 reads the flag with StringToBOOL(L, 2, 0): no argument clears it.
int __fastcall Script_SetPropagateKeyboardInput(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    const bool on = Game::Lua::ToFlag(L, 2);
    reinterpret_cast<SetFrameFlag_t>(Offsets::FUN_FRAME_SET_FRAME_FLAG)(
        frame, nullptr, Offsets::FRAME_FLAG_PROPAGATE_KEYBOARD, on);
    return 0;
}

int __fastcall Script_GetPropagateKeyboardInput(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, Propagates(frame));
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetPropagateKeyboardInput", &Script_SetPropagateKeyboardInput},
    {"GetPropagateKeyboardInput", &Script_GetPropagateKeyboardInput},
};

const Game::Doc::Field kSetArgs[] = {
    Game::Doc::Opt("propagate", "boolean", "false",
                   "When true, the frame's OnKeyDown and OnChar scripts run and the key goes on to the frames "
                   "below and to the key bindings."),
};
const Game::Doc::Field kGetRets[] = {
    Game::Doc::Req("propagate", "boolean"),
};
const Game::Doc::Function kSetPropagateKeyboardInput{
    "Sets whether the frame passes the keys it gets on to the frames below it.", kSetArgs, {}};
const Game::Doc::Function kGetPropagateKeyboardInput{
    "Returns whether the frame passes the keys it gets on to the frames below it.", {}, kGetRets};

const Game::Doc::Method g_methodDocs[] = {
    {"SetPropagateKeyboardInput", &kSetPropagateKeyboardInput},
    {"GetPropagateKeyboardInput", &kGetPropagateKeyboardInput},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::KeyboardPropagation
