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

#include "Offsets.h"
#include "animation/Object.h"
#include "model/Outfit.h"
#include "model/TryOn.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// The ModelScene frame (model/Scene.cpp) and its actors (model/SceneActor.cpp).
// docs/notes/model-scene.md has the plan, the retail contract and the probes.
namespace Model::Scene {

class Actor;
namespace World {
struct Ambience;
}

// The light of a spell's model in flight or left behind, as the world gives
// a missile or an effect a light node of its own: an entity standing on the
// WMO under it (World::Settle / ActorLight), else the frame's light.
struct EntityLight {
    explicit EntityLight(void *frame);
    ~EntityLight();
    EntityLight(const EntityLight &) = delete;
    EntityLight &operator=(const EntityLight &) = delete;
    void *frame;
    World::Ambience *ambience;
};

// One PlaySpellVisual on its caster (model/SceneSpell.cpp): the precast, the
// cast, then the missile in flight.
struct SpellCast {
    enum class Phase { Precast, Cast, Flight };
    uint32_t spellID = 0;
    // The visual the caster shows (UnitGetSpellVisual's 64 bytes: the
    // spell's record, a ranged spell's gaps filled from its weapon's).
    uint8_t visual[Offsets::SPELL_VISUAL_SIZE] = {};
    // The ammo the shot carries (SMSG_SPELL_GO's): an ItemDisplayInfo id and
    // inventory type, 0 for a spell that is not ranged.
    uint32_t ammoDisplay = 0;
    uint32_t ammoType = 0;
    Actor *target = nullptr;         // the caster for a cast on itself
    Phase phase = Phase::Precast;
    float precastLeftSec = 0.0f;
    std::vector<void *> precastEffects; // one reference each
    bool precastTinted = false;         // the precast kit set a tint or alpha
    void *missile = nullptr;            // one reference, from the cast on
    void *missileSound = nullptr;
    float queuedSec = 0.0f; // since the missile was made, until its release
    float speed = 0.0f;
    float position[3] = {};
    float destination[3] = {};
    int attachment = -1; // the target attachment the launch settled, or -1
    bool sticks = false; // shot from a bow, crossbow or thrown weapon
    std::unique_ptr<EntityLight> light; // the missile's
};

// A missile stuck in its target, until its time is up.
struct StuckMissile {
    void *model = nullptr; // one reference
    float leftSec = 0.0f;
};

// An impact model left at a missile's arrival, until its particles are gone.
struct ImpactModel {
    void *model = nullptr; // one reference
    int framesShown = 0;
    std::unique_ptr<EntityLight> light;
};

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
    // Hangs the hand lanes per MeleeSheathed and hideWeapons, and the ranged
    // weapon in hand while it is drawn.
    void ApplyWeapons();
    // A ranged spell's start (the unit's sheathe state 2): the melee items
    // on their sheath points, the ranged weapon in hand, until SetSheathed or
    // a new model.
    void DrawRanged();
    // Leaves sheathe state 2: the ranged weapon leaves the hand.
    void SheatheRanged();
    // Whether the hand lanes hang at their sheath points: sheathed, or the
    // ranged weapon drawn.
    bool MeleeSheathed() const { return sheathed || rangedDrawn; }
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
    // The CreatureDisplayInfo the model shows (a creature display's, or the
    // unit's UNIT_FIELD_DISPLAYID), for its sound data; 0 for a model file.
    uint32_t displayID = 0;
    bool playAnimationSounds = false; // SetPlayAnimationSounds
    bool shadowShown = false;         // SetShadowShown: a blob shadow under the model
    World::Ambience *ambience;        // its light in the scene's WMO (owned)
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
    bool rangedDrawn = false; // the unit's sheathe state 2
    // The ranged slot (a ClassicAPI extension): a unit's third held lane,
    // inventory slot 18, which every bow, gun, crossbow, wand and thrown
    // weapon takes. Its ItemDisplayInfo row, and its model (a unit's +0xD24,
    // one reference) while hung: in hand while drawn, else only at a sheath
    // point its type has.
    Outfit::HeldLane ranged = {};
    const void *rangedDisplay = nullptr;
    void *rangedModel = nullptr;
    // Per lane: the SetSheathedCategory category (Outfit::SheatheCategory),
    // and the sheathe type the lane's item has of its own. The lane itself
    // holds the type it hangs with.
    uint32_t sheatheCategory[2] = {};
    uint8_t ownSheathe[2] = {};
    bool useUnitSheatheCategory = false; // UseUnitSheatheCategory
    // The ammo tried on (an INVTYPE_AMMO item, as a player equips it): its
    // ItemDisplayInfo id, 0 for none.
    uint32_t ammoDisplay = 0;
    // Lane `i` (on `onModel`, the actor's model or the copy being built) was
    // just filled with its item's own type: hang it per its category.
    void TookLane(void *onModel, int i);
    // Hangs lane `i` per its category.
    void ApplySheatheCategory(void *onModel, int i);
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
    // ApplySpellVisualKit calls made before the model loaded: {kit, oneShot}.
    std::vector<std::pair<uint32_t, bool>> pendingKits;

