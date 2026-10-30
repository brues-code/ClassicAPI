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

// `smf:AddOnDisplayRefreshedCallback(callback)`, retail's: callback(frame)
// runs each time the frame refreshes what it shows.
//
// Retail's mixin (Blizzard_SharedXML/ScrollingMessageFrame.lua) marks its
// display dirty on each change - a message, a scroll, a resize, a fade reset -
// and refreshes in its OnPostUpdate. So the callbacks run once per update of
// a shown frame that changed, in the order they were added, each under the
// error handler (secureexecuterange). The mark is cleared after them, so a
// change a callback makes (a scroll bar setting the offset) does not run them
// again.
//
// 1.12 refreshes what the frame shows in its line layout
// (ScrollingMessageFrame::Layout), which marks the frame. A message added
// while the frame is scrolled up changes no line and runs no layout, but it
// moves the scroll offset and range, so AddMessage marks the frame too, as
// ScrollByAmount does for retail's fade reset. The callbacks run after the
// frame's own update (FUN_SCROLLINGMESSAGEFRAME_ON_UPDATE: OnUpdate, then the
// fader), which the engine's update pass calls once per frame for each shown
// frame - where retail's OnPostUpdate runs.

#include "scrollingmessageframe/DisplayRefreshed.h"

#include "Game.h"
#include "Offsets.h"
#include "scrollingmessageframe/Layout.h"

#include <unordered_map>

namespace ScrollingMessageFrame::DisplayRefreshed {

namespace {

// Frames with callbacks, and whether each has a refresh pending. Every frame
// is rebuilt on /reload, so the map is cleared then.
std::unordered_map<void *, bool> g_frames;

// The frame-table → callback-list table, in the registry of the Lua state
// that made it. LUA_NOREF until first use in each state.
constexpr int kNoRef = -2;
int g_callbacksRef = kNoRef;

// Runs the callbacks of the frame, each as callback(frame) under the
// engine's script error handler.
void RunCallbacks(void *smf) {
    using namespace Game::Lua;
    void *L = State();
    if (L == nullptr || g_callbacksRef == kNoRef)
        return;
    const int top = GetTop(L);
    PushObject(L, smf);
    const int frame = GetTop(L);
    RawGetI(L, REGISTRY_INDEX, g_callbacksRef);
    PushValue(L, frame);
    RawGet(L, -2);
    const int callbacks = GetTop(L);
    if (Type(L, callbacks) == TYPE_TABLE) {
        const int errRef = Game::Read<int>(static_cast<uintptr_t>(Offsets::VAR_FRAMESCRIPT_ERROR_HANDLER_REF));
        for (int i = 1;; ++i) {
            int errIdx = 0;
            if (errRef > 0) {
                RawGetI(L, REGISTRY_INDEX, errRef);
                errIdx = GetTop(L);
            }
            RawGetI(L, callbacks, i);
            if (Type(L, -1) == TYPE_NIL)
                break;
            PushValue(L, frame);
            PCall(L, 1, 0, errIdx);
            SetTop(L, callbacks);
        }
    }
    SetTop(L, top);
}

using OnUpdate_t = void(__fastcall *)(void *smf, void *edx, float elapsed);
OnUpdate_t g_origOnUpdate = nullptr;

void __fastcall OnUpdate_h(void *smf, void *edx, float elapsed) {
    g_origOnUpdate(smf, edx, elapsed);
    const auto it = g_frames.find(smf);
    if (it == g_frames.end() || !it->second)
        return;
    RunCallbacks(smf);
    const auto again = g_frames.find(smf); // a callback can add frames
    if (again != g_frames.end())
        again->second = false;
}

const Game::HookAutoRegister _updateHook{Offsets::FUN_SCROLLINGMESSAGEFRAME_ON_UPDATE,
                                         reinterpret_cast<void *>(&OnUpdate_h),
                                         reinterpret_cast<void **>(&g_origOnUpdate)};

const ScrollingMessageFrame::Layout::AutoSubscribe _layoutSub{&MarkDirty};

int __fastcall Script_AddOnDisplayRefreshedCallback(void *L) {
    using namespace Game::Lua;
    void *frame = ResolveTypedObject(L, 1, Offsets::VAR_SCROLLINGMESSAGEFRAME_LUA_TYPE_ID);
    if (frame == nullptr)
        return 0;
    if (Type(L, 2) != TYPE_FUNCTION) {
        Error(L, "Usage: %s:AddOnDisplayRefreshedCallback(callback)", ObjectDisplayName(frame));
        return 0;
    }
    if (g_callbacksRef == kNoRef) {
        NewTable(L);
        g_callbacksRef = Ref(L, REGISTRY_INDEX);
    }
    RawGetI(L, REGISTRY_INDEX, g_callbacksRef);
    const int frames = GetTop(L);
    PushValue(L, 1);
    RawGet(L, frames);
    if (Type(L, -1) != TYPE_TABLE) {
        SetTop(L, frames);
        NewTable(L);
        PushValue(L, 1);
        PushValue(L, -2);
        RawSet(L, frames); // frames[self] = callbacks
    }
    const int callbacks = GetTop(L);
    int n = 1;
    for (;; ++n) {
        RawGetI(L, callbacks, n);
        const bool end = Type(L, -1) == TYPE_NIL;
        SetTop(L, -2);
        if (end)
            break;
    }
    PushNumber(L, n);
    PushValue(L, 2);
    RawSet(L, callbacks);
    g_frames.emplace(frame, false);
    return 0;
}

const Game::Lua::FrameMethodEntry kMethods[] = {
    {"AddOnDisplayRefreshedCallback", &Script_AddOnDisplayRefreshedCallback},
};

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("callback", "function", "Called as callback(frame)."),
};
const Game::Doc::Function kAddOnDisplayRefreshedCallback{
    "Adds a function that runs after the frame refreshes the messages it shows.", kArgs, {}};

const Game::Doc::Method kDocs[] = {
    {"AddOnDisplayRefreshedCallback", &kAddOnDisplayRefreshedCallback},
};

void RegisterLuaFunctions() {
    // Each registration pass is a new Lua state; the old state's ref is gone.
    g_callbacksRef = kNoRef;
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_SCROLLINGMESSAGEFRAME_METHOD_REGISTRY),
                                    kMethods, 1, kDocs, 1);
}

void PrepareForReload() { g_frames.clear(); }

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::ReloadAutoRegister _reload{&PrepareForReload};

} // namespace

void MarkDirty(void *smf) {
    const auto it = g_frames.find(smf);
    if (it != g_frames.end())
        it->second = true;
}

} // namespace ScrollingMessageFrame::DisplayRefreshed
