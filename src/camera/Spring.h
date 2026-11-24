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

#include <cmath>

namespace Camera {

// A value and its velocity, eased by SmoothCriticallyDamped.
struct Spring {
    float value;
    float velocity;
};

// The 3.80.2 client's critically damped spring (SmoothCriticallyDampedFloat
// 0x142a5bbd0, the "SmoothCD" of Game Programming Gems 4), which every
// ActionCam ease uses. `omega` is the stiffness, `dt` the frame time in
// seconds. Within `epsilon` (at least 1e-5) of the target, or once a step
// crosses it, the spring snaps to the target with no velocity and returns
// true.
inline bool SmoothCriticallyDamped(Spring &s, float target, float omega, float dt,
                                   float epsilon = 0.0f) {
    const float d = s.value - target;
    if (epsilon <= 1e-5f)
        epsilon = 1e-5f;
    if (epsilon < std::fabs(d)) {
        const float k = omega * dt;
        const float e = 1.0f / (k * k * 0.235f * k + 1.0f + k * k * 0.48f + k);
        const float t = (d * omega + s.velocity) * dt;
        const float value = (t + d) * e + target;
        s.velocity = (s.velocity - t * omega) * e;
        s.value = value;
        if (0.0f < (value - target) * d)
            return false;
    }
    s.value = target;
    s.velocity = 0.0f;
    return true;
}

// Two values eased together (SmoothCriticallyDampedVec2 0x142a5bcd0): the
// same step on each, the epsilon test on the larger difference, and the
// crossing test on their dot product.
inline bool SmoothCriticallyDamped2(Spring &a, Spring &b, float targetA, float targetB,
                                    float omega, float dt, float epsilon = 0.0f) {
    const float da = a.value - targetA;
    const float db = b.value - targetB;
    if (epsilon <= 1e-5f)
        epsilon = 1e-5f;
    const float largest = std::fabs(da) < std::fabs(db) ? std::fabs(db) : std::fabs(da);
    if (epsilon < largest) {
        const float k = omega * dt;
        const float e = 1.0f / (k * k * 0.235f * k + 1.0f + k * k * 0.48f + k);
        const float ta = (da * omega + a.velocity) * dt;
        const float tb = (db * omega + b.velocity) * dt;
        a.value = (ta + da) * e + targetA;
        b.value = (tb + db) * e + targetB;
        a.velocity = (a.velocity - ta * omega) * e;
        b.velocity = (b.velocity - tb * omega) * e;
        if (0.0f < (a.value - targetA) * da + (b.value - targetB) * db)
            return false;
    }
    a = {targetA, 0.0f};
    b = {targetB, 0.0f};
    return true;
}

} // namespace Camera
