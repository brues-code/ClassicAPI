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

// Tab on the scored candidate list — TargetNearestUseNew, as the 3.80.2
// Classic client runs it (docs/notes/soft-targeting-3802.md, "Tab targeting:
// the new path"): FUN_1405deee0 and its chooser FUN_1405e21b0.
//
// Every tab search in 1.12 is FUN_TARGET_NEAREST_CYCLE: the four natives,
// Target::Nearest's three modes, the key bindings and the attack with no
// target. With TargetNearestUseNew 0 (the default: Tab is the most used
// targeting key, so the change is opt-in) it runs as it is; with 1 the hook
// runs 3.80.2's path, the cycle's mode becoming its candidate mask:
//   - with a soft enemy (an enemy search) or a soft friend (a friend
//     search), Tab targets it unless it already is the locked target;
//   - a loose target that the search accepts is only locked (reason Tab, no
//     event);
//   - else the chooser walks the tab order (Target::Priority, rebuilt now)
//     from the target's place. It skips what this cycle has already
//     visited (the history), so Tab goes to the best unvisited candidate and
//     Shift-Tab walks back through the history. Each pick goes into the
//     history right after the target, replacing what followed it.
//   - a candidate needs to be in view, in line of sight and in range, not
//     excluded and no critter. With the target combat-locked, only
//     combat-locked enemies count (relaxed when none is in the camera cone,
//     TargetPriorityCombatLockContextualRelaxation); when nothing counts the
//     lock is dropped, and with TargetPriorityAllowAnyOnScreen 2 the range
//     and the exclusion too. Nothing at all: the target stays.
//   - the cycle restarts from the best after 3 s, a 20° camera turn or 15 yd
//     of movement since the target was set; the history is forgotten after
//     6 s, an 80° turn or 100 yd, and when the target is cleared;
//   - a click on a unit seeds the history with the old and the new target,
//     and with TargetPriorityContinueFromManualTarget Tab goes on from it;
//   - a target the game set itself (Auto, Locked) inside
//     TargetPriorityAutoTargetIgnoreWindow does not count as the place.
//
// 3.80.2 runs a bank of values (bank 1 here); hold-to-target (its bank 2),
// the PvP locks and the vehicle are not in 1.12.

#include "target/Tab.h"

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "object/Resolve.h"
#include "target/Priority.h"
#include "target/SoftUnit.h"
#include "time/Clock.h"
#include "unit/Identity.h"
#include "unit/Position.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Target::Tab {

