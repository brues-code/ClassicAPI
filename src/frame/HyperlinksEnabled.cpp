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
// The flag is kept per frame, default on. All three scripts fire through
// FUN_FRAME_RUN_SCRIPT_WITH_CONTEXT with the script's slot on the frame, so a
// `Frame::RunnerHook` subscriber skips Enter and Click while the frame's flag
// is off — no hook of our own.
//
// Leave is never skipped. 3.3.5's setter (0x00973C40) doesn't gate the
// scripts: it stores the flag (+0x348) and calls 0x009699C0, which destroys
// the frame's link hit-regions and rebuilds them only while enabled. 1.12's
// hit-regions can't be removed that way, and skipping Leave would strand
// whatever Enter showed (a GameTooltip) when a frame is disabled while a
// link is hovered. A Leave with no Enter before it is harmless.
//
// The argument is parsed with the engine's loose boolean, default on — the
// same helper and default 3.3.5's setter uses (0x00815500(L, 2, 1)), so a
// bare `SetHyperlinksEnabled()` enables and `SetHyperlinksEnabled(0)`
// disables.

#include "Game.h"
#include "Offsets.h"
#include "frame/RunnerHook.h"

#include <cstdint>
#include <unordered_set>

namespace Frame::HyperlinksEnabled {

namespace {

// Frames with hyperlinks switched off. Every frame is rebuilt on /reload, so
// the set is cleared then. Only ScrollingMessageFrame / SimpleHTML frames get
// in (the methods live on those registries alone), which is what makes the
// slot-offset test below safe: on other frame types those offsets hold
// other things.
std::unordered_set<void *> g_disabled;


bool OnRun(void *frame, uint32_t *slotPtr, const char * /*fmt*/, void * /*varargs*/) {
    if (g_disabled.empty() || g_disabled.count(frame) == 0)
        return false;
    const uintptr_t off = reinterpret_cast<uintptr_t>(slotPtr) - reinterpret_cast<uintptr_t>(frame);
    return off == Offsets::OFF_HYPERLINK_ENTER_SLOT || off == Offsets::OFF_HYPERLINK_CLICK_SLOT;
}

const Frame::RunnerHook::AutoSubscribe _runnerSub{&OnRun};

int __fastcall Script_SetHyperlinksEnabled(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    if (Game::Lua::ToFlag(L, 2, true))
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
