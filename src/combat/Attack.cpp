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

// `StartAttack([target])` / `StopAttack()` — the non-toggling melee
// auto-attack verbs. Vanilla 1.12 ships only `AttackTarget()`, which
// *toggles* (start <-> stop each call), so a `/startattack` can't guarantee
// it won't cancel an in-progress swing. These split the engine's own attack
// machinery into the two explicit, idempotent verbs modern clients expose.
//
// 1.12's `AttackTarget` (`0x00489B50`) is `FUN_006131A0`: resolve the target,
// then `isAttacking ? stop : start`. We call the same start/stop primitives
// directly (see the `FUN_ATTACK_*` notes in Offsets.h), skipping the toggle:
//   StartAttack -> the "start" leg only (idempotent; switches or no-ops on a
//                  valid target, never stops it).
//   StopAttack  -> the "stop" leg only (self-guards, so a no-op when idle).

#include "Game.h"
#include "Offsets.h"
#include "target/SoftAttack.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Combat::Attack {

namespace {

using Start_t = void(__thiscall *)(void *player, const uint64_t *targetGuid);
using Stop_t = void(__fastcall *)(void *player);

// Resolve an optional unit-token argument to a GUID without raising.
// `Unit::Identity::GuidForToken` raises for non-token strings (a name or
// garbage — 1.12 has no name->GUID resolver), so run it under `pcall`; a raise
// or an empty token yields 0, which the caller treats as "use current target".
int __fastcall ResolveTokenThunk(void *L) {
    using namespace Game::Lua;
    const char *token = ToString(L, 1);
    const uint64_t g = (token && *token) ? Unit::Identity::GuidForToken(token) : 0;
    PushNumber(L, static_cast<double>(static_cast<uint32_t>(g)));
    PushNumber(L, static_cast<double>(static_cast<uint32_t>(g >> 32)));
    return 2;
}

uint64_t SafeResolveToken(void *L, const char *token) {
    using namespace Game::Lua;
    if (token == nullptr || *token == '\0')
        return 0;

    const int base = GetTop(L);
    PushCClosure(L, &ResolveTokenThunk, 0);
    PushString(L, token);
    uint64_t guid = 0;
    if (PCall(L, 1, 2, 0) == 0) {
        const uint32_t lo = static_cast<uint32_t>(ToNumber(L, -2));
        const uint32_t hi = static_cast<uint32_t>(ToNumber(L, -1));
        guid = (static_cast<uint64_t>(hi) << 32) | lo;
    }
    SetTop(L, base); // drop the results (or the caught error message)
    return guid;
}

} // namespace

static int __fastcall Script_StartAttack(void *L) {
    auto *player = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    if (player == nullptr)
        return 0;

    uint64_t target = SafeResolveToken(L, Game::Lua::ToString(L, 1));
    if (target == 0) {
        // No explicit target given (or unresolvable): validate and take the
        // current target, or with none the soft enemy (action targeting).
        // Returns 0 — and posts the engine's own UI error — when there's
        // nothing valid to attack.
        if (Target::SoftAttack::ResolveMelee(player, &target) == 0)
            return 0;
    }

    reinterpret_cast<Start_t>(Offsets::FUN_ATTACK_START)(player, &target);
    return 0;
}

static int __fastcall Script_StopAttack(void *L) {
    (void)L;
    auto *player = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    if (player == nullptr)
        return 0;

    reinterpret_cast<Stop_t>(Offsets::FUN_ATTACK_STOP)(player);
    return 0;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("StartAttack", &Script_StartAttack);
    Game::Lua::RegisterGlobalFunction("StopAttack", &Script_StopAttack);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Combat::Attack