namespace {

using Cycle_t = void(__fastcall *)(int reverse, int mode);
using Valid_t = int(__fastcall *)(void *player, void *candidate, int mode);

constexpr float kPi = 3.14159265f;
constexpr float kDeg = 0.017453292f;

// The chooser's base filter (3.80.2's static masks at 0x14440ddb8 / b4):
// in view, in line of sight and in range; not excluded, no critter.
constexpr uint32_t kNeed = Priority::InFrustum | Priority::Visible | Priority::InRange;
constexpr uint32_t kExclude = Priority::Excluded | Priority::Critter;
constexpr uint32_t kLocks = Priority::PvpPriority | Priority::CombatLock;

// Bank 1's cycle tables (3.80.2 0x143a83334 / 58 / 40 / 64 / 4c / 70).
constexpr uint32_t kRestartMs = 3000;
constexpr uint32_t kForgetMs = 6000;
constexpr float kRestartTurn = 20 * kDeg;
constexpr float kForgetTurn = 80 * kDeg;
constexpr float kRestartMove = 15;
constexpr float kForgetMove = 100;

Cycle_t g_cycleOriginal = nullptr;

CVar::Factory::Handle g_useNew = nullptr;
CVar::Factory::Handle g_allowAny = nullptr;
CVar::Factory::Handle g_continueManual = nullptr;
CVar::Factory::Handle g_ignoreWindow = nullptr;

std::vector<uint64_t> g_history; // the targets this cycle has visited
uint32_t g_setAt = 0;            // when the target was set
uint32_t g_autoSetAt = 0;        // the same, for a target the game set (Auto, Locked)
float g_setYaw = 0;              // the camera's yaw then
float g_setPos[3] = {};          // the player's position then
bool g_continue = false;         // go on from a clicked target
uint32_t g_locks = 0;            // the lock bits taken from the target
int g_lastLockMode = 0;          // TargetPriorityCombatLock at the last check

struct Filter {
    uint32_t need;
    uint32_t exclude;
};

int AllowAny() { return CVar::Factory::GetInt(g_allowAny); }

// FUN_1405e36a0's lock bits for a unit: CombatLock while it is combat-locked.
uint32_t Locks(uint64_t guid) {
    return Priority::CombatLockMode() != 0 && Priority::CombatLockedWith(guid)
               ? Priority::CombatLock
               : 0;
}

// One relax step: drop the locks; without any, with AllowAnyOnScreen 2 drop
// the range and the exclusion. False when nothing changed.
bool Relax(Filter &f) {
    if ((f.need & kLocks) != 0) {
        f.need &= ~kLocks;
        return true;
    }
    if (AllowAny() < 2)
        return false;
    const Filter before = f;
    f.need &= ~Priority::InRange;
    f.exclude &= ~Priority::Excluded;
    return f.need != before.need || f.exclude != before.exclude;
}

void RelaxTimes(Filter &f, int times) {
    for (int i = 0; i < times; ++i)
        Relax(f);
}

// The target's lock bits taken again, then the relax steps already made.
void Relock(Filter &f, uint64_t target, int relaxed) {
    g_locks = Locks(target);
    f.need = (f.need & ~kLocks) | g_locks;
    f.exclude |= Priority::Excluded;
    RelaxTimes(f, relaxed);
}

bool Accept(uint32_t flags, const Filter &f) {
    return (flags & f.exclude) == 0 && (flags & f.need) == f.need &&
           (flags & Priority::InFrustum) != 0;
}

bool InHistory(uint64_t guid, size_t limit) {
    const size_t n = std::min(limit, g_history.size());
    for (size_t i = 0; i < n; ++i)
        if (g_history[i] == guid)
            return true;
    return false;
}

// FUN_1405e3cd0: puts `guid` at `index` (at most the end) — moved there when
// it is in the history, else it replaces the entry there, or is appended.
void PlaceAt(uint64_t guid, size_t index) {
    if (guid == 0)
        return;
    const size_t n = g_history.size();
    index = std::min(index, n);
    const auto it = std::find(g_history.begin(), g_history.end(), guid);
    const bool found = it != g_history.end();
    const size_t at = found ? static_cast<size_t>(it - g_history.begin()) : 0;
    if (found && at == index)
        return;
    if (index == n)
        g_history.push_back(0);
    if (found && index != n) {
        if (index < at)
            std::move_backward(g_history.begin() + index, g_history.begin() + at,
                               g_history.begin() + at + 1);
        else
            std::move(g_history.begin() + at + 1, g_history.begin() + index + 1,
                      g_history.begin() + at);
    }
    g_history[index] = guid;
}

int Step(int i, int n, bool reverse) {
    i += reverse ? -1 : 1;
    if (i < 0)
        return n - 1;
    return i >= n ? 0 : i;
}

// FUN_1405e39a0: the first candidate in tab order from `start` (from the
// one after it unless inclusive) that the filter accepts and is not among
// the history's first `limit`.
uint64_t Walk(size_t start, bool inclusive, size_t limit, bool reverse, const Filter &f) {
    const int n = static_cast<int>(Priority::TabSize());
    if (n == 0)
        return 0;
    int i = static_cast<int>(start);
    int count = n;
    if (!inclusive || start >= static_cast<size_t>(n)) {
        i = Step(i, n, reverse);
        count = n - 1;
    }
    for (; count > 0; --count, i = Step(i, n, reverse)) {
        const uint64_t guid = Priority::TabGuid(i);
        if (Accept(Priority::TabFlags(i), f) && !InHistory(guid, limit))
            return guid;
    }
    return 0;
}

// FUN_1405e3b00: the first history entry from the one after `index` that is
// still a candidate the filter accepts (combat-locked: in range or excluded
// alike); entries whose object is gone are dropped on the way.
uint64_t HistStep(size_t index, bool reverse, Filter f) {
    if ((f.need & Priority::CombatLock) != 0) {
        f.exclude &= ~Priority::Excluded;
        f.need &= ~Priority::InRange;
    }
    int n = static_cast<int>(g_history.size());
    if (n == 0)
        return 0;
    int i = Step(static_cast<int>(index), n, reverse);
    for (int left = n - 1; left > 0; --left) {
        const uint64_t guid = g_history[i];
        const int rank = Priority::TabRank(guid);
        const uint32_t flags = rank >= 0 ? Priority::TabFlags(rank) : 0;
        if (rank < 0 || (flags & f.exclude) != 0 || (flags & f.need) != f.need) {
            i = Step(i, n, reverse);
            continue;
        }
        if (Object::ByGuid(Offsets::TYPEMASK_OBJECT, guid) != nullptr)
            return guid;
        g_history.erase(g_history.begin() + i);
        n = static_cast<int>(g_history.size());
        if (n == 0)
            return 0;
        if (i >= n)
            i = 0;
    }
    return 0;
}

// The camera's yaw and the player's position, now.
bool Where(float &yaw, float *pos) {
    void *player = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    return player != nullptr && Unit::Position::Read(player, pos) && Priority::CameraYaw(yaw);
}

float Turned(float from, float to) {
    float a = to - from;
    while (a > kPi)
        a -= 2 * kPi;
    while (a < -kPi)
        a += 2 * kPi;
    return std::fabs(a);
}

// FUN_1405e21b0. The pick becomes the target (reason Tab) and goes into the
// history; with none, the target stays.
void Choose(uint64_t target, bool reverse, uint32_t mask) {
    const uint32_t now = Time::Clock::NowMs();
    const int lockMode = Priority::CombatLockMode();
    Filter f{mask | kNeed | g_locks, kExclude};
    size_t rank = 0;
    bool inclusive = true;
    int relaxed = 0;
    size_t index = 0;
    size_t limit = g_history.size();
    bool inHistory = false;
    uint64_t pick = 0;

    if (target == 0) {
        g_locks = 0;
        f = {mask | kNeed, kExclude};
        g_continue = false;
        pick = Walk(0, !reverse, g_history.size(), reverse, f);
        if (pick != 0)
            g_locks = Locks(pick);
    } else {
        const int at = Priority::TabRank(target);
        const bool inList = at >= 0;
        if (inList) {
            rank = static_cast<size_t>(at);
            if ((f.need & Priority::CombatLock) != 0 && !Priority::LockedInCone() &&
                Priority::CombatLockRelaxation() && Relax(f))
                relaxed = 1;
            if (AllowAny() != 0 && Walk(rank, true, 0, false, f) != target) {
                const int steps = AllowAny() > 1 ? 2 : 1;
                for (int i = relaxed; i < steps; ++i)
                    if (Relax(f))
                        ++relaxed;
            }
        }
        for (size_t i = 0; i < limit; ++i) {
            if (g_history[i] == target) {
                index = i;
                limit = i + 1;
                inHistory = true;
                break;
            }
        }
        bool fresh = false;
        if (!reverse) {
            fresh = !g_continue;
            bool timedOut = false;
            if (Time::Clock::Reached(now, g_setAt + kRestartMs)) {
                timedOut = true;
                fresh = true;
            }
            bool forget = Time::Clock::Reached(now, g_setAt + kForgetMs);
            float yaw, pos[3];
            if (Where(yaw, pos)) {
                const float turn = Turned(g_setYaw, yaw);
                const float moved = std::sqrt(Game::DistanceSq(g_setPos, pos));
                fresh |= turn > kRestartTurn || moved > kRestartMove;
                forget |= turn > kForgetTurn || moved > kForgetMove;
            }
            if (lockMode != g_lastLockMode) {
                g_lastLockMode = lockMode;
                timedOut = true;
            }
            if (timedOut || forget)
                Relock(f, target, relaxed);
            if (fresh)
                g_continue = false;
            if (forget) {
                g_history.clear();
                PlaceAt(target, 0);
                inHistory = true;
                index = 0;
                limit = 1;
                g_continue = false;
            }
        }
        if (fresh) {
            if (rank == 0)
                fresh = false;
            else
                rank = 0;
        }
        if (inList && !fresh)
            inclusive = false;
        const int window = CVar::Factory::GetInt(g_ignoreWindow);
        if (g_autoSetAt != 0 && !inclusive && window != 0)
            inclusive = !Time::Clock::Reached(now, g_autoSetAt + static_cast<uint32_t>(window));
        if (!inHistory) {
            index = reverse ? 0 : g_history.size();
            PlaceAt(target, index);
            inHistory = true;
            Relock(f, target, relaxed);
            pick = Walk(rank, inclusive, limit, reverse, f);
        }
    }

    while (pick == 0) {
        if (reverse) {
            if (index != 0)
                pick = HistStep(index, reverse, f);
            else if (g_history.empty())
                pick = Game::Read<uint64_t>(Offsets::VAR_LAST_SELECTION_GUID_LO);
        }
        if (pick == 0)
            pick = Walk(rank, inclusive, limit, reverse, f);
        if (pick == 0)
            pick = HistStep(index, reverse, f);
        if (pick == 0)
            pick = Walk(rank, inclusive, 0, reverse, f);
        if (pick == 0 && !Relax(f))
            return;
    }

    Reason::Set(pick, Reason::Tab);
    size_t at = reverse ? 0 : g_history.size();
    if (inHistory)
        at = reverse ? index : index + 1;
    if (!InHistory(pick, at))
        PlaceAt(pick, at);
}

// The cycle's mode as 3.80.2's candidate mask (its Lua wrappers' masks).
uint32_t MaskFor(int mode) {
    switch (mode) {
    case Offsets::TARGET_MODE_FRIEND:
        return Priority::Friend;
    case Offsets::TARGET_MODE_PARTY:
        return Priority::Party;
    case Offsets::TARGET_MODE_RAID:
        return Priority::Party | Priority::Raid;
    case Offsets::TARGET_MODE_ANY:
        return Priority::Unit;
    case Offsets::TARGET_MODE_ENEMY_PLAYER:
        return Priority::Enemy | Priority::Player;
    case Offsets::TARGET_MODE_FRIEND_PLAYER:
        return Priority::Friend | Priority::Player;
    default: // the predicate takes any other mode as the enemy one
        return Priority::Enemy;
    }
}

// The search accepts the unit `guid` (FUN_1405ddc80 is 1.12's predicate).
bool Fits(uint64_t guid, int mode) {
    void *player = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid);
    return player != nullptr && unit != nullptr &&
           reinterpret_cast<Valid_t>(Offsets::FUN_TARGET_CANDIDATE_VALID)(player, unit, mode) != 0;
}

