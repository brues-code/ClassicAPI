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

// Target reasons, SoftTargetForce and IsTargetLoose — the 3.80.2 Classic
// client's model (docs/notes/soft-targeting-3802.md, "set-target reasons"
// and "SoftTargetForce in detail").
//
// Every target carries the reason it was set for. 1, 4, 5 and 6 are loose
// (set by soft targeting); the rest are locked. 3.80.2's set-target
// (FUN_1405ddef0) applies, in order:
//   - a loose reason never replaces a locked target (silently dropped);
//   - Force (4) over a loose target keeps that target's reason; Attack (3)
//     is stored as 2;
//   - the same GUID again only raises the reason, and a raise fires
//     PLAYER_TARGET_CHANGED;
//   - a Force target fires no PLAYER_TARGET_CHANGED, and the `target` token
//     resolves nothing for it (the token resolver: `reason == 4 ? 0 :
//     target`). The engine target itself is set: the server selection, the
//     selection circle, and a running melee attack moves to it
//     (Combat::TargetChange's restart).
//
// 1.12 has one target setter, FUN_SET_SELECTION_BY_GUID, so the reason is
// recorded around it: Set() hands its reason to the hook, and a select from
// anywhere else in the engine (a click, Tab, TargetUnit, assist, ...) is a
// choice — Chosen. The reason is in place before the original runs, so the
// setter's own PLAYER_TARGET_CHANGED fire (redirected in
// Combat::TargetChange) sees it.
//
// Each frame, after the soft picks (3.80.2's FUN_1405dd710 around its soft
// driver):
//   - a loose target whose object is gone is cleared;
//   - SoftTargetForce (1 enemies, 2 friends) sets the target to the soft
//     unit with Force while there is no target or a loose one; with no soft
//     unit and that side's arc 0, it clears the loose target instead. Read
//     literally: the friend side runs whenever the enemy side set nothing,
//     also when the target already is the soft enemy (Force 3 swaps between
//     the two each frame — 3.80.2 does the same; 3 is not a documented value);
//   - a Force target that is no longer a soft target is cleared.
//
// Not yet: reasons 1 (TargetEnemyAttacker, the interact key) and 5 (the
// interact key with looseTargeting), and the candidate-list part of the
// expiry (a loose target that leaves line of sight or range) — both come
// with the Target Priority picker.

#include "target/Reason.h"

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "object/Resolve.h"
#include "target/SoftInteract.h"
#include "target/SoftUnit.h"
#include "tick/FrameTick.h"

