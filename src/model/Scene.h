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
#include "model/SceneContact.h"
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
namespace Mover {
struct Body;
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

// The model lights as an entity of the scene's WMO through its render
// callback, with its own `light` (model/SceneSpell.cpp).
void LightAsEntity(void *model, std::unique_ptr<EntityLight> &light, void *frame);
// Gives the model the frame's light callback again, frees `light`, and
// hides and releases the model.
void ReleaseLit(void *model, std::unique_ptr<EntityLight> &light);
// The light's re-placement when the model moved, and its ease, each tick.
void SettleLight(void *model, EntityLight *light, float elapsed);

// One PlaySpellVisual on its caster (model/SceneSpell.cpp): the precast, the
// cast, then the missile in flight.
struct SpellCast {
    enum class Phase { Precast, Cast, Flight, Channel };
    uint32_t id = 0; // PlaySpellVisual's cast ID
    uint32_t spellID = 0;
    // The visual the caster shows (UnitGetSpellVisual's 64 bytes: the
    // spell's record, a ranged spell's gaps filled from its weapon's).
    uint8_t visual[Offsets::SPELL_VISUAL_SIZE] = {};
    // The ammo the shot carries (SMSG_SPELL_GO's): an ItemDisplayInfo id and
    // inventory type, 0 for a spell that is not ranged.
    uint32_t ammoDisplay = 0;
    uint32_t ammoType = 0;
    Actor *target = nullptr;         // the caster for a cast on itself, null at a point
    bool atPoint = false;            // cast at the ground point `destination`
    Phase phase = Phase::Precast;
    float precastLeftSec = 0.0f;
    std::vector<void *> precastEffects; // one reference each
    bool precastTinted = false;         // the precast kit set a tint or alpha
    // A lasting kit's loop: the animation it made the caster's loop (-1 for
    // none), and its looping sound with the sound's ID (0 for a sound played
    // once), for the precast and for the channel.
    struct KitLoop {
        int animation = -1;
        void *sound = nullptr;
        int soundID = 0;
    };
    KitLoop precastLoop;
    KitLoop channelLoop;
    void *missile = nullptr;            // one reference, from the cast on
    void *missileSound = nullptr;
    float queuedSec = 0.0f; // since the missile was made, until its release
    float speed = 0.0f;
    float position[3] = {};
    float destination[3] = {};
    int attachment = -1; // the target attachment the launch settled, or -1
    bool sticks = false; // shot from a bow, crossbow or thrown weapon
    std::unique_ptr<EntityLight> light; // the missile's
    // The channel after the cast (a channeled spell): its kit's effects, one
    // reference each, whether it tinted, and its time left (forever when
    // `channelEndless`).
    int channelMs = 0; // from Play: 0 for no channel, -1 for one without end
    std::vector<void *> channelEffects;
    bool channelTinted = false;
    float channelLeftSec = 0.0f;
    bool channelEndless = false;
};

// A cast that reached its target this frame, for the caster's
// OnSpellVisualImpact.
struct SpellImpact {
    uint32_t castID = 0;
    uint32_t spellID = 0;
    Actor *target = nullptr;
};

// A missile stuck in its target, until its time is up.
struct StuckMissile {
    void *model = nullptr; // one reference
    float leftSec = 0.0f;
};

// An impact model left at a missile's arrival, until its particles are gone;
// or the effect a spell plants at its ground point (`planted`), which plays
// its sequence once, hides when it ends (`ended`), and goes once its
// particles are gone.
struct ImpactModel {
    void *model = nullptr; // one reference
    int framesShown = 0;
    std::unique_ptr<EntityLight> light;
    bool planted = false;
    bool ended = false;
};

// A shard of a spell's ground area (model/SceneArea.cpp): one model of the
// emitter's file, played once where it fell, as the world's shard
// (0x006EBB30). Its callbacks hold its address.
struct AreaShard {
    void *frame = nullptr;
    void *model = nullptr; // one reference
    float position[3] = {};
    uint32_t startMs = 0; // the scene clock at which it starts
    bool started = false;
    bool done = false; // its animation ended
    std::unique_ptr<EntityLight> light;
};

// An aura the actor shows (AddAuraVisual, model/SceneAura.cpp): the state kit
// of its spell's visual, held while the aura lasts.
struct AuraVisual {
    uint32_t spellID = 0;
    const uint8_t *kit = nullptr; // the SpellVisualKit record
    bool shown = false;           // on the loaded model
    std::vector<void *> effects;  // one reference each
    // The kit's world effect, planted where the actor stood (turned by its
    // facing, scaled by its scale), following it for a spell that roots.
    void *worldEffect = nullptr; // one reference
    std::unique_ptr<EntityLight> worldLight;
    float worldPosition[3] = {};
    float worldYaw = 0.0f;
    float worldScale = 1.0f;
    bool worldFollows = false;
    // The kit's sound when it loops (Sounds::KitLoops): its ID, and the loop
    // playing at the actor (null while the scene is paused or hidden).
    int loopSoundID = 0;
    void *loopSound = nullptr;
};

// The area kit of a spell's ground area while it runs: the shard emitter
// (0x006EB930) and the kit's looping sound.
struct AreaEmitter {
    bool running = false;
    const char *path = nullptr; // the shard file, null for a kit without an emitter
    float rate = 0.0f;          // shards a second
    double accumulator = 0.0;
    int32_t soundID = 0;
    void *loopSound = nullptr;
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
    // `elapsedSec` is the frame's time (OnUpdate), `sceneSec` the time the
    // scene clock moved (casts; 0 while the scene is paused).
    void Update(float elapsedSec, float sceneSec);
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
    // SetModelBySpellArea: the ground area of a spell, as the world shows a
    // dynamic object (CGDynamicObject_C). Its SpellVisualEffectName row and
    // its radius in yards; null and 0 for any other model.
    const uint8_t *areaEffect = nullptr;
    float areaRadius = 0.0f;
    const uint8_t *areaVisual = nullptr; // the spell's SpellVisual row
    // The area's kit, and its shards, which play out after the area goes.
    AreaEmitter areaEmitter;
    std::vector<std::unique_ptr<AreaShard>> areaShards;
    bool shadowShown = false;         // SetShadowShown: a blob shadow under the model
    // SetHighlight: a unit's light bits (CGObject +0xB8, Offsets
    // CGOBJECT_LIGHT_BIT_*); while any is set the model glows in the scene
    // light's ambient colour.
    uint32_t highlightBits = 0;
    // SetSelectionCircle: a selection circle under the model, in this ARGB.
    bool selectionCircleShown = false;
    uint32_t selectionCircle = 0;
    // SetGroundReticle: the world's ground-target reticle at the actor's
    // position: its radius as given (0 takes the world's default) and
    // whether it shows the in-range texture.
    bool groundReticleShown = false;
    float groundReticleRadius = 0.0f;
    bool groundReticleValid = true;
    // The model's emissive colour for the highlight bits.
    void ApplyHighlight();
    // SetNameText: the name over the head, drawn as the world draws a unit's
    // (UpdatePlate 0x006C6E90) from the model's draw callback. Its text and
    // ARGB colour; the font string, made at the next draw while nameDirty,
    // with the font that made it (the world's name font, destroyed at
    // logout before the actors); and the block height, the extra lines times
    // the plate scale at that draw.
    std::string nameText;
    uint32_t nameColor = 0xFFFFFFFFu;
    void *nameString = nullptr;
    const void *nameFont = nullptr;
    bool nameDirty = false;
    float nameBlockHeight = 0.0f;
    // GetNamePosition (0x00608640) for the model, in scene coordinates;
    // false without a loaded model.
    bool NamePosition(float (&out)[3]) const;
    // From the model's draw callback: UpdatePlate's name half.
    void DrawName();
    void DropNameString();
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