    // A spell kit's animation, shown in place of `animation` (-1 none): a
    // precast's loops, a cast's or an impact's plays once.
    int kitAnimation = -1;
    bool kitAnimationLoops = false;
    // The precast's looping animation (-1 none), which a once-only kit
    // animation (a wound) gives back when it ends, as a unit's replays its
    // spell's looping kit animation.
    int kitLoopAnimation = -1;
    bool kitAnimationEnded = false; // a once-only kit animation ended this frame
    void PlayKitAnimation(int anim, bool loops);
    // The animation the body plays: a spell kit's, else the script's.
    int CurrentAnimation() const { return kitAnimation >= 0 ? kitAnimation : static_cast<int>(animation); }
    // A release event of the model this frame ($CSL / $CSR / $CST, or $BWR
    // from the bow or rifle release point), and its point.
    bool releaseEvent = false;
    float releasePoint[3] = {};
    // The ranged pose, as a unit keeps it: the ammo in hand (one reference,
    // nocked at attachment 35 while the bow is drawn), its display, and the
    // drawn bit.
    void *heldAmmo = nullptr;
    uint32_t heldAmmoDisplay = 0;
    bool bowDrawn = false;
    // The unit fields DrawBowstring reads (the model and the drawn bit), the
    // user data of a hung bow's string; each update, before the draw.
    alignas(4) uint8_t stringUnit[Offsets::OFF_CGUNIT_STATE_FLAGS + 4] = {};
    std::vector<SpellCast> spellCasts;
    std::vector<ImpactModel> impactModels;
    std::vector<StuckMissile> stuckMissiles; // the missiles stuck in this actor
};

// The spell visuals an actor casts (model/SceneSpell.cpp).
namespace Casts {
// Starts the cast; false when the spell, its visual or the caster's model is
// missing. `castTimeMs` < 0 takes the spell's cast time.
bool Play(Actor &caster, uint32_t spellID, Actor *target, int castTimeMs);
// Interrupts the casts still preparing; casts already made go on.
void Stop(Actor &caster);
// The model's $BWP: the bow draws and the ammo is nocked, while a ranged
// spell is cast.
void BowPull(Actor &caster);
// The model's $BWR: the weapon plays its release before the missiles go.
void BowRelease(Actor &caster);
// The animation a ranged load or hold goes on to when it ends, or -1 for
// none (the ranged shot cycle).
int RangedAnimationAfter(const Actor &caster, int ended);
// From the actor's update, after the scene advance: the casts move on.
void Update(Actor &caster, float elapsedSec);
// From the actor's Link: the released missiles and the impact models go on
// the scene's animate list.
void Link(Actor &caster);
// From the actor's Place: the missiles' placement.
void Place(Actor &caster);
// Drops every cast, missile and impact model of the caster.
void Drop(Actor &caster);
// Stops the missiles' loop sounds (the scene hid; its update stops).
void Silence(Actor &caster);
} // namespace Casts

// The actor model's animation events (model/SceneEvents.cpp).
namespace Events {
// The model event callback (Offsets::FUN_M2MODEL_SET_EVENT_CALLBACK), user
// data the actor: missile releases for Casts, and the event sounds.
void __stdcall OnModelEvent(uint32_t tag, uint32_t data, const float *point, uint32_t unknown,
                            uint32_t userLow, uint32_t userHigh);
} // namespace Events

