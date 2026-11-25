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

// The soft enemy and soft friend, and the soft driver all three soft targets
// (with Target::SoftInteract) run from — the `softenemy` / `softfriend` /
// `anyenemy` / `anyfriend` tokens, their PLAYER_SOFT_ENEMY_CHANGED /
// PLAYER_SOFT_FRIEND_CHANGED events and the SoftTargetEnemy* /
// SoftTargetFriend* / SoftTargetMatchLocked / SoftTargetWithLocked cvars, as
// the 3.80.2 Classic client runs them (docs/notes/soft-targeting-3802.md).
//
// This is its per-frame soft driver (FUN_1405dd710 around FUN_1405e0650),
// run on the UI frame tick:
//   1. a loose target whose object is gone is cleared (Target::Reason);
//   2. the candidate list (Target::Priority) is rebuilt when its delay has
//      passed; with a target and SoftTargetWithLocked 0 both soft units are
//      cleared instead, and nothing else runs;
//   3. on a rebuild, the pick (FUN_1405e1410; interact's in
//      Target::SoftInteract): with SoftTargetMatchLocked 1
//      a locked target, with 2 also a target your spell set (AutoSpell), is
//      the side's soft unit when it is that side's candidate — even with the
//      side off. Otherwise the first candidate of the side in soft order
//      that is in view, in range and inside the side's arc;
//   4. every frame, the check that keeps them (FUN_1405e1900): still that
//      side's candidate and no critter, within SoftTarget<Side>Range of the
//      player (3D, centre to centre); the soft enemy is also dropped when it
//      is a locked target the player is attacking (unless
//      SoftTargetWithLocked 2). Between rebuilds a soft unit can only be
//      dropped, never replaced;
//   5. SoftTargetForce (Target::Reason), then the events of the sides that
//      changed (interact, enemy, friend), then, on a rebuild, the
//      loose-target expiry.
// A side is on with SoftTarget<Side> 2 or 3 (1 is gamepad, which 1.12 lacks)
// and a range above 0. The tokens read the last result; nothing picks on a
// read.
//
// Defaults are Classic Era's (1.15.9) and 3.80.2's: SoftTargetEnemy and
// SoftTargetFriend 0, both arcs 2, both ranges 45, SoftTargetMatchLocked 1,
// SoftTargetWithLocked 1.
// The softenemy / softfriend unit events (UNIT_HEALTH, …) follow the pick via
// Unit::TokenEvents, as they do for focus.

#include "target/SoftUnit.h"

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "event/Custom.h"
#include "object/Resolve.h"
#include "target/Priority.h"
#include "target/Reason.h"
#include "target/SoftInteract.h"
#include "target/SoftTarget.h"
#include "tick/FrameTick.h"
#include "unit/Identity.h"
#include "unit/Position.h"
#include "unit/TokenEvents.h"

