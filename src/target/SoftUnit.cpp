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

// The soft enemy and soft friend — Classic Era 1.15's `softenemy` /
// `softfriend` / `anyenemy` / `anyfriend` tokens, their
// PLAYER_SOFT_ENEMY_CHANGED / PLAYER_SOFT_FRIEND_CHANGED events and the
// SoftTargetEnemy* / SoftTargetFriend* / SoftTargetMatchLocked cvars.
//
// The scan is Target::SoftTarget's (on screen, arc, range, nearest). What
// qualifies is what tab targeting takes: FUN_TARGET_CANDIDATE_VALID's enemy
// or friend mode (TargetNearestEnemy / TargetNearestFriend), minus the
// creature types tab targeting never picks. The player is never a candidate.
//
// Classic Era 1.15 probes:
//   - the nearest candidate wins, not the one nearest the forward line;
//   - SoftTargetMatchLocked 1: a target that qualifies is `softenemy`, even
//     with a nearer enemy; past the range the nearer enemy stays;
//   - SoftTargetMatchLocked 0: `softenemy` stays the nearest enemy, and
//     `anyenemy` is the target that qualifies;
//   - `softfriend` picked a party member, not the player;
//   - a friendly target is never `softenemy`, a hostile one never
//     `softfriend` — the match keeps to the side's test.
//
// Defaults are Classic Era's: SoftTargetEnemy 1 (gamepad, so off here),
// SoftTargetFriend 0, both arcs 2, both ranges 45, SoftTargetMatchLocked 1.
// Picked when something asks (a read of a token) and on the UI frame tick
// only while a frame is registered for the side's event.

#include "target/SoftUnit.h"

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "event/Custom.h"
#include "object/Resolve.h"
#include "target/Nearest.h"
#include "target/SoftTarget.h"
#include "tick/FrameTick.h"

namespace Target::SoftUnit {

namespace {

using Valid_t = int(__fastcall *)(void *player, void *cand, int mode);

const Game::Doc::Event kEnemyChangedDoc{"Unit", "Fires when the softenemy unit changes.", {}};
const Game::Doc::Event kFriendChangedDoc{"Unit", "Fires when the softfriend unit changes.", {}};
const Event::Custom::AutoReserve _enemyChanged{"PLAYER_SOFT_ENEMY_CHANGED", &kEnemyChangedDoc};
const Event::Custom::AutoReserve _friendChanged{"PLAYER_SOFT_FRIEND_CHANGED",
                                                &kFriendChangedDoc};

struct State {
    int mode; // FUN_TARGET_CANDIDATE_VALID's
    const Event::Custom::AutoReserve *changed;
    CVar::Factory::Handle enable;
    CVar::Factory::Handle arc;
    CVar::Factory::Handle range;
    uint64_t soft;
};

State g_enemy{Offsets::TARGET_MODE_ENEMY, &_enemyChanged};
State g_friend{Offsets::TARGET_MODE_FRIEND, &_friendChanged};

CVar::Factory::Handle g_matchLocked = nullptr;

constexpr float kDefaultRange = 45.0f;

State &Of(Side side) { return side == Side::Enemy ? g_enemy : g_friend; }

bool Accept(const SoftTarget::Area &area, void *object, float distSq, void *ctx) {
    const auto &st = *static_cast<const State *>(ctx);
    return distSq <= area.rangeSq &&
           reinterpret_cast<Valid_t>(Offsets::FUN_TARGET_CANDIDATE_VALID)(area.player, object,
                                                                          st.mode) != 0 &&
           !Nearest::NotTabTarget(object);
}

uint64_t LockedTarget() { return Game::Read<uint64_t>(Offsets::VAR_CURRENT_SELECTION_GUID_LO); }

// The target, when it passes the side's whole test.
bool Qualifies(const State &st, const SoftTarget::Area &area, uint64_t guid) {
    if (guid == 0)
        return false;
    void *object = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid);
    float distSq;
    return object != nullptr && SoftTarget::InView(area, object, distSq) &&
           Accept(area, object, distSq, const_cast<State *>(&st));
}

uint64_t Pick(State &st) {
    if (!SoftTarget::Enabled(st.enable))
        return 0;
    SoftTarget::Area area;
    if (!SoftTarget::Begin(area, st.arc, st.range, kDefaultRange))
        return 0;
    const uint64_t target = LockedTarget();
    if (CVar::Factory::GetInt(g_matchLocked) != 0 && Qualifies(st, area, target))
        return target;
    return SoftTarget::Nearest(area, Offsets::TYPEMASK_UNIT, &Accept, &st);
}

bool Watched(const State &st) { return Event::Custom::HasListeners(st.changed->Slot()); }

void Refresh(State &st) {
    const uint64_t guid = Pick(st);
    if (guid == st.soft)
        return;
    st.soft = guid;
    Event::Custom::Fire(st.changed->Slot(), "");
}

void OnFrameTick() {
    if (!Object::RegistryReady()) {
        g_enemy.soft = 0;
        g_friend.soft = 0;
        return;
    }
    if (Watched(g_enemy))
        Refresh(g_enemy);
    if (Watched(g_friend))
        Refresh(g_friend);
}

const Tick::FrameTick::AutoSubscribe _tick{&OnFrameTick};

void Register() {
    g_enemy.enable = CVar::Factory::Register("SoftTargetEnemy", "1");
    g_enemy.arc = CVar::Factory::Register("SoftTargetEnemyArc", "2");
    g_enemy.range = CVar::Factory::Register("SoftTargetEnemyRange", "45");
    g_friend.enable = CVar::Factory::Register("SoftTargetFriend", "0");
    g_friend.arc = CVar::Factory::Register("SoftTargetFriendArc", "2");
    g_friend.range = CVar::Factory::Register("SoftTargetFriendRange", "45");
    g_matchLocked = CVar::Factory::Register("SoftTargetMatchLocked", "1");
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

// Fresh when nothing watches; the frame tick's pick (this frame's) when a
// listener does.
uint64_t Get(Side side) {
    if (!Object::RegistryReady())
        return 0;
    State &st = Of(side);
    if (!Watched(st))
        Refresh(st);
    return st.soft;
}

// Classic Era 1.15 probes, `anyfriend`: a target that qualifies, else
// `softfriend`, else the target whatever its reaction (a hostile one, or a
// friendly NPC the friend test rejects), else `softenemy` (no target, no
// friend near). `anyenemy` takes the same order with the sides swapped; its
// last two steps are by symmetry, not probed.
uint64_t GetAny(Side side) {
    if (!Object::RegistryReady())
        return 0;
    State &st = Of(side);
    SoftTarget::Area area;
    const uint64_t target = LockedTarget();
    if (SoftTarget::Enabled(st.enable) &&
        SoftTarget::Begin(area, st.arc, st.range, kDefaultRange) && Qualifies(st, area, target))
        return target;
    if (const uint64_t soft = Get(side))
        return soft;
    if (target != 0)
        return target;
    return Get(side == Side::Enemy ? Side::Friend : Side::Enemy);
}

} // namespace Target::SoftUnit
