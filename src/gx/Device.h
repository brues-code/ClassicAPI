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

// The graphics device's transforms: its projection and view matrices and its
// viewport (Offsets.h, at FUN_GX_GET_PROJECTION). The engine's own draws that
// leave the UI pass's transforms (SimpleModelDraw 0x0076D240, the ScrollFrame
// draw 0x00787220, the portrait bake) save all three first and restore them
// after; SavedTransforms does that for a scope.

#include "Offsets.h"
#include "gx/Matrix.h"

#include <cstdint>

namespace Gx {

inline void GetProjection(Matrix &m) {
    reinterpret_cast<void(__fastcall *)(float *)>(Offsets::FUN_GX_GET_PROJECTION)(m);
}
inline void SetProjection(const Matrix &m) {
    reinterpret_cast<void(__fastcall *)(const float *)>(Offsets::FUN_GX_SET_PROJECTION)(m);
}
inline void GetView(Matrix &m) {
    reinterpret_cast<void(__fastcall *)(float *)>(Offsets::FUN_GX_GET_VIEW)(m);
}
inline void SetView(const Matrix &m) {
    reinterpret_cast<void(__fastcall *)(const float *)>(Offsets::FUN_GX_SET_VIEW)(m);
}

// In NDC, 0 to 1.
struct Viewport {
    float minX, maxX, minY, maxY, minZ, maxZ;
};

inline Viewport GetViewport() {
    using Fn = void(__fastcall *)(float *, float *, float *, float *, float *, float *);
    Viewport v{};
    reinterpret_cast<Fn>(Offsets::FUN_GX_GET_VIEWPORT)(&v.minX, &v.maxX, &v.minY, &v.maxY, &v.minZ,
                                                      &v.maxZ);
    return v;
}
inline void SetViewport(const Viewport &v) {
    using Fn = void(__stdcall *)(float, float, float, float, float, float);
    reinterpret_cast<Fn>(Offsets::FUN_GX_SET_VIEWPORT)(v.minX, v.maxX, v.minY, v.maxY, v.minZ,
                                                      v.maxZ);
}

// ---- render states (GXRS_*) -----------------------------------------------------
// The device journals each state written after a push; the pop puts every one
// back. The engine brackets each region-layer draw in a pair.

inline void PushStates() { reinterpret_cast<void(__fastcall *)()>(Offsets::FUN_GX_STATE_PUSH)(); }
inline void PopStates() { reinterpret_cast<void(__fastcall *)()>(Offsets::FUN_GX_STATE_POP)(); }

// The render states written in a scope, put back when it ends.
class SavedStates {
  public:
    SavedStates() { PushStates(); }
    ~SavedStates() { PopStates(); }
    SavedStates(const SavedStates &) = delete;
    SavedStates &operator=(const SavedStates &) = delete;
};

inline void SetState(int state, int value) {
    reinterpret_cast<void(__fastcall *)(int, int)>(Offsets::FUN_GX_RS_SET)(state, value);
}
// A pointer state: a texture bind.
inline void SetState(int state, const void *value) {
    reinterpret_cast<void(__fastcall *)(int, const void *)>(Offsets::FUN_GX_RS_SET_PTR)(state, value);
}
// A colour state (fog colour, material).
inline void SetStateWord(int state, uint32_t value) {
    reinterpret_cast<void(__fastcall *)(int, int, uint32_t)>(Offsets::FUN_GX_RS_SET_WORD)(state, 0, value);
}
inline void SetStateFloat(int state, float value) {
    reinterpret_cast<void(__fastcall *)(int, int, float)>(Offsets::FUN_GX_RS_SET_FLOAT)(state, 0, value);
}
inline int GetState(int state) {
    int value = 0;
    reinterpret_cast<void(__fastcall *)(int, int *)>(Offsets::FUN_GX_RS_GET_INT)(state, &value);
    return value;
}

// ---- transform stacks -----------------------------------------------------------
// Each texture unit has a matrix stack, and stack 8 is the world transform.

inline void PushTransform(int unit) {
    reinterpret_cast<void(__fastcall *)(int)>(Offsets::FUN_GX_TEXMTX_PUSH)(unit);
}
inline void PushTransform(int unit, const float *m) {
    reinterpret_cast<void(__fastcall *)(int, const float *)>(Offsets::FUN_GX_TEXMTX_PUSH_LOAD)(unit, m);
}
inline void PopTransform(int unit) {
    reinterpret_cast<void(__fastcall *)(int)>(Offsets::FUN_GX_TEXMTX_POP)(unit);
}
// Over the top of the unit's stack.
inline void SetTransform(int unit, const float *m) {
    reinterpret_cast<void(__fastcall *)(int, const float *)>(Offsets::FUN_GX_TEXMTX_LOAD)(unit, m);
}

// ---- clear ----------------------------------------------------------------------

inline uint32_t GetClearColor() {
    uint32_t argb = 0;
    reinterpret_cast<uint32_t *(__fastcall *)(uint32_t *)>(Offsets::FUN_GX_GET_CLEAR_COLOR)(&argb);
    return argb;
}
inline void SetClearColor(uint32_t argb) {
    reinterpret_cast<void(__stdcall *)(uint32_t)>(Offsets::FUN_GX_SET_CLEAR_COLOR)(argb);
}
// `mask` 1 the colour, 2 the depth.
inline void Clear(uint32_t mask) { reinterpret_cast<void(__fastcall *)(uint32_t)>(Offsets::FUN_GX_CLEAR)(mask); }

// The near and far plane points at the viewport fractions `a` (across) and
// `b` (up), through the device view and projection: relative to the eye when
// the view leaves it out, as a camera's does.
inline void UnprojectScreenPoint(float (&nearPoint)[3], float (&farPoint)[3], float a, float b) {
    using Fn = void(__fastcall *)(float *, float *, float, float);
    reinterpret_cast<Fn>(Offsets::FUN_UNPROJECT_SCREEN_POINT)(nearPoint, farPoint, a, b);
}

// The projection, view and viewport, put back when the scope ends.
class SavedTransforms {
  public:
    SavedTransforms() : viewport_(GetViewport()) {
        GetProjection(projection_);
        GetView(view_);
    }
    ~SavedTransforms() {
        SetProjection(projection_);
        SetView(view_);
        SetViewport(viewport_);
    }
    SavedTransforms(const SavedTransforms &) = delete;
    SavedTransforms &operator=(const SavedTransforms &) = delete;

    const Viewport &SavedViewport() const { return viewport_; }

  private:
    Matrix projection_;
    Matrix view_;
    Viewport viewport_;
};

} // namespace Gx
