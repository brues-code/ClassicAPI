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

// `C_PlayerInfo.UnitDistanceBetween` — the distance between two units'
// surfaces instead of their centers: the center-to-center world distance
// minus both units' bounding radii. This is the measurement SP3 exposes as
// `UnitXP("distanceBetween", …)`.
//
//   `C_PlayerInfo.UnitDistanceBetween("target") → yards` — player→target.
//   `C_PlayerInfo.UnitDistanceBetween("party1", "target") → yards`.
//   `C_PlayerInfo.UnitDistanceBetween("0x0000000000001234", "target")`.
//
// The result is a signed surface gap, not a yard count: overlapping
// hitboxes give 0 or a negative number, the convention SP3 uses. Callers
// that need a non-negative number clamp with `math.max(0, d)`.
//
// Registered ONLY as a C_PlayerInfo table function (no global alias), so
// it can't collide with addons that define their own distance helpers.
// Returns nil when either unit has no known position.

#include "Game.h"
#include "Offsets.h"
#include "guid/Guid.h"
#include "object/Resolve.h"
#include "unit/Position.h"

#include <cmath>
#include <cstdint>

namespace Unit::DistanceBetween {

namespace {

// Resolves a Lua string argument to a unit pointer. Accepts both
// standard unit tokens ("player", "target", "party3", …) and raw
// GUID hex-strings ("0xHHHHHHHHLLLLLLLL"). Returns nullptr when the
// string isn't valid in either form, or the resolved entity has no
// live client object.
void *ResolveArg(const char *arg) {
    if (arg == nullptr)
        return nullptr;

    // GUID form: "0x" prefix + 16 hex digits.
    if (arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X')) {
        uint64_t guid = 0;
        if (Guid::Parse(arg, &guid) && guid != 0)
            return Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, nullptr, 0);
        return nullptr;
    }

    // Unit-token form.
    return Unit::Position::ResolveToken(arg);
}

// Reads a unit's bounding radius (the engine's per-unit "reach") from
// `[m_objectFields + 0x1F0]`. Returns 0 for a null object or a unit whose
// descriptor isn't populated yet, so the surface distance degrades to the
// raw center distance. Same field `Unit::Range` reads.
float BoundingRadius(void *obj) {
    if (obj == nullptr)
        return 0.0f;
    auto *fields = *reinterpret_cast<const uint8_t *const *>(
        static_cast<const uint8_t *>(obj) + Offsets::OFF_UNIT_DESCRIPTOR);
    if (fields == nullptr)
        return 0.0f;
    return *reinterpret_cast<const float *>(
        fields + Offsets::OFF_UNIT_FIELD_BOUNDING_RADIUS);
}

// `C_PlayerInfo.UnitDistanceBetween("unit1" [, "unit2"])`. With one
// argument the other unit is the player. The order of the arguments does
// not matter: the measurement is symmetric.
int __fastcall Script_UnitDistanceBetween(void *L) {
    const int nargs = Game::Lua::GetTop(L);
    const char *token1 = nullptr;
    const char *token2 = nullptr;

    if (nargs == 1) {
        if (!Game::Lua::IsString(L, 1)) {
            Game::Lua::Error(L, "Usage: UnitDistanceBetween(\"unit\") "
                                 "or UnitDistanceBetween(\"unit1\", \"unit2\")");
            return 0;
        }
        token1 = "player";
        token2 = Game::Lua::ToString(L, 1);
    } else if (nargs == 2) {
        if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsString(L, 2)) {
            Game::Lua::Error(L, "Usage: UnitDistanceBetween(\"unit\") "
                                 "or UnitDistanceBetween(\"unit1\", \"unit2\")");
            return 0;
        }
        token1 = Game::Lua::ToString(L, 1);
        token2 = Game::Lua::ToString(L, 2);
    } else {
        Game::Lua::Error(L, "Usage: UnitDistanceBetween(\"unit\") "
                             "or UnitDistanceBetween(\"unit1\", \"unit2\")");
        return 0;
    }

    void *unit1 = ResolveArg(token1);
    void *unit2 = ResolveArg(token2);
    if (unit1 == nullptr || unit2 == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    if (unit1 == unit2) {
        Game::Lua::PushNumber(L, 0.0);
        return 1;
    }

    float pos1[3] = {};
    float pos2[3] = {};
    if (!Unit::Position::Read(unit1, pos1) ||
        !Unit::Position::Read(unit2, pos2)) {
        Game::Lua::PushNil(L);
        return 1;
    }

    const float dx = pos1[0] - pos2[0];
    const float dy = pos1[1] - pos2[1];
    const float dz = pos1[2] - pos2[2];
    const double center = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double surface = center - BoundingRadius(unit1) - BoundingRadius(unit2);
    // floor(+0.5) rather than floor() so a 7.6-yard gap reads as 8 the way
    // addons ported from SP3 (`math.floor(d + 0.5)`) expect, and a -0.4
    // overlap reads as 0 instead of -1.
    Game::Lua::PushNumber(L, std::floor(surface + 0.5));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_PlayerInfo", "UnitDistanceBetween",
                                     &Script_UnitDistanceBetween);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::DistanceBetween
