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

// Shared per-frame tick that fires in BOTH the glue and in-world states — the
// counterpart to Tick::WorldTick, which is world-only.
//
// The hook target is `FUN_UI_RENDER_ROOT` (`0x00764330`), the CSimpleTop
// singleton's per-frame render callback: it runs every frame the UI draws,
// and because the UI root is (re)built on both the world-init and glue-boot
// paths, that includes the login / character-select screens. WorldTick
// (`FUN_WORLD_TICK`) only fires in-world, so UI-object upkeep that must also
// run on glue — inline-texture icon regions being the motivating case —
// belongs here.
//
// HISTORY: an earlier version of this hook was blamed for an in-world chat
// regression and reverted. That regression was actually the LAA pointer-bound
// bug (see the laa-pointer-bounds memory / commit dc61f77's message) rolling
// its allocation dice at the moment the tick moved — the flicker persisted
// after the revert, which already exonerated the hook. This tick is safe.
//
// Semantics vs WorldTick (choose deliberately per subscriber):
//   • WorldTick  = "world simulation advanced" — world state (units, auras,
//     casts, camera) is valid. World-only. Use for gameplay-state upkeep.
//   • FrameTick  = "a UI frame is rendering" — fires always, world state may
//     be absent (glue). Use ONLY for UI-object upkeep safe with no world.
//
// Subscribers run at the **tail** of the render callback (after the UI strata
// walk completes, so no frame is mid-draw and mutating regions is safe;
// mutations take effect next frame — same latency class as WorldTick). Order
// across subscribers is unspecified.
//
// Module pattern (identical to WorldTick): declare a file-scope
// `static const Tick::FrameTick::AutoSubscribe` in your TU; the constructor
// chains the callback onto the internal list at static-init time.

namespace Tick::FrameTick {

using Callback = void (*)();

struct AutoSubscribe {
    explicit AutoSubscribe(Callback cb);
    Callback cb;
    AutoSubscribe *next;
};

} // namespace Tick::FrameTick
