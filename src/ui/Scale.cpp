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

// `UI_SCALE_CHANGED` (no payload) — fires each time the engine applies the
// interface scale to UIParent.
//
// 4.3.4 signals it from the two functions that set UIParent's scale
// (CGGameUI::SetUIScale and ScaleToScreenSize), right after SetFrameScale and
// the pending layout pass, and only while UIParent exists. 1.12 funnels every
// such change through one sink, SetUiParentScale (FUN_SET_UI_PARENT_SCALE),
// which does the same SetScale + layout drain under the same UIParent test.
// Hooking it covers all six callers: the uiScale and useUiScale cvar
// callbacks, OnDisplaySizeChanged with useUiScale on, and the automatic
// scale. Each path calls the sink once, so one change fires one event; a
// resize fires it before DISPLAY_SIZE_CHANGED. As in 4.3.4, a set of the
// same value still fires, and Lua's UIParent:SetScale() does not.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"

namespace UI::Scale {

namespace {

const Game::Doc::Event kDoc{
    "UIManager",
    "Fires each time the interface scale is applied to UIParent, after a "
    "uiScale or useUiScale change or a new window size.",
    {}};
const Event::Custom::AutoReserve _reserve{"UI_SCALE_CHANGED", &kDoc};

using SetUiParentScale_t = void(__stdcall *)(float scale);
SetUiParentScale_t g_original = nullptr;

void __stdcall SetUiParentScale_h(float scale) {
    g_original(scale);
    if (*reinterpret_cast<void *const *>(Offsets::VAR_UI_PARENT) != nullptr)
        Event::Custom::Fire(_reserve.Slot(), "");
}

const Game::HookAutoRegister _hookreg{
    Offsets::FUN_SET_UI_PARENT_SCALE,
    reinterpret_cast<void *>(&SetUiParentScale_h),
    reinterpret_cast<void **>(&g_original)};

} // namespace

} // namespace UI::Scale
