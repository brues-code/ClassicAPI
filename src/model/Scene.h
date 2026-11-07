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

#include "animation/Object.h"

#include <cstdint>
#include <string>

// The ModelScene frame (model/Scene.cpp) and its actors (model/SceneActor.cpp).
// docs/notes/model-scene.md has the plan, the retail contract and the probes.
namespace Model::Scene {

// A ModelSceneActor: one model in its scene frame's M2 scene, placed by the
// scene's paint. A script object (Animation::Object) whose parent is the scene
// frame, which owns it.
class Actor : public Animation::Object {
  public:
    static Animation::Class s_class;

    Actor(void *scene, const char *name);
    ~Actor();

    void *SceneFrame() const { return parent; }

    // From the scene frame's update walk: load and dress state, scripts.
    void Update(float elapsedSec);
    // From the scene's paint, before the scene animates: the model's
    // placement matrix.
    void Place();
    // The scene frame's alpha changed.
    void ApplyAlpha();

    // Shows `model` (one reference, which the actor takes over), or nothing.
    void SetModel(void *model, const char *path, const uint8_t *dressRecord);
    void ClearModel();
    void PlayAnimation();

    void *model = nullptr;
    std::string modelPath;
    const uint8_t *dressRecord = nullptr; // CreatureDisplayInfoExtra, until dressed
    void *dresser = nullptr;
    bool loaded = false; // OnModelLoaded has run for the model
    bool animFinished = false;

    float position[3] = {0.0f, 0.0f, 0.0f};
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    float scale = 1.0f;
    float alpha = 1.0f;
    bool shown = true;
    bool useCenterForOrigin[3] = {false, false, false};
    uint32_t animation = 0;
    int variation = -1;
    float animSpeed = 1.0f;
    float animOffsetSec = 0.0f;
};

// Registers the actor class in the current Lua state.
void RegisterActorClass();

} // namespace Model::Scene
