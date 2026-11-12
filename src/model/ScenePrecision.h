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

// The two helpers the transcribed client mover (SceneContact.cpp,
// SceneMover.cpp) writes its arithmetic with. The client runs it on the x87
// at 53 bits and stores floats, so a sum is taken in doubles (D) and each
// store is a float cast. A constant the client reads from memory is a
// Game::Live over its Offsets.h address; Bits is for the few that are code
// immediates or have no address.

#include <cstdint>
#include <cstring>

namespace Model::Scene::Precision {

// The float with these bits.
inline float Bits(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

// A float widened to the 53-bit sum the x87 keeps.
inline double D(float x) { return x; }

} // namespace Model::Scene::Precision
