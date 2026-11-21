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

#include "lua/FullUserdata.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// The 3.80.2 client's curve objects (C_CurveUtil.CreateCurve /
// CreateColorCurve; RTTI LuaCurveObject, LuaColorCurveObject, both a
// LuaCurveObjectBase), as the engine-side users take them: a reference-counted
// object that holders share (a SecondsFormatter keeps a reference, not a copy).
// docs/notes/curve-util-3802.md has the addresses.
namespace Curve {

// Enum.LuaCurveType.
enum Type : uint8_t {
    TYPE_LINEAR = 0,
    TYPE_STEP = 1,
    TYPE_COSINE = 2,
    TYPE_CUBIC = 3,
};

constexpr std::size_t kMaxPoints = 256;

struct Base : Game::Lua::FullUserdata::Object {
    uint8_t type = TYPE_LINEAR;
};

struct ScalarPoint {
    float x;
    float y;
};

// A colour point keeps its colour as 8 bits a channel: b | g << 8 | r << 16 | a << 24.
struct ColorPoint {
    float x;
    uint32_t color;
};

// The points are kept sorted by x.
struct Scalar : Base {
    std::vector<ScalarPoint> points;
    // 0x141BA33E0, in float arithmetic.
    float Evaluate(float x) const;
};

struct Color : Base {
    std::vector<ColorPoint> points;
    // 0x141BA24A0: the packed colour.
    uint32_t Evaluate(float x) const;
};

// The curve at `idx` when the value is a LuaCurveObject, else null.
Scalar *ToScalar(void *L, int idx);

// Pushes a new LuaCurveObject value for `c`, which gains a reference.
void Push(void *L, Scalar *c);

} // namespace Curve
