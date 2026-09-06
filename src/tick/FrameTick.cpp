// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

#include "FrameTick.h"

#include "Game.h"
#include "Offsets.h"

namespace Tick::FrameTick {

namespace {

AutoSubscribe *g_head = nullptr;

// FUN_UI_RENDER_ROOT is `__stdcall(const float *bounds, float elapsed)`,
// RET 0x8 (see the Offsets.h note). `elapsed` is the frame delta the UI layout
// pass hands to every frame's per-frame update — the same value OnUpdate
// receives; `bounds` is the layer rect the HLAYER drain passes to every layer,
// which this callback never reads. Both are forwarded untouched; the caller
// discards the return value. Match the convention exactly so the stack stays
// balanced.
using UIRenderRoot_t = void(__stdcall *)(const float *bounds, float elapsed);
UIRenderRoot_t UIRenderRoot_o = nullptr;

void __stdcall UIRenderRoot_h(const float *bounds, float elapsed) {
    // Original UI draw first, subscribers at the tail — the strata walk is
    // complete, so no frame is mid-draw when regions are mutated.
    UIRenderRoot_o(bounds, elapsed);
    for (auto *node = g_head; node != nullptr; node = node->next)
        node->cb();
}

} // namespace

AutoSubscribe::AutoSubscribe(Callback cb) : cb(cb), next(g_head) {
    g_head = this;
}

static const Game::HookAutoRegister _hook{
    Offsets::FUN_UI_RENDER_ROOT,
    reinterpret_cast<void *>(&UIRenderRoot_h),
    reinterpret_cast<void **>(&UIRenderRoot_o)};

} // namespace Tick::FrameTick
