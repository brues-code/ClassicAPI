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

// A game object's model and its state animations on an actor
// (SetModelByGameObjectDisplayID, SetGameObjectState), as the world's
// CGGameObject_C and its family A handlers (doors, chests, buttons, traps
// and the other animated types) play them:
//
// - A change of GAMEOBJECT_STATE picks a moving animation state and the end
//   state it leads to (0x005F3CB0): to 0 (active, a door open) from 1, state
//   2 then 3; to 1 (ready, closed) from 0, state 4 then 1, and from 2, state
//   7 then 1; to 2 (alternative, destroyed) from 1, state 5 then 6. Any other
//   change goes straight to the end state. The client also goes straight there
//   at GAMEOBJECT_ANIMPROGRESS 100, which an actor does not have.
// - A moving state that ends goes on to its end state (0x005F4120); an end
//   state plays again.
// - Each state plays an animation of the client's table 0x008607E4 (Spawn,
//   Closed, Open, Opened, Close, Destroy, Destroyed, Rebuild, ...). A loaded
//   model that lacks the open or close ones falls back as PlayAnimatedHandler-
//   State (0x005F3930) does, some held still at speed 0.

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "model/M2.h"
#include "time/Clock.h"

namespace Model::Scene::GameObjects {

namespace {

// The family A handler's animation states (+0x10) that this file names.
constexpr int kClosed = 1, kOpening = 2, kOpened = 3, kClosing = 4, kDestroying = 5, kDestroyed = 6,
              kRebuilding = 7;
constexpr int kStates = 13; // the table's length: 0 to 12

// The state a moving one leads to when it ends, else -1.
int EndOf(int state) {
    switch (state) {
    case kOpening:
        return kOpened;
    case kClosing:
    case kRebuilding:
        return kClosed;
    case kDestroying:
        return kDestroyed;
    default:
        return -1;
    }
}

// The animation and speed the state plays on `model`: its table entry, or the
// fallback for a loaded model without it (0x005F3930).
void Animation(Actor &actor, uint32_t &anim, float &speed) {
    anim = Game::Read<uint32_t>(Offsets::VAR_GAMEOBJECT_STATE_ANIMS + actor.gameObjectAnim * 4);
    speed = 1.0f;
    if (!actor.loaded || M2::HasAnimation(actor.model, anim))
        return;
    auto has = [&](uint32_t a) { return M2::HasAnimation(actor.model, a); };
    switch (anim) {
    case Offsets::ANIM_GAMEOBJECT_CLOSE:
        if (!has(Offsets::ANIM_GAMEOBJECT_OPEN))
            anim = Offsets::ANIM_GAMEOBJECT_CLOSED;
        break;
    case Offsets::ANIM_GAMEOBJECT_CLOSED:
        if (!has(Offsets::ANIM_GAMEOBJECT_CLOSE)) {
            if (has(Offsets::ANIM_GAMEOBJECT_OPEN)) {
                anim = Offsets::ANIM_GAMEOBJECT_OPEN;
                speed = 0.0f;
            } else {
                anim = 0;
            }
        }
        break;
    case Offsets::ANIM_GAMEOBJECT_OPEN:
        if (!has(Offsets::ANIM_GAMEOBJECT_CLOSE))
            anim = has(Offsets::ANIM_GAMEOBJECT_DESTROY) ? Offsets::ANIM_GAMEOBJECT_DESTROY
                                                        : Offsets::ANIM_GAMEOBJECT_OPENED;
        break;
    case Offsets::ANIM_GAMEOBJECT_OPENED:
        if (!has(Offsets::ANIM_GAMEOBJECT_OPEN)) {
            if (has(Offsets::ANIM_GAMEOBJECT_CLOSE)) {
                anim = Offsets::ANIM_GAMEOBJECT_CLOSE;
                speed = 0.0f;
            } else {
                anim = Offsets::ANIM_GAMEOBJECT_DESTROYED;
            }
        }
        break;
    default:
        break;
    }
}

} // namespace

const char *ModelPath(uint32_t displayID) {
    if (displayID == 0)
        return nullptr;
    const uint8_t *row = DBC::Record(Offsets::VAR_GAMEOBJECTDISPLAYINFO_RECORDS,
                                     Offsets::VAR_GAMEOBJECTDISPLAYINFO_COUNT, displayID);
    const char *path = row != nullptr ? Game::Read<const char *>(row, Offsets::OFF_GAMEOBJECTDISPLAYINFO_MODEL) : nullptr;
    return path != nullptr && *path != '\0' ? path : nullptr;
}

void SetState(Actor &actor, uint32_t state, int previous) {
    int anim;
    switch (state) {
    case 0:
        anim = previous == 1 ? kOpening : kOpened;
        break;
    case 1:
        anim = previous == 0 ? kClosing : previous == 2 ? kRebuilding : kClosed;
        break;
    case 2:
        anim = previous == 1 ? kDestroying : kDestroyed;
        break;
    default:
        return;
    }
    actor.gameObjectAnim = anim;
    actor.gameObjectAnimStartMs = ClockMs(actor.SceneFrame());
    Play(actor);
}

void Play(Actor &actor) {
    if (actor.model == nullptr || actor.gameObjectAnim < 0 || actor.gameObjectAnim >= kStates)
        return;
    uint32_t anim;
    float speed;
    Animation(actor, anim, speed);
    // A moving state that ended before its model loaded is at its end now.
    uint32_t offsetMs = Time::Clock::Elapsed(actor.gameObjectAnimStartMs, ClockMs(actor.SceneFrame()));
    if (actor.loaded && EndOf(actor.gameObjectAnim) >= 0) {
        const uint32_t length = M2::GetSequenceInfo(actor.model, anim, 0).length;
        if (offsetMs >= length) {
            actor.gameObjectAnim = EndOf(actor.gameObjectAnim);
            actor.gameObjectAnimStartMs += length;
            Animation(actor, anim, speed);
            offsetMs = 0;
        }
    } else {
        offsetMs = 0; // an end state loops from its start
    }
    actor.animation = anim;
    actor.variation = -1;
    actor.animSpeed = speed;
    actor.animOffsetSec = static_cast<float>(offsetMs) / 1000.0f;
    actor.PlayAnimation();
}

bool FollowOn(Actor &actor) {
    const int end = actor.gameObjectAnim >= 0 ? EndOf(actor.gameObjectAnim) : -1;
    if (end < 0)
        return false;
    actor.gameObjectAnim = end;
    actor.gameObjectAnimStartMs = ClockMs(actor.SceneFrame());
    Play(actor);
    return true;
}

} // namespace Model::Scene::GameObjects
