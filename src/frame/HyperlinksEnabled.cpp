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

// `SetHyperlinksEnabled(enabled)` / `GetHyperlinksEnabled()` on
// ScrollingMessageFrame and SimpleHTML, the two types 3.3.5 has them on.
//
// Both types run OnHyperlinkEnter, OnHyperlinkLeave and OnHyperlinkClick
// when the mouse meets a link in their text. Later clients can switch that
// off per frame: 3.3.5's chat frames do it for a chat window set to not
// take the mouse. Vanilla always runs them. Zygor Guides Viewer 2.0 turns
// hyperlinks off on the step lines it fills with its Gold Guide's item
// lists, and back on when those lines show guide steps again.
//
// The flag is kept per frame, default on. The hook sits on the three
// functions the engine fires those scripts through, shared by both types,
// and skips the script while the frame's flag is off.

#include "Game.h"
#include "Offsets.h"

#include <unordered_set>

namespace Frame::HyperlinksEnabled {

namespace {

// Frames with hyperlinks switched off. Every frame is rebuilt on /reload, so
// the set is cleared then.
std::unordered_set<void *> g_disabled;

using EnterLeave_t = void(__thiscall *)(void *frame, const char *link, const char *text);
using Click_t = void(__thiscall *)(void *frame, const char *link, const char *text, int button);
EnterLeave_t g_origEnter = nullptr;
EnterLeave_t g_origLeave = nullptr;
Click_t g_origClick = nullptr;

bool Disabled(void *frame) { return !g_disabled.empty() && g_disabled.count(frame) != 0; }

void __fastcall Enter_h(void *frame, void * /*edx*/, const char *link, const char *text) {
    if (!Disabled(frame))
        g_origEnter(frame, link, text);
}
void __fastcall Leave_h(void *frame, void * /*edx*/, const char *link, const char *text) {
    if (!Disabled(frame))
        g_origLeave(frame, link, text);
}
void __fastcall Click_h(void *frame, void * /*edx*/, const char *link, const char *text, int button) {
    if (!Disabled(frame))
        g_origClick(frame, link, text, button);
}

const Game::HookAutoRegister _enterHook{Offsets::FUN_HYPERLINK_ENTER, reinterpret_cast<void *>(&Enter_h),
                                    reinterpret_cast<void **>(&g_origEnter)};
const Game::HookAutoRegister _leaveHook{Offsets::FUN_HYPERLINK_LEAVE, reinterpret_cast<void *>(&Leave_h),
                                    reinterpret_cast<void **>(&g_origLeave)};
const Game::HookAutoRegister _clickHook{Offsets::FUN_HYPERLINK_CLICK, reinterpret_cast<void *>(&Click_h),
                                    reinterpret_cast<void **>(&g_origClick)};

int __fastcall Script_SetHyperlinksEnabled(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    if (Game::Lua::ToBoolean(L, 2))
        g_disabled.erase(frame);
    else
        g_disabled.insert(frame);
    return 0;
}

int __fastcall Script_GetHyperlinksEnabled(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, g_disabled.count(frame) == 0);
    return 1;
}

void PrepareForReload() { g_disabled.clear(); }

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetHyperlinksEnabled", &Script_SetHyperlinksEnabled},
    {"GetHyperlinksEnabled", &Script_GetHyperlinksEnabled},
};

void RegisterLuaFunctions() {
    for (const uintptr_t registry : {static_cast<uintptr_t>(Offsets::VAR_SCROLLINGMESSAGEFRAME_METHOD_REGISTRY),
                                     static_cast<uintptr_t>(Offsets::VAR_SIMPLEHTML_METHOD_REGISTRY)}) {
        Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(registry), g_methods,
                                        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
    }
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::ReloadAutoRegister _reload{&PrepareForReload};

} // namespace

} // namespace Frame::HyperlinksEnabled
