// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

#include "WorldTick.h"

#include "FrameTick.h"
#include "Game.h"
#include "Offsets.h"

namespace Tick::WorldTick {

namespace {

AutoSubscribe *g_head = nullptr;
bool g_ticked = false;

using WorldTick_t = void(__fastcall *)(int, int, int);
WorldTick_t WorldTick_o = nullptr;

// The world tick runs inside the UI draw: WorldFrame's layer-0 draw callback
// calls it in the middle of the strata walk, after the WORLD stratum's batches
// are rebuilt and before its levels above 0 — the nameplates — are drawn.
// Script work done there that changes a font string in that stratum (an addon
// restyling a plate on NAME_PLATE_CREATED) closes the font a built batch still
// draws, and the draw then uploads a freed font's atlas pages. So only note
// that the world ran.
void __fastcall WorldTick_h(int a, int b, int c) {
    WorldTick_o(a, b, c);
    g_ticked = true;
}

// From the UI render root's tail: the strata walk is over, so whatever the
// subscribers change is rebuilt before it is drawn again, as with OnUpdate.
void Drain() {
    if (!g_ticked)
        return;
    g_ticked = false;
    for (auto *node = g_head; node != nullptr; node = node->next)
        node->cb();
}

} // namespace

AutoSubscribe::AutoSubscribe(Callback cb) : cb(cb), next(g_head) {
    g_head = this;
}

static const Game::HookAutoRegister _hook{
    Offsets::FUN_WORLD_TICK,
    reinterpret_cast<void *>(&WorldTick_h),
    reinterpret_cast<void **>(&WorldTick_o)};

static const Tick::FrameTick::AutoSubscribe _drain{&Drain};

} // namespace Tick::WorldTick