namespace Target::Reason {

namespace {

using Select_t = void(__stdcall *)(uint32_t guidLo, uint32_t guidHi);
using FireEvent_t = void(__fastcall *)(int eventID);

Select_t g_selectOriginal = nullptr;

uint64_t g_guid = 0;   // the target the reason belongs to
Why g_reason = None;   // its reason
Why g_pending = None;  // Set()'s reason for the select it is making

CVar::Factory::Handle g_force = nullptr;

bool IsLooseWhy(Why why) { return why == Force || why == AutoSpell; }

void Select(uint64_t guid) {
    reinterpret_cast<Select_t>(Offsets::FUN_SET_SELECTION_BY_GUID)(
        static_cast<uint32_t>(guid), static_cast<uint32_t>(guid >> 32));
}

void __stdcall Select_h(uint32_t guidLo, uint32_t guidHi) {
    const uint64_t guid = (static_cast<uint64_t>(guidHi) << 32) | guidLo;
    Why why = g_pending != None ? g_pending : Chosen;
    g_pending = None;
    const uint64_t target = Selection();
    const Why current = Current();

    if (IsLooseWhy(why) && target != 0 && !IsLooseWhy(current))
        return;
    if (why == Force && target != 0)
        why = current;
    else if (why == Attack)
        why = Locked;

    if (guid == 0) {
        g_selectOriginal(guidLo, guidHi);
        if (Selection() == 0) {
            g_guid = 0;
            g_reason = None;
        }
        return;
    }
    // A click on the target while a spell waits for one binds the spell: the
    // setter's own early-out, and no raise.
    if (guid == target) {
        g_selectOriginal(guidLo, guidHi);
        if (Game::Read<uint32_t>(Offsets::VAR_SPELL_TARGETING_FLAGS) == 0 && why > current) {
            g_guid = guid;
            g_reason = why;
            reinterpret_cast<FireEvent_t>(Offsets::FUN_FIRE_EVENT_NO_ARGS)(
                Offsets::EVENT_PLAYER_TARGET_CHANGED);
        }
        return;
    }
    const uint64_t oldGuid = g_guid;
    const Why oldReason = g_reason;
    g_guid = guid;
    g_reason = why;
    g_selectOriginal(guidLo, guidHi);
    if (Selection() != guid) {
        // Refused (charmed), or bound to an awaiting spell instead.
        g_guid = oldGuid;
        g_reason = oldReason;
    }
}

const Game::HookAutoRegister _hook{Offsets::FUN_SET_SELECTION_BY_GUID,
                                   reinterpret_cast<void *>(&Select_h),
                                   reinterpret_cast<void **>(&g_selectOriginal)};

// One side of SoftTargetForce. True when it set (or cleared) the target.
bool ForceSide(SoftUnit::Side side, uint64_t target) {
    const uint64_t soft = SoftUnit::Get(side);
    if (soft == target)
        return false;
    if (soft != 0) {
        Set(soft, Force);
        return true;
    }
    if (SoftUnit::Arc(side) != 0)
        return false;
    if (target != 0)
        Set(0, Force);
    return true;
}

void ApplyForce() {
    const uint64_t target = Selection();
    if (target != 0 && !IsLoose())
        return;
    const int force = CVar::Factory::GetInt(g_force);
    if ((force & 1) != 0 && ForceSide(SoftUnit::Side::Enemy, target))
        return;
    if ((force & 2) != 0)
        ForceSide(SoftUnit::Side::Friend, target);
}

bool IsSoftTarget(uint64_t guid) {
    return guid == SoftUnit::Get(SoftUnit::Side::Enemy) ||
           guid == SoftUnit::Get(SoftUnit::Side::Friend) || guid == SoftInteract::Get();
}

void OnFrameTick() {
    if (!Object::RegistryReady()) {
        g_guid = 0;
        g_reason = None;
        return;
    }
    if (IsLoose() && Object::ByGuid(Offsets::TYPEMASK_OBJECT, Selection()) == nullptr)
        Set(0, Chosen);
    ApplyForce();
    if (Current() == Force && !IsSoftTarget(Selection()))
        Set(0, Chosen);
}

const Tick::FrameTick::AutoSubscribe _tick{&OnFrameTick};

int __fastcall Script_IsTargetLoose(void *L) {
    Game::Lua::PushBool(L, IsLoose());
    return 1;
}

const Game::Doc::Field kIsTargetLooseRets[] = {
    Game::Doc::Req("isTargetLoose", "bool",
                   "True while the target was set by soft targeting, not chosen."),
};
const Game::Doc::Function kIsTargetLoose{
    "Returns whether the current target is loose: set by soft targeting (SoftTargetForce, or "
    "your spell landing on the soft unit), not chosen.",
    {}, kIsTargetLooseRets, "TargetScript"};

void Register() {
    g_force = CVar::Factory::Register("SoftTargetForce", "Auto-set target to match soft target. 1 = for enemies, 2 = for friends", Offsets::CVAR_CATEGORY_GAME, "1");
    Game::Lua::RegisterGlobalFunction("IsTargetLoose", &Script_IsTargetLoose, &kIsTargetLoose);
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

uint64_t Selection() { return Game::Read<uint64_t>(Offsets::VAR_CURRENT_SELECTION_GUID_LO); }

Why Current() {
    const uint64_t selection = Selection();
    if (selection == 0)
        return None;
    // A target that changed without the setter (no such path is known) is
    // taken as chosen.
    return selection == g_guid ? g_reason : Chosen;
}

bool IsLoose() { return IsLooseWhy(Current()); }

uint64_t LockedTarget() { return IsLoose() ? 0 : Selection(); }

bool Hidden() { return Current() == Force; }

void Set(uint64_t guid, Why why) {
    g_pending = why;
    Select(guid);
    g_pending = None;
}

} // namespace Target::Reason