    // The client's mover (SetMovementEnabled, a ClassicAPI extension): while
    // set, it moves `position` and turns `yaw` through the scene's WMO.
    std::unique_ptr<Mover::Body> mover;
    // A unit's facings while the mover is on (CGUnit_C +0xC98, +0xC94): the
    // display facing the body turns toward, the body facing the model is
    // drawn at, the time the display last stood still (+0xCB0), and the body
    // pass's bits (+0xD58): 0x1 turning, 0x80 and 0x100 the model's twist
    // bones, marked for `bonesOf` once it has loaded. `shuffle` is the pass's
    // 0x800 (1) or 0x1000 (-1): the body caught up turning left or right on
    // the spot this frame.
    struct Facings {
        float display = 0.0f;
        float body = 0.0f;
        uint32_t stamp = 0;
        uint32_t bits = 0;
        int shuffle = 0;
        const void *bonesOf = nullptr;
    } facings;
    // The yaw the model is drawn at: the body facing while the mover is on.
    float ShownYaw() const { return mover != nullptr ? facings.body : yaw; }

    float position[3] = {0.0f, 0.0f, 0.0f};
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    float scale = 1.0f;
    float alpha = 1.0f;
    bool shown = true;
    // FadeOut: the world's fade of a removed object's model, on the scene
    // clock from fadeStartMs; the actor hides when it ends.
    bool fadingOut = false;
    uint32_t fadeStartMs = 0;
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
    // The kit's world effect (OFF_SPELLVISUALKIT_WORLD_EFFECT): a model of its
    // own in the scene, planted where the actor stood, one reference, lit as
    // an entity; `kitWorldDone` once a one-shot effect has ended.
    void *kitWorldEffect = nullptr;
    float kitWorldPosition[3] = {0.0f, 0.0f, 0.0f};
    float kitWorldYaw = 0.0f;   // the actor's facing and scale when it was planted
    float kitWorldScale = 1.0f;
    std::unique_ptr<EntityLight> kitWorldLight;
    bool kitWorldDone = false;
    // ApplySpellVisualKit calls made before the model loaded: {kit, oneShot}.
    std::vector<std::pair<uint32_t, bool>> pendingKits;
    // The auras shown (AddAuraVisual), one per spell, in the order added.
    std::vector<AuraVisual> auraVisuals;

