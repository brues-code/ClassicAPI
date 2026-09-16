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

// `C_SwingTimer.EnableRangeCheck` / `IsTargetWithinSwingRange` and
// `PLAYER_SWING_RANGE_UPDATE(swingType, isInRange, checksRange)`.
//
// RANGED reuses the engine's generic range core: `Spell::Range::PlayerVsUnit`
// wraps `FUN_SPELL_RANGE_CHECK` -> `FUN_006e3480`, feeding it Auto Shot/Shoot
// as a pure geometry probe (it doesn't check spell knowledge). That core
// computes full 3D (X/Y/Z) distance, which matches the SERVER's generic
// spell-range gate (`Spell::CheckRange`'s non-combat-range path ->
// `WorldObject::GetCombatDistance`, also 3D) closely enough for ranged.
//
// MELEE does NOT reuse it — verified wrong in-game (rejected a swing at
// 5.08yd 3D distance from a stationary target's center while hits were
// landing). The server's actual melee-attack gate
// (`Unit::CanReachWithMeleeAutoAttackAtPosition`,
// `WorldObject::CanReachWithMeleeSpellAttack`) is explicitly 2D — its own
// source comment says "melee spells ignore Z-axis checks" — so any Z offset
// between the two units (sloped ground, stairs, model-origin differences)
// eats into a distance budget the server never charges for. The GENERIC
// engine range core was never asked to gate a real auto-attack decision
// before this feature existed, so there's no existing client mechanism for
// that exact semantic to mirror; `Combat::Swing::InMeleeRange` reproduces the
// SERVER's formula instead (using the engine's own live reach constants
// rather than hardcoding them) — shared with `Combat::Swing`, which also
// needs it to decide whether an attack-start off-hand reset is close enough
// to promise a real swing time.
//
// What this module adds on top of the geometry: the "no weapon for this
// swing type" and "target can't be attacked" gates Blizzard's contract also
// requires (both report as `isInRange == nil`, i.e. `checksRange == false`),
// and turning the per-frame poll into a change-driven
// `PLAYER_SWING_RANGE_UPDATE`.

#include "combat/Swing.h"

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "item/CGItem.h"
#include "item/Location.h"
#include "item/Record.h"
#include "spell/Range.h"
#include "tick/WorldTick.h"
#include "unit/Position.h"

#include <cstdint>

namespace Combat::SwingRange {

namespace {

using Combat::Swing::SwingType;
using Combat::Swing::RANGED;

enum class RangeState { NoCheck, InRange, OutOfRange };

bool g_enabled[3] = {false, false, false};
RangeState g_lastState[3] = {RangeState::NoCheck, RangeState::NoCheck, RangeState::NoCheck};

using UnitCanAttack_t = bool(__thiscall *)(void *attacker, void *target);

// Pure geometry probe for Ranged -- the two ranged-weapon families.
constexpr int kAutoShotSpellId = 75;       // bow/gun/crossbow, RangeIndex 114 (8-35yd)
constexpr int kShootSpellId = 5019;        // wand, RangeIndex 4 (0-30yd)

// While actively auto-shooting, the exact spell in flight is the truest
// probe; otherwise pick by the equipped ranged weapon's ammo type (bows/
// guns/crossbows consume ammo, wands don't -- the same distinction the
// server itself uses to route SPELL_ATTR_EX2_AUTOREPEAT_FLAG casts).
int RangedProbeSpellId() {
    const auto active = Game::Read<uint32_t>(
        static_cast<uintptr_t>(Offsets::VAR_ACTIVE_AUTO_REPEAT_SPELL));
    if (active != 0)
        return static_cast<int>(active);

    const uint8_t *cgItem = Item::Location::ResolveEquipmentSlot(18); // ranged slot
    if (cgItem != nullptr) {
        const uint8_t *instance = Item::InstanceBlock(cgItem);
        if (instance != nullptr) {
            const uint32_t itemID = *reinterpret_cast<const uint32_t *>(
                instance + Offsets::OFF_INSTANCE_BLOCK_ITEM_ID);
            const uint8_t *stats = Item::PeekRecord(itemID);
            if (stats != nullptr) {
                const uint32_t ammoType = *reinterpret_cast<const uint32_t *>(
                    stats + Offsets::OFF_ITEMSTATS_AMMO_TYPE);
                return ammoType != 0 ? kAutoShotSpellId : kShootSpellId;
            }
        }
    }
    return kAutoShotSpellId;
}

RangeState Evaluate(SwingType type) {
    if (Combat::Swing::AttackTimeMs(type) == 0)
        return RangeState::NoCheck; // no weapon equipped for this swing type

    void *player = Unit::Position::ResolveToken("player");
    void *target = Unit::Position::ResolveToken("target");
    if (player == nullptr || target == nullptr)
        return RangeState::NoCheck; // no current target

    auto canAttack = reinterpret_cast<UnitCanAttack_t>(Offsets::FUN_UNIT_CAN_ATTACK);
    if (!canAttack(player, target))
        return RangeState::NoCheck; // target can't be attacked

    if (type != RANGED)
        return Combat::Swing::InMeleeRange(player, target) ? RangeState::InRange
                                                            : RangeState::OutOfRange;

    const int result = Spell::Range::PlayerVsUnit(RangedProbeSpellId(), "target");
    if (result < 0)
        return RangeState::NoCheck;
    return result != 0 ? RangeState::InRange : RangeState::OutOfRange;
}

// --- C_SwingTimer.EnableRangeCheck(swingType, enable) ----------------------

int __fastcall Script_EnableRangeCheck(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_SwingTimer.EnableRangeCheck(swingType, enable)");
        return 0;
    }
    const int type = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (type < 0 || type > 2)
        return 0;
    const bool enable = Game::Lua::ToBoolean(L, 2) != 0;
    g_enabled[type] = enable;
    // Seed the change-detector without firing -- callers are expected to
    // read the current state via IsTargetWithinSwingRange right after
    // enabling (exactly what SwingTimerMixin:UpdateRangeState does), not
    // wait for a synthetic first event.
    g_lastState[type] = enable ? Evaluate(static_cast<SwingType>(type)) : RangeState::NoCheck;
    return 0;
}

// --- C_SwingTimer.IsTargetWithinSwingRange(swingType) -> isInRange ---------

int __fastcall Script_IsTargetWithinSwingRange(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_SwingTimer.IsTargetWithinSwingRange(swingType)");
        return 0;
    }
    const int type = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (type < 0 || type > 2) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const RangeState s = Evaluate(static_cast<SwingType>(type));
    if (s == RangeState::NoCheck)
        Game::Lua::PushNil(L);
    else
        Game::Lua::PushBool(L, s == RangeState::InRange);
    return 1;
}