// The scene's WMO (SetWorldModel; model/SceneWorld.cpp).
namespace World {
struct Backdrop;
// Whether `path` names a WMO (ends in ".wmo"), which only the scene shows.
bool IsWmoPath(const char *path);
// Null when the engine cannot read `path` safely: it is not a WMO, a file
// does not open, the root's chunks are not in the engine's order or it has
// more groups than the engine holds.
// `mapID` is the map whose light the outdoor water takes (Light.dbc at the
// scene's camera), or kNoMap for the world's light where the player is.
constexpr uint32_t kNoMap = 0xFFFFFFFFu;
Backdrop *Load(const char *path, const float (&placement)[16], uint32_t doodadSet, uint32_t mapID);
// The single WMO of a WMO-only map at the map's own placement; null for any
// other map.
Backdrop *LoadMap(uint32_t mapID);
// SetWorldModel's placement: at `position`, turned by `yaw` about z.
void Placement(const float (&position)[3], float yaw, float (&out)[16]);
void Free(Backdrop *backdrop);
// From the scene's update, before the scene advances: makes the doodads of
// each group parsed since, puts the ready ones on the animate list and
// moves their sounds with the camera, and moves the camera's WMO fog (the
// MFOG records near the camera, faded in and out as the world fades them).
// `sounds` is SetPlayWorldModelSounds.
void Update(Backdrop &backdrop, void *frame, bool sounds, float elapsed);
// Drops the doodads' loop sounds (the scene hid).
void Silence(Backdrop &backdrop);
// From the paint, after the animate pass: the WMO in the device's view.
void Draw(Backdrop &backdrop, void *frame, const float (&eye)[3]);
// From the paint, between the scene's opaque and translucent lists: the
// liquids of the WMO's groups (the world draws them between its model lists).
void DrawLiquids(Backdrop &backdrop, void *frame, const float (&eye)[3]);
// Whether the camera is in one of the WMO's liquids: the world then draws the
// liquids after the translucent models.
bool CameraWet(const Backdrop &backdrop);
// How the world lights a unit that stands in a WMO, kept per actor: the
// colour of the face under it, or the sun, and the WMO's lights near it.
struct Ambience;
Ambience *NewAmbience();
void FreeAmbience(Ambience *ambience);
// From the scene's update: places the model in the WMO again when it moved,
// or when more of the WMO has loaded since.
void Settle(const Backdrop &backdrop, Ambience &ambience, void *frame, void *model, float elapsed);
// The actor's lighting slots: the frame's light and fog without a WMO, or
// before the model is placed in this one.
void ActorLight(const Backdrop *backdrop, const Ambience &ambience, void *frame, void *record);
// The TerrainType id of the WMO face under the actor, or -1 (no face, no
// WMO, or not placed in this one).
int Ground(const Backdrop *backdrop, const Ambience &ambience);
// From the paint, after Draw: a texture laid over the WMO's faces in `box`
// (scene space), as the world lays a decal on its ground.
void DrawDecal(const Backdrop &backdrop, const float (&box)[6], uint32_t argb, const float (&m)[16], float fade,
               uint32_t mask, const float (&eye)[3]);
} // namespace World

// The scene's WMO, or null.
World::Backdrop *WorldOf(void *sceneFrame);

// The scene's sounds (model/SceneSound.cpp), heard from the scene camera:
// each plays where its scene point lies from the camera, turned into the
// sound listener's frame, so the engine's cutoff, falloff and panning apply
// as they would to a unit at that distance.
namespace Sounds {
// A scene point as a point of the listener's (the engine's axes).
void Heard(void *sceneFrame, const float (&point)[3], float (&out)[3]);
// Sound::Play::PlayKitIDAt, PlayKitLoopedAt and Move at a scene point.
void *Play(void *sceneFrame, int soundKitID, const float (&point)[3]);
void *Loop(void *sceneFrame, int soundKitID, const float (&point)[3]);
void Move(void *sceneFrame, void *sound, const float (&point)[3]);
} // namespace Sounds

// The scene camera (model/Scene.cpp).
struct View {
    float eye[3];
    float forward[3];
    float up[3];
    float farClip;
};
View CameraView(void *sceneFrame);

// The scene's ground terrain (SetGroundTerrain): a TerrainType id, or -1.
int GroundTerrain(void *sceneFrame);

// Registers the actor class in the current Lua state.
void RegisterActorClass();

// The methods' argument checks, for the frame and its actors. True when the
// `count` arguments from `first` are numbers; else the engine's usage error,
// "Usage: <owner>:<usage>".
bool NumberArgs(void *L, int first, int count, const char *owner, const char *usage);
float FloatArg(void *L, int index);

} // namespace Model::Scene