    // A spell kit's animation, shown in place of `animation` (-1 none): a
    // precast's loops, a cast's or an impact's plays once.
    int kitAnimation = -1;
    bool kitAnimationLoops = false;
    // The precast's looping animation (-1 none), which a once-only kit
    // animation (a wound) gives back when it ends, as a unit's replays its
    // spell's looping kit animation.
    int kitLoopAnimation = -1;
    // A cast animation ended or was replaced this frame (the missile release
    // test, M2::IsMissileReleaseAnimation): the queued missiles go.
    bool castAnimationEnded = false;
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
    // The casts that reached their target, in order, until the update runs
    // OnSpellVisualImpact for them: outside the casts' step, which a script
    // that casts or stops a spell would change under it.
    std::vector<SpellImpact> spellImpacts;
    // The moves (MoveTo) the mover reported done, until the update runs
    // OnMoveFinished for them.
    uint32_t movesFinished = 0;
    std::vector<ImpactModel> impactModels;
    std::vector<StuckMissile> stuckMissiles; // the missiles stuck in this actor
};

// The spell visuals an actor casts (model/SceneSpell.cpp).
namespace Casts {
// Starts the cast and returns its ID (from 1); 0 when the spell, its visual
// or the caster's model is missing. `castTimeMs` < 0 takes the spell's cast
// time.
// channelMs: a channeled spell's channel time, -1 for its duration.
uint32_t Play(Actor &caster, uint32_t spellID, Actor *target, int castTimeMs, int channelMs);
// The same cast at a ground point of the scene instead of an actor.
uint32_t PlayAtPoint(Actor &caster, uint32_t spellID, const float (&point)[3], int castTimeMs, int channelMs);
// Interrupts the casts still preparing; casts already made go on.
// With `channels`, the channels end too; with a `castID`, only that cast.
void Stop(Actor &caster, bool channels, uint32_t castID = 0);
// The model's $BWP: the bow draws and the ammo is nocked, while a ranged
// spell is cast.
void BowPull(Actor &caster);
// The model's $BWR: the weapon plays its release before the missiles go.
void BowRelease(Actor &caster);
// The animation a ranged load or hold goes on to when it ends, or -1 for
// none (the ranged shot cycle).
int RangedAnimationAfter(const Actor &caster, int ended);
// EndRangedPose (0x0060F530): the bow is no longer drawn, the ammo leaves the
// hand, and the weapon stands again unless it plays its release.
void EndRangedPose(Actor &actor);
// From the actor's update, after the scene advance: the casts move on.
void Update(Actor &caster, float elapsedSec);
// From the actor's Link: the released missiles and the impact models go on
// the scene's animate list.
void Link(Actor &caster);
// From the actor's Place: the missiles' placement.
void Place(Actor &caster);
// Drops every cast, missile and impact model of the caster.
void Drop(Actor &caster);
// A planted effect of the caster's stops (CEffect::Stop 0x006203E0, after
// 159 Decay when its model has it, 0x00614150) and joins the impact list,
// which frees it once its particles are gone. Takes the model's reference.
void Retire(Actor &caster, void *model, std::unique_ptr<EntityLight> light);
// Stops the missiles' loop sounds (the scene hid; its update stops).
void Silence(Actor &caster);
} // namespace Casts

// The byte a spell visual kit procedure keeps in a float value: the bits of
// the value + 512.0, shifted right 14 (the engine's decode, 0x005D5665 and
// 0x0060DB19; model/SceneArea.cpp).
uint8_t DecodeKitByte(float value);

// The chain effects of a scene's casts (model/SceneChain.cpp): Chain
// Lightning's bolts, drawn by a lightning renderer the scene owns.
namespace Chains {
// From a cast's start, or a channel's (`channelCastID` its cast ID): the
// kit's chain procedures (kinds 0 and 12) draw a bolt from the caster to the
// target. A persistent chain of a channel lasts until End.
void Start(Actor &caster, Actor &target, const uint8_t *visual, const uint8_t *kit, uint32_t channelCastID = 0);
// The channel `castID` of `caster` ends: its persistent chains go.
void End(Actor &caster, uint32_t castID);
// From the scene's update, after the actors': the hops move with their
// ends and end on the scene clock; the bolts animate.
void Update(void *frame, float sceneSec);
// From the scene's paint, after the models: draws the bolts.
void Render(void *frame, const float (&eye)[3]);
// The actor goes: its hops go with it.
void Forget(Actor &actor);
// The scene goes.
void Destroy(void *frame);
} // namespace Chains

// The auras an actor shows (model/SceneAura.cpp).
namespace AuraVisuals {
// The spell's aura joins the actor's, its state kit shown once the model has
// loaded. False when the spell's visual has no state kit.
bool Add(Actor &actor, uint32_t spellID);
// The spell's aura goes (0 for every aura): its effects decay or stop.
void Remove(Actor &actor, uint32_t spellID);
// The model loaded: the auras show on it.
void Show(Actor &actor);
// The model goes: the auras' effects go with it, the auras stay.
void Drop(Actor &actor);
// The tint and alpha of the auras shown, after a kit reset the model's.
void ApplyColors(Actor &actor);
// The animation the newest aura shown pins while the actor stands (a state
// kit's, ApplyEffectAnimation 0x005FDE80), or -1.
int PinnedAnimation(const Actor &actor);
// From the actor's update, link and place: the world effects' light,
// visibility and placement, and the loop sounds' place and pause.
void Update(Actor &actor, float elapsedSec);
void Link(Actor &actor);
void Place(Actor &actor);
// Stops the loop sounds (the scene hid; its update stops). The update starts
// them again.
void Silence(Actor &actor);
} // namespace AuraVisuals

// The area kit of an actor that shows a spell's ground area
// (model/SceneArea.cpp): its shard emitter and its looping sound.
namespace Areas {
// From the actor's update, after the scene advance: the kit starts while the
// actor shows an area and is shown, and stops otherwise; shards fall and
// the finished ones go.
void Update(Actor &actor, float sceneSec);
// From the actor's Link: the shards whose start came go on the animate list.
void Link(Actor &actor);
// From the actor's Place: the shards' placement and the loop sound's.
void Place(Actor &actor);
// The kit stops (the model changed): no new shards, the loop fades.
void Stop(Actor &actor);
// Stops the kit and frees every shard (the actor goes).
void Drop(Actor &actor);
// Stops the loop sound (the scene hid; its update stops). It loops again
// at the next update.
void Silence(Actor &actor);
} // namespace Areas

// The unit's movement animations (model/SceneLocomotion.cpp): an actor with
// the mover on picks and plays them as the client's unit does from its own
// mover. Each is a no-op for an actor without the mover, without a loaded
// model, or dead (its own animation a death animation).
namespace Locomotion {
// The movement messages a unit animates (AnimateMoveOpcode 0x0060E480), by
// their 1.12 opcodes.
enum class Move : uint32_t {
    StartForward = 0xB5,
    StartBackward = 0xB6,
    Stop = 0xB7,
    StartStrafeLeft = 0xB8,
    StartStrafeRight = 0xB9,
    StopStrafe = 0xBA,
    Jump = 0xBB,
    StartTurnLeft = 0xBC,
    StartTurnRight = 0xBD,
    StopTurn = 0xBE,
    RunMode = 0xC2,
    WalkMode = 0xC3,
    TeleportAck = 0xC7,
    StartSwim = 0xCA,
    StopSwim = 0xCB,
    KnockBack = 0xF0, // CMSG_MOVE_KNOCK_BACK_ACK, after the launch
};
// A movement message for a command the mover took, or a change the actor
// made (ProcessLocalMoveEvent 0x0060E0A0 -> AnimateMoveOpcode).
void OnMove(Actor &actor, Move move);
// The mover's substep hook (Mover::Body::stepped), context the actor: an
// arrival at a spline's end, a landing, a fall or a swim's end.
void OnStep(void *actor, uint32_t oldFlags, bool jumped, bool arrived);
// The body pass's shuffle (the tail of CGUnit_C::Update 0x00607ED0), `turn`
// the turn the body made this frame.
void OnBodyTurn(Actor &actor, float turn);
// The actor's own animation ended: plays what follows it
// (PlayFollowOnAnimation 0x005FC3F0) and returns true; false when the chain
// does not run, and the caller replays it.
bool FollowOn(Actor &actor);
// Picks again (UpdateBaseAnimation 0x005FD9E0).
void Repick(Actor &actor);
} // namespace Locomotion

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
// From the paint, in place of its depth clear: the world's frame clear and,
// with the camera outside the WMO's interior groups and dry, the world's sky
// (the dome, the sun and moons, the stars, the clouds and the skyboxes) in
// the device's view, lit by the backdrop map's light at the camera.
void ClearAndDrawSky(const Backdrop &backdrop, void *frame, const float (&eye)[3]);
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
// The nearest face of the loaded groups that the segment from `start` to
// `end` (scene space) crosses, of the faces the world's line of sight tests,
// or with `camera` the faces its camera collides with: its fraction along the
// segment and its unit normal, turned to face `start`. False on a miss or
// before the WMO has loaded.
bool Trace(const Backdrop &backdrop, const float (&start)[3], const float (&end)[3], bool camera,
           float &fraction, float (&normal)[3]);
// The liquid a point (scene space) is in: the type (0 water, 1 ocean, 2
// magma, 3 slime) and the surface's z, FLT_MAX for a group that is liquid
// throughout. False when the point is not under a liquid surface of the
// group below it, or before the WMO has loaded.
bool LiquidAt(const Backdrop &backdrop, const float (&point)[3], uint32_t &type, float &surface);
// The world query (0x006721B0) a scene body's sweeps make, for
// Contact::Facets: the WMO's collision faces in a box (scene space), then
// its doodads' collision meshes. False without a WMO, before it has loaded,
// or while a group or doodad in the box is still loading, so a body there
// does not move.
bool QueryFacets(void *backdrop, const float (&box)[6], std::vector<Contact::Facet> &out);
// The same query with a swimmer's class (0x30000), for Contact::Facets: the
// water and ocean surfaces of the WMO's liquid groups in a box (scene space),
// as their tiles' triangles. False without a WMO or before it has loaded.
bool QueryLiquidFacets(void *backdrop, const float (&box)[6], std::vector<Contact::Facet> &out);
// Sweeps a body by `move` to its first contact with the WMO's collision
// faces, as the client's mover sweeps a unit's volume (CMovement::SweepVolume
// 0x00632BA0, SceneContact.h): a box `radius` from `feet` on each side and
// `height` tall over a downward point at `feet`. `end` is where the feet
// stop; true when a face was met, `normal` the facet's plane normal (scene
// space). The feet stay put before the WMO has loaded.
bool Sweep(const Backdrop &backdrop, const float (&feet)[3], float radius, float height, const float (&move)[3],
           float (&end)[3], float (&normal)[3]);
// From the paint, after Draw: a texture laid over the WMO's faces in `box`
// (scene space), as the world lays a decal on its ground.
void DrawDecal(const Backdrop &backdrop, const float (&box)[6], uint32_t argb, const float (&m)[16], float fade,
               uint32_t mask, const float (&eye)[3]);
// The same decal in a scene without a WMO: laid on the flat ground at
// height `z`, one quad over the box.
void DrawDecalOnPlane(const float (&box)[6], uint32_t argb, const float (&m)[16], float fade, float z,
                      const float (&eye)[3]);
// The scene's fog on the device, for a decal at `point` that the world draws
// in its own fog: the WMO's, or the frame's without one. Inside a state push.
void CommitDecalFog(const Backdrop *backdrop, void *frame, const float (&point)[3], const float (&eye)[3]);
} // namespace World

