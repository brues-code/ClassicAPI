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

// `UI_SCALE_CHANGED` (no payload) — as the 3.80.2 client fires it. One
// function fires it (0x14169C3E0), immediately and always after
// UPDATE_FLOATING_CHAT_WINDOWS and DISPLAY_SIZE_CHANGED, from two places:
//   - Every apply of the interface scale (0x141A4B580): the uiScale and
//     useUiScale cvars, a resolution change and a window resize, while
//     UIParent exists, after UIParent's scale is set and the layout drained,
//     whether or not the scale changed.
//   - The end of CGGameUI::Initialize (0x14168F883), at login and every
//     reload, once the addons have loaded.
//
// 1.12 funnels every scale apply through one sink, SetUiParentScale
// (FUN_SET_UI_PARENT_SCALE), which does the same SetScale + layout drain
// under the same UIParent test; its six callers are the uiScale and
// useUiScale cvar callbacks, OnDisplaySizeChanged with useUiScale on, and the
// automatic scale. After it, this fires DISPLAY_SIZE_CHANGED (the engine's
// own event, through FUN_FIRE_EVENT_NO_ARGS) and then UI_SCALE_CHANGED. 1.12
// has no UPDATE_FLOATING_CHAT_WINDOWS.
//
// A resize already fires DISPLAY_SIZE_CHANGED from OnDisplaySizeChanged
// (0x00492F5C), after the sink. That fire is dropped when the sink has fired
// the pair in the same frame, so a resize gives one of each, in 3.80.2's
// order.
//
// The login/reload fire goes right before PLAYER_LOGIN, which 1.12 fires once
// per UI instance after CGGameUI::Initialize (0x0048FBF0) has loaded the
// addons and applied the scale. 1.12 applies no scale at login while UIParent
// exists, so this is the one fire there (3.80.2 also applies once more, a
// second pair).
//
// As in 3.80.2, Lua's UIParent:SetScale() fires nothing.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "event/SignalHook.h"
#include "tick/WorldTick.h"

namespace UI::Scale {

namespace {

const Game::Doc::Event kDoc{
    "UIManager",
    "Fires each time the interface scale is applied to UIParent, after a "
    "uiScale or useUiScale change or a new window size, and once at login and "
    "after a reload; always right after DISPLAY_SIZE_CHANGED.",
    {}};
const Event::Custom::AutoReserve _reserve{"UI_SCALE_CHANGED", &kDoc};

// Our own DISPLAY_SIZE_CHANGED is passing through the dispatcher.
bool g_firing = false;
// The pair fired from the sink this frame: the engine's DISPLAY_SIZE_CHANGED
// that follows it in OnDisplaySizeChanged is dropped.
bool g_dropEngineSize = false;

bool UiParentExists() { return *reinterpret_cast<void *const *>(Offsets::VAR_UI_PARENT) != nullptr; }

void FirePair() {
    using FireNoArgs_t = void(__fastcall *)(int eventID);
    g_firing = true;
    reinterpret_cast<FireNoArgs_t>(Offsets::FUN_FIRE_EVENT_NO_ARGS)(Offsets::EVENT_DISPLAY_SIZE_CHANGED);
    g_firing = false;
    const int slot = _reserve.Slot();
    if (slot >= 0)
        Event::Custom::Fire(slot, "");
}

using SetUiParentScale_t = void(__stdcall *)(float scale);
SetUiParentScale_t g_original = nullptr;

void __stdcall SetUiParentScale_h(float scale) {
    g_original(scale);
    if (!UiParentExists())
        return;
    FirePair();
    g_dropEngineSize = true;
}

bool InterceptDisplaySize(int eventID) {
    if (eventID != Offsets::EVENT_DISPLAY_SIZE_CHANGED || g_firing || !g_dropEngineSize)
        return false;
    g_dropEngineSize = false;
    return true;
}

void OnWorldTick() { g_dropEngineSize = false; }

void OnPlayerLogin() {
    if (UiParentExists())
        FirePair();
}

const Game::HookAutoRegister _hookreg{
    Offsets::FUN_SET_UI_PARENT_SCALE,
    reinterpret_cast<void *>(&SetUiParentScale_h),
    reinterpret_cast<void **>(&g_original)};
const Event::SignalHook::AutoSubscribe _intercept{&InterceptDisplaySize};
const Event::SignalHook::AutoObserve _login{Offsets::EVENT_PLAYER_LOGIN, &OnPlayerLogin};
const Tick::WorldTick::AutoSubscribe _tick{&OnWorldTick};

} // namespace

} // namespace UI::Scale
