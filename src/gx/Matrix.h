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

// The engine's 4x4 matrix helpers (Offsets.h FUN_MATRIX_*, FUN_TRANSFORM_*).
// Matrices are row-vector: a point is a row times the matrix, rows 0..2 are
// the scaled axes and row 3 the translation. Each transform below
// pre-multiplies, so the one called last applies to a point first.

#include "Offsets.h"

#include <cstring>

namespace Gx {

using Matrix = float[16];

inline void Identity(float *m) {
    std::memset(m, 0, sizeof(Matrix));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// TranslateMatrix: row 3 += t · rows 0..2.
inline void Translate(float *m, const float (&t)[3]) {
    reinterpret_cast<void(__thiscall *)(float *, const float *)>(Offsets::FUN_MATRIX_TRANSLATE)(m, t);
}

// RotateMatrix: by `angle` radians about the unit `axis`.
inline void Rotate(float *m, float angle, const float (&axis)[3]) {
    reinterpret_cast<void(__thiscall *)(float *, float, const float *, int)>(Offsets::FUN_MATRIX_ROTATE)(
        m, angle, axis, 1);
}

// By the quaternion {x, y, z, w}.
inline void RotateQuaternion(float *m, const float *q) {
    reinterpret_cast<void(__thiscall *)(float *, const float *)>(Offsets::FUN_MATRIX_ROTATE_QUATERNION)(m, q);
}

inline void Scale(float *m, float s) {
    reinterpret_cast<void(__thiscall *)(float *, float)>(Offsets::FUN_MATRIX_SCALE)(m, s);
}

// MultiplyMatrix4: out = a × b.
inline void Multiply(float *out, const float *a, const float *b) {
    reinterpret_cast<float *(__fastcall *)(float *, const float *, const float *)>(Offsets::FUN_MATRIX_MULTIPLY)(
        out, a, b);
}

// m = m × b.
inline void MultiplyInPlace(float *m, const float *b) {
    reinterpret_cast<float *(__thiscall *)(float *, const float *)>(Offsets::FUN_MATRIX_MULTIPLY_IN_PLACE)(m, b);
}

// InvertAffineMatrix: the inverse of a rotation, uniform `scale` and
// translation.
inline void InvertAffine(const float *m, float *out, float scale) {
    reinterpret_cast<void(__thiscall *)(const float *, float *, float)>(Offsets::FUN_MATRIX_INVERT_AFFINE)(
        m, out, scale);
}

// The point through the matrix (`in` and `out` may be the same).
inline void TransformPoint(const float (&in)[3], const float *m, float (&out)[3]) {
    float v[3] = {in[0], in[1], in[2]};
    reinterpret_cast<float *(__fastcall *)(float *, float *, const float *)>(Offsets::FUN_TRANSFORM_POINT_IN_PLACE)(
        out, v, m);
}

// The box {min xyz, max xyz} through the matrix, as the box around its
// transformed corners.
inline void TransformBox(const float *m, const float (&box)[6], float (&out)[6]) {
    reinterpret_cast<void(__fastcall *)(const float *, const float *, float *)>(Offsets::FUN_TRANSFORM_BOX)(m, box,
                                                                                                           out);
}

} // namespace Gx
