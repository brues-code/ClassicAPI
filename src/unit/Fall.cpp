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

#include "unit/Fall.h"

#include "Game.h"
#include "Offsets.h"

#include <cmath>

namespace Unit::Fall {

namespace {

const Game::Live<float> kG{Offsets::VAR_MOVER_GRAVITY};
const Game::Live<float> kHalfG{Offsets::VAR_MOVER_HALF_GRAVITY};
const Game::Live<float> kInvG{Offsets::VAR_MOVER_INV_GRAVITY};
const Game::Live<float> kTerminal{Offsets::VAR_MOVER_TERMINAL_SPEED};
const Game::Live<float> kTerminalSlow{Offsets::VAR_MOVER_TERMINAL_SPEED_SLOW};

} // namespace

double Distance(float seconds, float fallSpeed, bool slowFall) {
    const double cap = slowFall ? kTerminalSlow : kTerminal;
    // The fall speed's start, capped (0x007C5D40).
    const double v = fallSpeed > cap ? cap : static_cast<double>(fallSpeed);
    const double dt = seconds;
    const double end = dt * kG + v;
    if (end > cap || std::isnan(end)) {
        const double tT = (cap - v) * kInvG;
        return (v + static_cast<double>(kHalfG) * tT) * tT + (dt - tT) * cap;
    }
    return (dt * kHalfG + v) * dt;
}

} // namespace Unit::Fall
