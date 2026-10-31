// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

#pragma once

// Shared per-frame tick subscription. The engine's world-subsystem
// update at `FUN_WORLD_TICK` (`0x0066FD50`) is the canonical
// once-per-frame hook target — single caller, quiet code region, no
// known collisions with other Octo DLLs. MinHook only permits ONE
// active hook per target address, so any feature that wants a
// per-frame tick must go through this module instead of registering
// its own `HookAutoRegister`.
//
// Subscribers run once per frame in which the world ticked, at the
// **tail** of that frame's UI draw (from `Tick::FrameTick`), never at
// the hook itself: the world tick runs inside the UI strata walk, where
// script work frees fonts that already-built batches still draw (see
// WorldTick.cpp). Order across subscribers is unspecified (LIFO of
// static-init order across TUs), so subscribers must not depend on
// each other.
//
// Module pattern, matching `HookAutoRegister` / `ModuleAutoRegister`:
// declare a file-scope `static const Tick::WorldTick::AutoSubscribe`
// in your module's TU. The constructor chains the callback onto the
// internal list at static-init time, before `DllMain` runs.

namespace Tick::WorldTick {

using Callback = void (*)();

struct AutoSubscribe {
    explicit AutoSubscribe(Callback cb);
    Callback cb;
    AutoSubscribe *next;
};

} // namespace Tick::WorldTick
