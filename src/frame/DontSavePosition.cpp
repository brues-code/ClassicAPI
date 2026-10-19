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

// `frame:SetDontSavePosition([dontSave])` and `frame:GetDontSavePosition()`.
//
// The client saves the position and size of every named, user-placed,
// movable or resizable frame to layout-cache.txt, and puts the frame back
// there at the next login. An addon that saves its own frame's position has
// to fight that copy. 4.3.4 lets it opt out: SetDontSavePosition sets frame
// flag 0x80000, and the layout-cache writer (PlacedFrameCallback) skips a
// frame that has it. Nothing else reads the flag; the reader still applies
// a record already in the file.
//
// 1.12's writer calls FUN_LAYOUT_CACHE_SAVE_FRAME on each frame, and that
// function makes the same name / user-placed / movable test. The hook adds
// 4.3.4's flag test in front of it.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Frame::DontSavePosition {

namespace {

using SaveFrame_t = void(__fastcall *)(void *frame, void *file);
using SetFrameFlag_t = void(__fastcall *)(void *frame, void *edx, uint32_t mask, int on);
using LooseBool_t = int(__fastcall *)(void *L, int idx, int defaultValue);

SaveFrame_t g_origSaveFrame = nullptr;

bool DontSave(void *frame) {
    return (Game::Read<uint32_t>(frame, Offsets::OFF_FRAME_FLAGS) & Offsets::FRAME_FLAG_DONT_SAVE_POSITION) != 0;
}

void __fastcall SaveFrame_h(void *frame, void *file) {
    if (DontSave(frame))
        return;
    g_origSaveFrame(frame, file);
}

const Game::HookAutoRegister _hook{Offsets::FUN_LAYOUT_CACHE_SAVE_FRAME, reinterpret_cast<void *>(&SaveFrame_h),
                                   reinterpret_cast<void **>(&g_origSaveFrame)};

// 4.3.4 reads the flag with StringToBOOL(L, 2, 1): no argument sets it.
int __fastcall Script_SetDontSavePosition(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    const int on = reinterpret_cast<LooseBool_t>(Offsets::FUN_LUA_TO_BOOLEAN_LOOSE)(L, 2, 1);
    reinterpret_cast<SetFrameFlag_t>(Offsets::FUN_FRAME_SET_FRAME_FLAG)(
        frame, nullptr, Offsets::FRAME_FLAG_DONT_SAVE_POSITION, on != 0);
    return 0;
}

// 4.3.4 pushes 1 or nil.
int __fastcall Script_GetDontSavePosition(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    if (DontSave(frame))
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetDontSavePosition", &Script_SetDontSavePosition},
    {"GetDontSavePosition", &Script_GetDontSavePosition},
};

const Game::Doc::Field kSetArgs[] = {
    Game::Doc::Opt("dontSave", "boolean", "true",
                   "When true, the client does not save the frame's position and size to layout-cache.txt."),
};
const Game::Doc::Field kGetRets[] = {
    Game::Doc::Opt("dontSave", "number", nullptr, "1 when the frame's position is not saved, else nil."),
};
const Game::Doc::Function kSetDontSavePosition{
    "Sets whether the client leaves the frame out of the saved frame layout.", kSetArgs, {}};
const Game::Doc::Function kGetDontSavePosition{
    "Returns whether the client leaves the frame out of the saved frame layout.", {}, kGetRets};

const Game::Doc::Method g_methodDocs[] = {
    {"SetDontSavePosition", &kSetDontSavePosition},
    {"GetDontSavePosition", &kGetDontSavePosition},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::DontSavePosition
