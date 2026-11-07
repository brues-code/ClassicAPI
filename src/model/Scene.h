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
#include "model/Outfit.h"
#include "model/TryOn.h"

#include <cstdint>
#include <string>
#include <vector>

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

    // From the scene frame's update, before the scene clock advances: puts a
    // ready model on the scene's animate list.
    void Link();
    // From the scene frame's update walk: load and dress state, scripts.
    void Update(float elapsedSec);
    // From the scene's paint, before the scene animates: the model's
    // placement matrix.
    void Place();
    // The scene frame's alpha changed.
    void ApplyAlpha();

    // Shows `model` (one reference, which the actor takes over), or nothing.
    void SetModel(void *model, const char *path, const uint8_t *dressRecord);
    // Shows a copy of the unit's model, dressed and armed per autoDress,
    // sheathed and hideWeapons; false (and no model) when it has none.
    bool SetUnitModel(uint64_t guid);
    // Hangs the held items per sheathed and hideWeapons.
    void ApplyWeapons();
    // Puts the item on the model (Outfit::TryOn), once the model has loaded
    // and the item's record is cached; returns the ItemTryOnReason.
    int TryOnItem(const Model::TryOn::Request &request);
    void ClearModel();
    void PlayAnimation();
    // Shows the kit (0 for none) on the model, replacing the one shown.
    void SetSpellVisualKit(uint32_t kitID, bool oneShot);
    // Loads the actor's XML element (an <Actor> template), then runs OnLoad.
    void LoadXml(const void *node, void *status);

    void *model = nullptr;
    std::string modelPath;
    const uint8_t *dressRecord = nullptr; // CreatureDisplayInfoExtra, until dressed
    float displayScale = 1.0f; // a creature display's CreatureModelScale, else 1
    // The character component dressing the model: a display's dress, or the
    // clone of a unit's.
    void *dresser = nullptr;
    // A unit's model (SetModelByUnit): the unit and what the copy holds.
    uint64_t unitGuid = 0;
    Outfit::Lanes lanes = {};
    Outfit::Weapons weapons;
    Outfit::Worn worn = {}; // what each inventory slot wears (GetItemTransmogInfoList)
    bool autoDress = true;
    bool sheathed = false;
    bool hideWeapons = false;
    // DressUpModel's ready byte: the model may take armor (not a shapeshifted
    // unit's).
    bool armorReady = true;
    std::vector<Model::TryOn::Request> pendingTryOns; // waiting for the model or the record
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
    uint32_t blendOperation = 1; // Enum.ModelBlendOperation: 0 None, 1 Anim
    bool paused = false;
    bool globalPaused = false;
    int heldPositionMs = -1; // the paused frame; -1 until read from a loaded model
    uint32_t spellVisualKit = 0;
    bool spellVisualKitOneShot = false;
    std::vector<void *> kitEffects; // the kit's effect models, one reference each
};

// Registers the actor class in the current Lua state.
void RegisterActorClass();

// The methods' argument checks, for the frame and its actors. True when the
// `count` arguments from `first` are numbers; else the engine's usage error,
// "Usage: <owner>:<usage>".
bool NumberArgs(void *L, int first, int count, const char *owner, const char *usage);
float FloatArg(void *L, int index);

} // namespace Model::Scene
