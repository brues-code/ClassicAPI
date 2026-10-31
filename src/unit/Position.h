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

#pragma once

#include "Game.h"
#include "Offsets.h"

// Shared world-position and facing reads for any CGObject-derived unit.
// The position comes from the object's `GetPosition` virtual (vtable
// slot 5) — the same one `CheckInteractDistance` / `UnitInRange` use —
// and the facing from its `GetFacing` virtual (slot 6).
// Header-only so callers (UnitInRange, CastAtUnit, UseAtUnit) don't
// each re-derive the vtable-slot math.

namespace Unit::Position {

// Resolves a unit token ("player", "target", "party1", "mouseover",
// …) to a `CGUnit *`. Returns nullptr for a recognized-but-absent
// token (e.g. "party5" with no such member). NOTE: the engine's
// resolver raises a Lua error for genuinely unrecognized token
// strings — same contract as `UnitHealth("garbage")` — so only feed
// it real unit tokens, or strings you're willing to let error.
inline void *ResolveToken(const char *token) {
    if (token == nullptr)
        return nullptr;
    return Game::ResolveUnitToken(token);
}

// Reads `obj`'s world position into `out[3]` (x, y, z). Calls
// `obj->vtable[5](outBuf)`; the returned float* may equal `outBuf`
// (object filled it) or point at a cached position field — either
// way the values land in `out`. Returns false if `obj` is null or
// the virtual yields no position (object has no known position yet).
inline bool Read(void *obj, float out[3]) {
    if (obj == nullptr)
        return false;
    auto **vtable = *reinterpret_cast<void ***>(obj);
    using GetPosition_t = float *(__thiscall *)(void *self, float outBuf[3]);
    auto fn = reinterpret_cast<GetPosition_t>(
        vtable[Offsets::OFF_CGOBJECT_VTBL_GET_POSITION / 4]);
    float *p = fn(obj, out);
    if (p == nullptr)
        return false;
    if (p != out) {
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
    }
    return true;
}

// `obj`'s world facing in radians (0 = north, counter-clockwise, 0..2pi)
// from its `GetFacing` virtual (vtable slot 6). On units and players that
// is CGUnit_C::GetFacing (0x5F1F30): the mover's facing passed through
// CMovement::TransportFacingToWorld (0x7C4AE0), so a unit on a boat or
// zeppelin reads its world facing, not the transport-relative one the
// mover stores.
inline float Facing(void *obj) {
    auto **vtable = *reinterpret_cast<void ***>(obj);
    using GetFacing_t = float(__thiscall *)(void *self);
    return reinterpret_cast<GetFacing_t>(
        vtable[Offsets::OFF_CGOBJECT_VTBL_GET_FACING / 4])(obj);
}

// Convenience: resolve a token and read its position in one step.
inline bool ReadToken(const char *token, float out[3]) {
    return Read(ResolveToken(token), out);
}

} // namespace Unit::Position