namespace Target::SoftUnit {

namespace {

const Game::Doc::Event kEnemyChangedDoc{"Unit", "Fires when the softenemy unit changes.", {}};
const Game::Doc::Event kFriendChangedDoc{"Unit", "Fires when the softfriend unit changes.", {}};
const Event::Custom::AutoReserve _enemyChanged{"PLAYER_SOFT_ENEMY_CHANGED", &kEnemyChangedDoc};
const Event::Custom::AutoReserve _friendChanged{"PLAYER_SOFT_FRIEND_CHANGED",
                                                &kFriendChangedDoc};

struct State {
    uint32_t type; // the side's candidate flag
    const Event::Custom::AutoReserve *changed;
    CVar::Factory::Handle enable;
    CVar::Factory::Handle arc;
    CVar::Factory::Handle range;
    uint64_t soft;
};

State g_enemy{Priority::Enemy, &_enemyChanged};
State g_friend{Priority::Friend, &_friendChanged};

CVar::Factory::Handle g_matchLocked = nullptr;
CVar::Factory::Handle g_withLocked = nullptr;

State &Of(Side side) { return side == Side::Enemy ? g_enemy : g_friend; }

bool On(const State &st) {
    return SoftTarget::Enabled(st.enable) && CVar::Factory::GetFloat(st.range) > 0.0f;
}

// FUN_1405e1280 for enemy / friend.
uint64_t Pick(const State &st, uint32_t lockedFlags, uint64_t target) {
    if ((lockedFlags & st.type) != 0)
        return target;
    if (!On(st))
        return 0;
    return Priority::First(st.type, CVar::Factory::GetInt(st.arc));
}

// FUN_1405e1410's locked-target match.
uint32_t LockedFlags(uint64_t target) {
    if (target == 0)
        return 0;
    const int match = CVar::Factory::GetInt(g_matchLocked);
    const Reason::Why why = Reason::Current();
    const bool loose = Reason::IsLoose();
    if ((match == 1 && !loose) || (match == 2 && (!loose || why == Reason::AutoSpell)))
        return Priority::FlagsOf(target);
    return 0;
}

bool WithinRange(uint64_t guid, CVar::Factory::Handle range) {
    void *player = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid);
    float a[3], b[3];
    if (player == nullptr || !Unit::Position::Read(player, a) || !Unit::Position::Read(unit, b))
        return false;
    const float r = CVar::Factory::GetFloat(range);
    return Game::DistanceSq(a, b) <= r * r;
}

// FUN_1405e1900 for enemy / friend.
void Validate(State &st, uint64_t target) {
    if (st.soft == 0)
        return;
    bool keep = (Priority::FlagsOf(st.soft) & (st.type | Priority::Critter)) == st.type &&
                WithinRange(st.soft, st.range);
    if (keep && &st == &g_enemy && st.soft == target && !Reason::IsLoose() &&
        CVar::Factory::GetInt(g_withLocked) != 2) {
        const auto *player = Unit::Identity::PlayerObject();
        keep = player == nullptr ||
               Game::Read<uint64_t>(player, Offsets::OFF_UNIT_ATTACK_TARGET) == 0;
    }
    if (!keep)
        st.soft = 0;
}

void Changed(State &st, uint64_t old) {
    if (st.soft == old)
        return;
    // The token's unit events follow the pick (the engine watches a unit
    // that holds a token), so the registration moves before the event fires.
    Unit::TokenEvents::Refresh(old);
    Unit::TokenEvents::Refresh(st.soft);
    Event::Custom::Fire(st.changed->Slot(), "");
}

void OnFrameTick() {
    Reason::DropVanished();
    if (!Object::RegistryReady()) {
        g_enemy.soft = 0;
        g_friend.soft = 0;
        SoftInteract::Clear();
        return;
    }
    const uint64_t oldEnemy = g_enemy.soft;
    const uint64_t oldFriend = g_friend.soft;
    const bool rebuilt = Priority::Update();
    const uint64_t target = Reason::Selection();
    const bool allowed = target == 0 || CVar::Factory::GetInt(g_withLocked) != 0;
    if (!allowed) {
        // 3.80.2's FUN_1405e2120: only the three soft targets go; the
        // candidate list stays, so UnitIsInteractable keeps answering.
        g_enemy.soft = 0;
        g_friend.soft = 0;
        SoftInteract::Clear();
    } else if (rebuilt) {
        const uint32_t locked = LockedFlags(target);
        g_enemy.soft = Pick(g_enemy, locked, target);
        g_friend.soft = Pick(g_friend, locked, target);
        SoftInteract::Pick(locked, target);
    }
    SoftInteract::Validate();
    Validate(g_enemy, target);
    Validate(g_friend, target);
    if (allowed)
        Reason::ApplyForce();
    SoftInteract::Notify();
    Changed(g_enemy, oldEnemy);
    Changed(g_friend, oldFriend);
    if (rebuilt)
        Reason::Expire();
}

const Tick::FrameTick::AutoSubscribe _tick{&OnFrameTick};

// The values of the SoftTargetEnemy / Friend / Interact cvars (SoftTarget.h).
const Game::Lua::EnumIntegerEntry kSoftTargetEnableFlags[] = {
    {"None", 0},
    {"Gamepad", 1},
    {"Kbm", 2},
    {"Any", 3},
};

void Register() {
    Game::Lua::RegisterIntegerEnum("Enum", "SoftTargetEnableFlags", kSoftTargetEnableFlags, "SoftTargetConstants");
    const auto reg = [](const char *name, const char *help, const char *value) {
        return CVar::Factory::Register(name, help, Offsets::CVAR_CATEGORY_GAME, value);
    };
    g_enemy.enable = reg("SoftTargetEnemy",
        "Sets when enemy soft targeting should be enabled. 0=off, 1=gamepad, 2=KBM, 3=always", "1");
    g_enemy.arc = reg("SoftTargetEnemyArc",
        "0 = No yaw arc allowance, must be directly in front. 1 = Must be in front yaw arc. 2 = Can be anywhere in tab targeting area.", "2");
    g_enemy.range = reg("SoftTargetEnemyRange",
        "Max range to soft target enemies (limited to tab targeting range)", "45");
    g_friend.enable = reg("SoftTargetFriend",
        "Sets when friend soft targeting should be enabled. 0=off, 1=gamepad, 2=KBM, 3=always", "0");
    g_friend.arc = reg("SoftTargetFriendArc",
        "0 = No yaw arc allowance, must be directly in front. 1 = Must be in front yaw arc. 2 = Can be anywhere in targeting area.", "2");
    g_friend.range = reg("SoftTargetFriendRange",
        "Max range to soft target friends (limited to tab targeting range)", "45");
    g_matchLocked = reg("SoftTargetMatchLocked",
        "Match appropriate soft target to locked target. 1 = hard locked target only, 2 = for targets you attack", "1");
    g_withLocked = reg("SoftTargetWithLocked",
        "Allows soft target selection while player has a locked target. 2 = always do soft targeting", "1");
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

uint64_t Get(Side side) { return Of(side).soft; }

uint64_t Current(Side side) { return Of(side).soft; }

bool Enabled(Side side) { return SoftTarget::Enabled(Of(side).enable); }

int Arc(Side side) { return CVar::Factory::GetInt(Of(side).arc); }

// 3.80.2's ActionTarget (FUN_1405dd580), which its `anyenemy` / `anyfriend`
// resolve through: with no soft unit, the target; the soft unit while there
// is no target, it is the target, or the target is loose; the target when
// it is a candidate of the side; else the soft unit.
uint64_t GetAny(Side side) {
    const State &st = Of(side);
    const uint64_t target = Reason::Selection();
    if (st.soft == 0)
        return target;
    if (target == 0 || st.soft == target || Reason::IsLoose())
        return st.soft;
    return Priority::Has(target, st.type) ? target : st.soft;
}

uint64_t AnyTarget() {
    const uint64_t target = Reason::Selection();
    if (target == 0 || Reason::IsLoose()) {
        if (g_enemy.soft != 0)
            return g_enemy.soft;
        if (g_friend.soft != 0)
            return g_friend.soft;
        if (target == 0)
            return SoftInteract::Get();
    }
    return target;
}

} // namespace Target::SoftUnit
