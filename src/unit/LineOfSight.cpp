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

// `UnitInLineOfSight(unit)` — true if the player has clear line of sight
// to `unit`, false if world geometry blocks it, nil when the check can't
// apply (unresolvable / absent token, or a non-unit object). A ClassicAPI
// extension — retail has no such global — implemented natively on the
// engine's world-collision core so it works without UnitXP_SP3.
//
// Mechanism: trace one segment between the two units through the client's
// world geometry via `CWorld::Intersect` (`FUN_CWORLD_INTERSECT`). The
// question the caller is really asking is "will the server reject my cast
// with SPELL_FAILED_LINE_OF_SIGHT", so the segment mirrors the server's
// `WorldObject::IsWithinLOSInMap`: ONE trace, each end raised by its own
// unit's collision height (`Unit::GetLosCheckPosition`). The client's
// `CMovement+0xB4` is that same height — CreatureModelData collision height
// × scale, set by `0x6174b0` from `0x60b270`. An earlier version (copied
// from UnitXP_SP3) first traced both ends at the SHORTER unit's height and
// returned "in sight" if that cleared; the server has no such pass, so a
// segment that slipped under an overhang or ledge read as clear while the
// cast still failed.
//
// Class word `0x100111` is the engine's own (walk-to-attack `0x60fe18`,
// corpse `0x5d6a9d`). It runs terrain (0xf00), WMO groups (0x10), the cell /
// group doodad queue + M2 ray (0xf0000f → `0x69cdb0`, `0x7089c0`) and game
// object entities (0xf00000 → `0x69ce90`). Its WMO face mask is `0x84`
// (rejects detail faces, MOPY 0x04), which matches the server's vmap
// extractor dropping the same faces; the camera's `0x100171` (mask `0x82`)
// would keep detail faces the server ignores. If UnitXP_SP3 is loaded it has
// the intersect hooked, but a call routes through its detour harmlessly.

#include "Game.h"
#include "Offsets.h"
#include "unit/Position.h"

#include <cmath>
#include <cstdint>

namespace Unit::LineOfSight {

namespace {

struct Vec3 {
    float x, y, z;
};

// CWorld::Intersect(start, end, unused, hitOut, tOut, flags) -> hit?
using Intersect_t = char(__fastcall *)(const Vec3 *start, const Vec3 *end,
                                       int unused, Vec3 *hitOut, float *tOut,
                                       uint32_t flags);

constexpr uint32_t kLoSFlags = 0x100111;   // terrain + WMO (LoS combo)
constexpr float kMaxTraceYards = 150.0f;   // guard: raw call can crash beyond
constexpr float kSamePointYards = 0.05f;

int ObjectType(void *obj) {
    return Game::Read<int>(obj, Offsets::OFF_CGOBJECT_TYPE_ID);
}

bool IsUnitOrPlayer(void *obj) {
    const int t = ObjectType(obj);
    return t == Offsets::OBJECT_TYPE_UNIT || t == Offsets::OBJECT_TYPE_PLAYER;
}

// Eye-height offset for a unit: its collision-box height, read out of the
// CMovement sub-struct. 0 if the movement pointer is absent (degrades to a
// foot-level trace rather than crashing).
float EyeHeight(void *unit) {
    const uint32_t cmov =
        Game::Read<uint32_t>(unit, Offsets::OFF_UNIT_MOVEMENT_INFO_PTR);
    if (cmov == 0)
        return 0.0f;
    return *reinterpret_cast<float *>(cmov +
                                      Offsets::OFF_CMOVEMENT_COLLISION_BOX_HEIGHT);
}

// True if the segment a→b is clear of world geometry. `CWorld::Intersect`
// returns whether it hit and sets `t` to the hit fraction along a→b; a hit
// with t in [0,1] lies between the endpoints and blocks. The 150yd guard
// (over-range → treat as blocked) and same-point short-circuit mirror
// UnitXP_SP3's wrapper, which documented a crash on very long segments.
bool SegmentClear(const Vec3 &a, const Vec3 &b) {
    const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist > kMaxTraceYards)
        return false;
    if (dist <= kSamePointYards)
        return true;

    Vec3 hit{};
    float t = 1.0f;
    auto intersect = reinterpret_cast<Intersect_t>(Offsets::FUN_CWORLD_INTERSECT);
    const char hitGeometry = intersect(&a, &b, 0, &hit, &t, kLoSFlags);
    if (hitGeometry == 0)
        return true;
    return !(t >= 0.0f && t <= 1.0f);
}

// Player↔unit LoS. Returns 1 = in sight, 0 = blocked, -1 = unknown
// (position unavailable). One segment, each end at its own unit's
// collision height — the server's check (see the file header).
int InSight(void *a, void *b) {
    if (a == b)
        return 1;

    float pa[3];
    float pb[3];
    if (!Unit::Position::Read(a, pa) || !Unit::Position::Read(b, pb))
        return -1;

    const Vec3 ea{pa[0], pa[1], pa[2] + EyeHeight(a)};
    const Vec3 eb{pb[0], pb[1], pb[2] + EyeHeight(b)};
    return SegmentClear(ea, eb) ? 1 : 0;
}

int __fastcall Script_UnitInLineOfSight(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitInLineOfSight(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);

    void *player = Unit::Position::ResolveToken("player");
    void *unit = Unit::Position::ResolveToken(token);
    if (player == nullptr || unit == nullptr || !IsUnitOrPlayer(player) ||
        !IsUnitOrPlayer(unit)) {
        Game::Lua::PushNil(L);
        return 1;
    }

    const int sight = InSight(player, unit);
    if (sight < 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushBool(L, sight != 0);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitInLineOfSight",
                                      &Script_UnitInLineOfSight);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::LineOfSight