// FUN_1405deee0.
void Run(bool reverse, int mode) {
    const uint32_t mask = MaskFor(mode);
    const uint64_t target = Reason::Selection();
    const bool loose = Reason::IsLoose();
    const uint64_t softEnemy = SoftUnit::Get(SoftUnit::Side::Enemy);
    const uint64_t softFriend = SoftUnit::Get(SoftUnit::Side::Friend);
    if ((mask & Priority::Enemy) != 0 && softEnemy != 0 && (loose || target != softEnemy)) {
        Reason::Set(softEnemy, Reason::Tab);
        return;
    }
    if ((mask & Priority::Friend) != 0 && softFriend != 0) {
        if (loose || target != softFriend) {
            Reason::Set(softFriend, Reason::Tab);
            return;
        }
    } else if (loose && Fits(target, mode)) {
        Reason::Promote(Reason::Tab);
        return;
    }
    Priority::Refresh();
    Choose(target, reverse, mask);
}

void __fastcall Cycle_h(int reverse, int mode) {
    if (CVar::Factory::GetInt(g_useNew) == 0 || !Object::RegistryReady()) {
        g_cycleOriginal(reverse, mode);
        return;
    }
    Run(reverse != 0, mode);
}

const Game::HookAutoRegister _hook{Offsets::FUN_TARGET_NEAREST_CYCLE,
                                   reinterpret_cast<void *>(&Cycle_h),
                                   reinterpret_cast<void **>(&g_cycleOriginal)};