// The scene's WMO, or null.
World::Backdrop *WorldOf(void *sceneFrame);

// The scene light's ambient colour {r, g, b}: the colour an actor's highlight
// takes, as a unit's takes the world light state's ambient.
const float *LightAmbient(void *sceneFrame);

// The scene clock in milliseconds (the M2 scene's): the actors' movement runs
// on it as a unit's runs on the OS clock, so it stops while the scene is
// paused or hidden, with the animations.
uint32_t ClockMs(void *sceneFrame);
// Whether SetPaused holds the scene.
bool IsPaused(void *sceneFrame);
// The radius the actor's ground-target reticle draws at: its own, or the
// world's default for 0 (Offsets::RETICLE_DEFAULT_RADIUS).
float GroundReticleRadius(const Actor &actor);

// The actor the scene's sounds are heard at (SetSoundListenerActor), or null.
Actor *SoundListener(void *sceneFrame);

// The scene's sounds (model/SceneSound.cpp), heard from the scene camera:
// each plays where its scene point lies from the camera, turned into the
// sound listener's frame, so the engine's cutoff, falloff and panning apply
// as they would to a unit at that distance.
namespace Sounds {
// A scene point as a point of the listener's (the engine's axes), heard at
// the scene's listener actor while SoundListenerAtCharacter is on, else at
// the scene camera.
void Heard(void *sceneFrame, const float (&point)[3], float (&out)[3]);
// Sound::Play::PlayKitIDAt, PlayKitLoopedAt and Move at a scene point.
void *Play(void *sceneFrame, int soundKitID, const float (&point)[3]);
void *Loop(void *sceneFrame, int soundKitID, const float (&point)[3]);
void Move(void *sceneFrame, void *sound, const float (&point)[3]);
// A sound's fade when its effect or missile stops: CEffect::Stop's `push
// 0x3E19999A` before Sound::Stop2 (0x006204B6) and the missile's free
// (0x0061CD70).
constexpr float kStopFadeSec = 0.15f;
// Whether a lasting kit (kinds 2 to 4) loops the sound rather than playing
// it once: bit 0x200 of its SoundEntries flags (FUN_SOUND_DEFINITION_KIT_LOOP).
bool KitLoops(int soundID);
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
