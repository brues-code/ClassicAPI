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

// The water effects the world shows around its liquids, for a scene's camera
// (benilla terrain/waterfx, terrain/liquid#drift-cloud).
//
// The drift cloud: the motes the camera sees in a liquid. The world builds one
// at start-up (0x0066F971: 0xFA40 bytes, CreateDriftCloud 0x0068E5A0 with the
// size base 1/36, the edge 30.0 and Textures\WaterPoop02.blp) and drives it
// from its camera: CameraEnvironment's change leg (0x00680AD9) scatters it in
// the new liquid's mode and sets its size base when the camera's liquid
// changes to water or ocean (1/36) or magma (1/9), and disables it in slime;
// each world frame advects it with the camera's step and its gust
// (0x0066FEE0 to AdvectDriftCloud 0x0068E930), and the world stage draws it
// after the liquids (0x006701E0 to DrawDriftCloud 0x0068EFE0), both while
// the camera is in a liquid and world flag 0x2000000 (waterParticulates) is
// set. A scene owns a cloud of its own, made the same way the first time its
// camera enters a liquid, and runs the same steps from its own camera: the
// advect reads the camera position and the frame seconds from the world's
// globals, so the scene's are staged in them around it. The draw works in
// the device's view, which the scene's paint has set.

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "gx/Device.h"
#include "gx/Matrix.h"

#include <cstdint>

namespace Model::Scene::Water {

struct alignas(16) DriftMemory {
    uint8_t bytes[Offsets::DRIFT_CLOUD_SIZE];
};

struct Effects {
    DriftMemory *drift = nullptr; // made by CreateDriftCloud, or null
};

namespace {

using This_t = void(__thiscall *)(void *);
using Word_t = void(__thiscall *)(void *, uint32_t);

bool Particulates() {
    return (Game::Read<uint32_t>(Offsets::VAR_WORLD_FLAGS) & Offsets::WORLD_FLAG_PARTICULATES) != 0;
}

void *Drift(Effects &e) {
    if (e.drift == nullptr) {
        using Create_t = void(__thiscall *)(void *, uint32_t sizeBase, uint32_t edge, const char *texture);
        e.drift = new DriftMemory();
        reinterpret_cast<Create_t>(Offsets::FUN_CREATE_DRIFT_CLOUD)(
            e.drift, Offsets::DRIFT_SIZE_BASE_WATER, Offsets::DRIFT_EDGE,
            reinterpret_cast<const char *>(static_cast<uintptr_t>(Offsets::VAR_DRIFT_TEXTURE_PATH)));
    }
    return e.drift;
}

} // namespace

void Update(Effects *&effects, uint32_t before, uint32_t now, const float (&eye)[3], float elapsed) {
    // CameraEnvironment's change leg (0x00680AD9): by the new liquid & 3.
    if (now != Offsets::LIQUID_DRY) {
        const uint32_t kind = now & Offsets::LIQUID_TYPE_MASK;
        if (kind == Offsets::LIQUID_SLIME) {
            if (effects != nullptr && effects->drift != nullptr)
                Game::Ref<uint8_t>(effects->drift, Offsets::OFF_DRIFT_CLOUD_ENABLED) = 0;
        } else if (now != before && Particulates()) {
            if (effects == nullptr)
                effects = new Effects();
            void *drift = Drift(*effects);
            reinterpret_cast<Word_t>(Offsets::FUN_SCATTER_DRIFT_CLOUD)(drift, now);
            reinterpret_cast<Word_t>(Offsets::FUN_SET_DRIFT_CLOUD_SIZE_BASE)(
                drift, kind == Offsets::LIQUID_MAGMA ? Offsets::DRIFT_SIZE_BASE_MAGMA : Offsets::DRIFT_SIZE_BASE_WATER);
            Game::Ref<uint8_t>(drift, Offsets::OFF_DRIFT_CLOUD_ENABLED) = 1;
        }
    }
    // The world frame's advect (0x0066FEE0), from the scene's camera and
    // frame time.
    if (effects == nullptr || effects->drift == nullptr || !Particulates() || now == Offsets::LIQUID_DRY)
        return;
    float *camera = &Game::Ref<float>(Offsets::VAR_CAMERA_POSITION);
    float &seconds = Game::Ref<float>(Offsets::VAR_FRAME_SECONDS);
    const float savedCamera[3] = {camera[0], camera[1], camera[2]};
    const float savedSeconds = seconds;
    for (int i = 0; i < 3; ++i)
        camera[i] = eye[i];
    seconds = elapsed;
    reinterpret_cast<This_t>(Offsets::FUN_ADVECT_DRIFT_CLOUD)(effects->drift);
    for (int i = 0; i < 3; ++i)
        camera[i] = savedCamera[i];
    seconds = savedSeconds;
}

void DrawDrift(const Effects *effects, uint32_t cameraLiquid) {
    // The world stage's particulates (0x006701E0).
    if (effects == nullptr || effects->drift == nullptr || !Particulates() || cameraLiquid == Offsets::LIQUID_DRY)
        return;
    // The draw's quads are in view space; the world's stage reaches it with
    // the world transform identity.
    Gx::Matrix identity;
    Gx::Identity(identity);
    Gx::PushTransform(Offsets::GX_XFORM_WORLD, identity);
    reinterpret_cast<This_t>(Offsets::FUN_DRAW_DRIFT_CLOUD)(effects->drift);
    Gx::PopTransform(Offsets::GX_XFORM_WORLD);
}

void Free(Effects *effects) {
    if (effects == nullptr)
        return;
    if (effects->drift != nullptr) {
        reinterpret_cast<This_t>(Offsets::FUN_FREE_DRIFT_CLOUD_TEXTURE)(effects->drift);
        delete effects->drift;
    }
    delete effects;
}

} // namespace Model::Scene::Water
