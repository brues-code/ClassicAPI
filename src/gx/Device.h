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

namespace Gx {

using Matrix = float[16];

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