void Register() {
    const auto reg = [](const char *name, const char *help, const char *value) {
        return CVar::Factory::Register(name, help, Offsets::CVAR_CATEGORY_GAME, value);
    };
    g_useNew = reg("TargetNearestUseNew",
        "Use new 7.2 'nearest target' functionality (Set to 0 for 6.x style tab targeting)", "0");
    g_allowAny = reg("TargetPriorityAllowAnyOnScreen",
        "If set, and no 100% correct target is available, allow selecting any valid in-range target (2 = also out-of-range)", "0");
    g_continueManual = reg("TargetPriorityContinueFromManualTarget",
        "If a target is manually selected (eg. clicked), continue priority targeting from that target.", "1");
    g_ignoreWindow = reg("TargetPriorityAutoTargetIgnoreWindow",
        "Number of MS a non-user selected target should be ignored For priority order purposes", "500");
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

void OnSet(uint64_t old, uint64_t guid, Reason::Why why) {
    const bool seed = why == Reason::Click || why == Reason::AutoSpell;
    if (seed || old == 0)
        g_locks = Locks(guid);
    if (seed) {
        PlaceAt(guid, 0);
        PlaceAt(old, 0);
        g_continue = CVar::Factory::GetInt(g_continueManual) != 0;
    }
    const uint32_t now = Time::Clock::NowMs();
    g_setAt = now;
    g_autoSetAt = why == Reason::Auto || why == Reason::Locked ? now : 0;
    float yaw, pos[3];
    if (Where(yaw, pos)) {
        g_setYaw = yaw;
        std::copy(pos, pos + 3, g_setPos);
    }
}

void OnClear() {
    g_history.clear();
    g_continue = false;
}

} // namespace Target::Tab
