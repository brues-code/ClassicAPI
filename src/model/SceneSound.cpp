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

// The scene's sounds, heard from the scene camera. The engine has one
// listener, which the world camera places; a scene is a second camera that
// the sound engine knows nothing of. So a scene point is played at the spot
// that lies from the listener as the point lies from the scene camera: its
// offset along the camera's forward, up and side becomes the same offset
// along the listener's. The engine then applies the row's cutoff, minimum
// distance, falloff and panning exactly as it does for a unit that far away.
//
// The listener is read back from FMOD (FSOUND_3D_Listener_GetAttributes), in
// the engine's axes (FMOD's (-y, z, x), Sound::SetListener 0x007A5EB0). A
// point is mapped when it plays; a loop is mapped again each time it moves.

#include "model/Scene.h"

#include "Offsets.h"
#include "sound/Play.h"

#include <cmath>
#include <cstring>

namespace Model::Scene::Sounds {

namespace {

// Three orthonormal axes at a point: the scene camera's, or the listener's.
struct Frame3 {
    float origin[3];
    float axes[3][3]; // forward, up, side
};

void Normalize(float (&v)[3]) {
    const float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (n > 0.0f)
        for (float &c : v)
            c /= n;
}

void Cross(const float (&a)[3], const float (&b)[3], float (&out)[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

float Dot(const float (&a)[3], const float (&b)[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// Forward, up and side from a forward and an up that need not be square.
Frame3 MakeFrame(const float (&origin)[3], const float (&forward)[3], const float (&up)[3]) {
    Frame3 f{};
    std::memcpy(f.origin, origin, sizeof f.origin);
    std::memcpy(f.axes[0], forward, sizeof f.axes[0]);
    Normalize(f.axes[0]);
    Cross(f.axes[0], up, f.axes[2]);
    Normalize(f.axes[2]);
    Cross(f.axes[2], f.axes[0], f.axes[1]);
    return f;
}

Frame3 ListenerFrame() {
    using Get_t = void(__stdcall *)(float *pos, float *vel, float *fx, float *fy, float *fz, float *tx, float *ty,
                                    float *tz);
    float pos[3], vel[3], fwd[3], top[3];
    reinterpret_cast<Get_t>(Offsets::FUN_FSOUND_LISTENER_GET_ATTRIBUTES)(pos, vel, &fwd[0], &fwd[1], &fwd[2],
                                                                         &top[0], &top[1], &top[2]);
    const auto engine = [](const float (&v)[3], float (&out)[3]) {
        out[0] = v[2];
        out[1] = -v[0];
        out[2] = v[1];
    };
    float p[3], f[3], u[3];
    engine(pos, p);
    engine(fwd, f);
    engine(top, u);
    return MakeFrame(p, f, u);
}

} // namespace

void Heard(void *sceneFrame, const float (&point)[3], float (&out)[3]) {
    const View view = CameraView(sceneFrame);
    const Frame3 ear = MakeFrame(view.eye, view.forward, view.up);
    const Frame3 listener = ListenerFrame();
    const float offset[3] = {point[0] - ear.origin[0], point[1] - ear.origin[1], point[2] - ear.origin[2]};
    for (int i = 0; i < 3; ++i)
        out[i] = listener.origin[i];
    for (int axis = 0; axis < 3; ++axis) {
        const float along = Dot(offset, ear.axes[axis]);
        for (int i = 0; i < 3; ++i)
            out[i] += along * listener.axes[axis][i];
    }
}

void *Play(void *sceneFrame, int soundKitID, const float (&point)[3]) {
    float heard[3];
    Heard(sceneFrame, point, heard);
    return Sound::Play::PlayKitIDAt(soundKitID, heard);
}

void *Loop(void *sceneFrame, int soundKitID, const float (&point)[3]) {
    float heard[3];
    Heard(sceneFrame, point, heard);
    return Sound::Play::PlayKitLoopedAt(soundKitID, heard);
}

void Move(void *sceneFrame, void *sound, const float (&point)[3]) {
    if (sound == nullptr)
        return;
    float heard[3];
    Heard(sceneFrame, point, heard);
    Sound::Play::Move(sound, heard);
}

} // namespace Model::Scene::Sounds
