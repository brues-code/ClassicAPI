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

// `useParentLevel` on XML frames, and `frame:SetUsingParentLevel(using)` /
// `frame:IsUsingParentLevel()`.
//
// A frame that uses its parent's level draws at the parent's level instead
// of one above it. 4.3.4 keeps this as frame flag 0x100000: setting it moves
// the frame (and its children) to the parent's level, and the level setter
// replaces any level it is given with the parent's while the flag is on. So
// SetParent's parent level + 1, a parent's level change passed down to its
// children, and SetFrameLevel all land on the parent's level. The XML
// attribute is read after the node loads, when the frame has its parent, and
// only when present and non-empty, so a node's value overrides its template's.
//
// 1.12's level setter (FUN_FRAME_SET_FRAME_LEVEL) is 4.3.4's without the
// flag test; the hook adds it in front. The setter's recursion into children
// calls its own entry, so it goes through the hook too.

#include "Game.h"
#include "Offsets.h"
#include "xml/FrameLoad.h"

#include <cstdint>

namespace Frame::UseParentLevel {

namespace {

using SetFrameLevel_t = void(__fastcall *)(void *frame, void *edx, int level, int children);
using SetFrameFlag_t = void(__fastcall *)(void *frame, void *edx, uint32_t mask, int on);

SetFrameLevel_t g_origSetFrameLevel = nullptr;

bool Using(void *frame) {
    return (Game::Read<uint32_t>(frame, Offsets::OFF_FRAME_FLAGS) & Offsets::FRAME_FLAG_USE_PARENT_LEVEL) != 0;
}

void __fastcall SetFrameLevel_h(void *frame, void *edx, int level, int children) {
    void *parent = Game::Read<void *>(frame, Offsets::OFF_REGION_PARENT);
    if (parent != nullptr && Using(frame))
        level = Game::Read<int>(parent, Offsets::OFF_FRAME_LEVEL);
    g_origSetFrameLevel(frame, edx, level, children);
}

const Game::HookAutoRegister _hook{Offsets::FUN_FRAME_SET_FRAME_LEVEL, reinterpret_cast<void *>(&SetFrameLevel_h),
                                   reinterpret_cast<void **>(&g_origSetFrameLevel)};

// 4.3.4's CSimpleFrame::SetUseParentLevel.
void Set(void *frame, bool on) {
    reinterpret_cast<SetFrameFlag_t>(Offsets::FUN_FRAME_SET_FRAME_FLAG)(
        frame, nullptr, Offsets::FRAME_FLAG_USE_PARENT_LEVEL, on);
    void *parent = Game::Read<void *>(frame, Offsets::OFF_REGION_PARENT);
    if (on && parent != nullptr)
        SetFrameLevel_h(frame, nullptr, Game::Read<int>(parent, Offsets::OFF_FRAME_LEVEL), 1);
}

void OnFrameNode(void *object, const void *node, void * /*status*/) {
    const char *value = Xml::Attr(node, "useParentLevel");
    if (value == nullptr || *value == '\0')
        return;
    Set(object, Game::StringToFlag(value));
}

const Xml::FrameLoad::AutoSubscribe _xmlSub{&OnFrameNode};

int __fastcall Script_SetUsingParentLevel(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    Set(frame, Game::Lua::ToFlag(L, 2));
    return 0;
}

int __fastcall Script_IsUsingParentLevel(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, Using(frame));
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetUsingParentLevel", &Script_SetUsingParentLevel},
    {"IsUsingParentLevel", &Script_IsUsingParentLevel},
};

const Game::Doc::Field kSetArgs[] = {
    Game::Doc::Req("usingParentLevel", "boolean",
                   "When true, the frame draws at its parent's frame level instead of one above it."),
};
const Game::Doc::Field kIsRets[] = {
    Game::Doc::Req("usingParentLevel", "boolean", "True when the frame uses its parent's frame level."),
};
const Game::Doc::Function kSetUsingParentLevel{"Sets whether the frame uses its parent's frame level.", kSetArgs,
                                               {}};
const Game::Doc::Function kIsUsingParentLevel{"Returns whether the frame uses its parent's frame level.", {},
                                              kIsRets};

const Game::Doc::Method g_methodDocs[] = {
    {"SetUsingParentLevel", &kSetUsingParentLevel},
    {"IsUsingParentLevel", &kIsUsingParentLevel},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::UseParentLevel