// --- PLAYER_SWING_RANGE_UPDATE(swingType, isInRange, checksRange) ----------

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("swingType", "PlayerSwingType", "Which attack's range changed."),
    Game::Doc::Req("isInRange", "bool",
                   "Whether the current target is within auto attack range. "
                   "Should not be used if checksRange is false."),
    Game::Doc::Req("checksRange", "bool",
                   "False when no range check could be made, for example there "
                   "is no current target."),
};
const Game::Doc::Event kRangeUpdateDoc{
    "SwingTimer",
    "Fires when the current target moves in or out of range for a swing type "
    "that has EnableRangeCheck on.",
    kPayload};
const Event::Custom::AutoReserve _evt{"PLAYER_SWING_RANGE_UPDATE", &kRangeUpdateDoc};

// The engine's printf-style event dispatcher has no `%b` and a plain `%d`
// with `0` pushes the NUMBER 0 (truthy in Lua) -- so each bool picks
// between `%d`+`1` and `%s`+NULL (tail-jumps to `lua_pushnil`), the same
// 1/nil idiom `Event::Custom::FireIdSuccess` uses. `isInRange` is only ever
// true when `checksRange` is also true (see `Evaluate`), so three shapes
// cover every reachable state.
void FireRangeUpdate(int slot, int type, bool isInRange, bool checksRange) {
    if (isInRange)
        Event::Custom::Fire(slot, "%d%d%d", type, 1, 1);
    else if (checksRange)
        Event::Custom::Fire(slot, "%d%s%d", type, static_cast<const char *>(nullptr), 1);
    else
        Event::Custom::Fire(slot, "%d%s%s", type, static_cast<const char *>(nullptr),
                            static_cast<const char *>(nullptr));
}

void OnWorldTick() {
    const int slot = _evt.Slot();
    for (int t = 0; t < 3; ++t) {
        if (!g_enabled[t])
            continue;
        const RangeState cur = Evaluate(static_cast<SwingType>(t));
        if (cur == g_lastState[t])
            continue;
        g_lastState[t] = cur;
        if (slot < 0)
            continue;
        FireRangeUpdate(slot, t, cur == RangeState::InRange, cur != RangeState::NoCheck);
    }
}

const Tick::WorldTick::AutoSubscribe _tickSub{&OnWorldTick};

// --- Registration -----------------------------------------------------------

const Game::Doc::Field kEnableArgs[] = {
    Game::Doc::Req("swingType", "PlayerSwingType"),
    Game::Doc::Req("enable", "bool",
                   "True if changes in range for the swing type should dispatch "
                   "PLAYER_SWING_RANGE_UPDATE. False if the swing type no longer "
                   "needs the event."),
};
const Game::Doc::Function kEnableDoc{
    "Used with PLAYER_SWING_RANGE_UPDATE to be informed when the current target "
    "enters or leaves range for a swing type.",
    kEnableArgs};

const Game::Doc::Field kIsInRangeArgs[] = {
    Game::Doc::Req("swingType", "PlayerSwingType"),
};
const Game::Doc::Field kIsInRangeRets[] = {
    Game::Doc::Opt("isInRange", "bool", nullptr,
                   "Nil when no range check could be made, for example there is "
                   "no target, the target cannot be attacked, or no weapon is "
                   "equipped for the swing type. Nil must not be treated as out "
                   "of range."),
};
const Game::Doc::Function kIsInRangeDoc{
    "Whether the current target is within range of the player's auto attack for "
    "the given swing type. Auto attacks only ever apply to the current target, "
    "so no other unit can be queried.",
    kIsInRangeArgs, kIsInRangeRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_SwingTimer", "EnableRangeCheck",
                                     &Script_EnableRangeCheck, &kEnableDoc);
    Game::Lua::RegisterTableFunction("C_SwingTimer", "IsTargetWithinSwingRange",
                                     &Script_IsTargetWithinSwingRange, &kIsInRangeDoc);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Combat::SwingRange
