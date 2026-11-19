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
// (set by the game); the rest are locked. 3.80.2's set-target
// (FUN_1405ddef0) applies, in order:
//   - a loose reason never replaces a locked target (silently dropped); over
//     a loose target, Auto (1) only replaces another Auto target, with
//     another unit, while the player is not attacking or auto-repeating and
//     the old one has left the view;
//   - Force (4) over a loose target keeps that target's reason; Attack (3)
//     is stored as 2, and so is Auto with TargetAutoLock on (its default,
//     Classic Era's and 3.80.2's);
//   - the same GUID again only raises the reason, and a raise fires
//     PLAYER_TARGET_CHANGED;
//   - a Force target fires no PLAYER_TARGET_CHANGED, and the `target` token
//     resolves nothing for it (the token resolver: `reason == 4 ? 0 :
//     target`). The engine target itself is set: the server selection, the
//     selection circle, and a running melee attack moves to it
//     (Combat::TargetChange's restart).
//
// 1.12 has one target setter, FUN_SET_SELECTION_BY_GUID, so the reason is
// recorded around it: Set() hands its reason to the hook, a select called
// from one of the world-click handlers is a Click, and a select from
// anywhere else in the engine (TargetUnit, assist, 1.12's own Tab, ...) is
// a choice — Chosen. The reason is in place before the original runs, so
// the setter's own PLAYER_TARGET_CHANGED fire (redirected in
// Combat::TargetChange) sees it. Each new target, and each clear, is also
// handed to Target::Tab, which keeps its cycle state from them.
//
// The soft-target driver (Target::SoftUnit) runs the per-frame steps in
// 3.80.2's order (FUN_1405dd710 around its soft driver):
//   - DropVanished: a loose target whose object is gone is cleared;
//   - ApplyForce, after the soft picks: SoftTargetForce (1 enemies, 2
//     friends) sets the target to the soft
//     unit with Force while there is no target or a loose one; with no soft
//     unit and that side's arc 0, it clears the loose target instead. Read
//     literally: the friend side runs whenever the enemy side set nothing,
//     also when the target already is the soft enemy (Force 3 swaps between
//     the two each frame — 3.80.2 does the same; 3 is not a documented value);
//   - Expire, on a frame that rebuilt the candidate list: a Force target
//     that is no longer a soft target is cleared; any other loose target is
//     kept while it is a candidate in view and range (in combat lock: in
//     melee, or in the frustum too).
//
// A running melee attack is not carried to an Auto target
// (Combat::TargetChange).

#include "target/Reason.h"

#include "Game.h"
#include "Offsets.h"
#include "cvar/Character.h"
#include "cvar/Factory.h"
#include "object/Resolve.h"
#include "target/Priority.h"
#include "target/SoftInteract.h"
#include "target/SoftUnit.h"
#include "target/Tab.h"
#include "tick/FrameTick.h"
#include "unit/Identity.h"

#include <intrin.h>

namespace Target::Reason {

namespace {

using Select_t = void(__stdcall *)(uint32_t guidLo, uint32_t guidHi);
using FireEvent_t = void(__fastcall *)(int eventID);

Select_t g_selectOriginal = nullptr;

uint64_t g_guid = 0;   // the target the reason belongs to
Why g_reason = None;   // its reason
Why g_pending = None;  // Set()'s reason for the select it is making
bool g_reforce = false; // a clear asks for SoftTargetForce next frame

CVar::Factory::Handle g_force = nullptr;
CVar::Factory::Handle g_autoLock = nullptr;

bool IsLooseWhy(Why why) {
    return why == Auto || why == Force || why == InteractKey || why == AutoSpell;
}

// The player is busy with the target: attacking it, or an auto-repeat spell
// (Auto Shot) running.
bool Engaged() {
    const auto *player = Unit::Identity::PlayerObject();
    return (player != nullptr && Game::Read<uint64_t>(player, Offsets::OFF_UNIT_ATTACK_TARGET) != 0) ||
           Game::Read<int>(Offsets::VAR_ACTIVE_AUTO_REPEAT_SPELL) != 0;
}

// 3.80.2's loose-reason gate: a loose reason takes an empty target, never a
// locked one. Over a loose target, Force, InteractKey and AutoSpell always
// take it; Auto only replaces another Auto target, with another unit, while
// the player is not engaged and the old target has left the view (no longer
// a candidate in the frustum and not excluded).
bool Admit(Why why, uint64_t guid, uint64_t target, Why current) {
    if (!IsLooseWhy(why) || target == 0)
        return true;
    if (!IsLooseWhy(current))
        return false;
    if (why != Auto)
        return true;
    if (why < current || guid == target || Engaged())
        return false;
    const uint32_t flags = Priority::FlagsOf(target);
    return (flags & (Priority::InFrustum | Priority::Excluded)) != Priority::InFrustum;
}

void Select(uint64_t guid) {
    reinterpret_cast<Select_t>(Offsets::FUN_SET_SELECTION_BY_GUID)(
        static_cast<uint32_t>(guid), static_cast<uint32_t>(guid >> 32));
}

// An engine select's reason, by its call site: a world click, or a choice.
Why EngineWhy(uintptr_t returnAddress) {
    return returnAddress == Offsets::RET_CLICK_SELECT ||
                   returnAddress == Offsets::RET_SPRITE_RIGHT_CLICK_SELECT
               ? Click
               : Chosen;
}

void __stdcall Select_h(uint32_t guidLo, uint32_t guidHi) {
    const uint64_t guid = (static_cast<uint64_t>(guidHi) << 32) | guidLo;
    Why why = g_pending != None ? g_pending
                                : EngineWhy(reinterpret_cast<uintptr_t>(_ReturnAddress()));
    g_pending = None;
    const uint64_t target = Selection();
    const Why current = Current();

    if (!Admit(why, guid, target, current))
        return;
    if (why == Force && target != 0)
        why = current;
    else if (why == Attack || (why == Auto && CVar::Factory::GetInt(g_autoLock) != 0))
        why = Locked;

    if (guid == 0) {
        g_selectOriginal(guidLo, guidHi);
        if (Selection() == 0) {
            g_guid = 0;
            g_reason = None;
            Tab::OnClear();
            if (why != Force)
                g_reforce = true;
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
        return;
    }
    Tab::OnSet(target, guid, why);
}

const Game::HookAutoRegister _hook{Offsets::FUN_SET_SELECTION_BY_GUID,
                                   reinterpret_cast<void *>(&Select_h),
                                   reinterpret_cast<void **>(&g_selectOriginal)};

// 3.80.2's next soft-driver pass sets the Force target again before anything
// draws. Ours runs at the tail of the UI render, so a clear between two
// frames (a terrain click with deselectOnClick, Escape) would draw one frame
// without it: the clear asks for SoftTargetForce at the head of the next
// frame instead. Never from inside the setter — the engine also clears the
// selection while it tears the world down (0x00490BDF), and a select then
// resolves a GUID in a manager that is gone.
void OnFrameHead() {
    if (!g_reforce)
        return;
    g_reforce = false;
    if (Game::Read<uint8_t>(Offsets::VAR_IN_WORLD) != 0 && Object::RegistryReady())
        ApplyForce();
}

const Tick::FrameTick::AutoSubscribeBefore _head{&OnFrameHead};

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

bool IsSoftTarget(uint64_t guid) {
    return guid == SoftUnit::Get(SoftUnit::Side::Enemy) ||
           guid == SoftUnit::Get(SoftUnit::Side::Friend) || guid == SoftInteract::Get();
}

// ClearTarget, as 3.80.2's (FUN_140ded3b0): it clears the target as 1.12's
// does, but answers true only for a target that is not a loose soft target
// (the soft interact, enemy or friend), or while the player attacks. So
// Escape (ToggleGameMenu's `elseif ( ClearTarget() )`) opens the game menu
// over a SoftTargetForce target, which Force sets again.
Game::Lua::CFunction g_clearTargetOriginal = nullptr;

int __fastcall ClearTarget_h(void *L) {
    const uint64_t target = Selection();
    const auto *player = Unit::Identity::PlayerObject();
    const bool attacking =
        player != nullptr && Game::Read<uint64_t>(player, Offsets::OFF_UNIT_ATTACK_TARGET) != 0;
    const bool cleared = (target != 0 && !(IsLoose() && IsSoftTarget(target))) || attacking;
    const int results = g_clearTargetOriginal(L);
    Game::Lua::SetTop(L, -2);
    Game::Lua::PushFlag(L, cleared);
    return results;
}

const Game::HookAutoRegister _clearTargetHook{Offsets::FUN_SCRIPT_CLEAR_TARGET,
                                              reinterpret_cast<void *>(&ClearTarget_h),
                                              reinterpret_cast<void **>(&g_clearTargetOriginal)};

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
    g_autoLock = CVar::Character::Register("TargetAutoLock", "Lock targets auto-set by the game", Offsets::CVAR_CATEGORY_GAME, "1");
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

bool Hidden() { return Current() == Force; }

void DropVanished() {
    if (!Object::RegistryReady()) {
        g_guid = 0;
        g_reason = None;
        return;
    }
    if (IsLoose() && Object::ByGuid(Offsets::TYPEMASK_OBJECT, Selection()) == nullptr)
        Set(0, Chosen);
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

void Expire() {
    if (!IsLoose())
        return;
    const uint64_t target = Selection();
    if (Current() == Force) {
        if (!IsSoftTarget(target))
            Set(0, Chosen);
        return;
    }
    const uint32_t flags = Priority::FlagsOf(target);
    if ((flags & Priority::CombatLock) != 0 && (flags & Priority::InMelee) != 0)
        return;
    const uint32_t need = (flags & Priority::CombatLock) != 0
                              ? (Priority::InFrustum | Priority::Visible | Priority::InRange)
                              : (Priority::Visible | Priority::InRange);
    if ((flags & need) != need)
        Set(0, Chosen);
}

void Set(uint64_t guid, Why why) {
    g_pending = why;
    Select(guid);
    g_pending = None;
}

void Promote(Why why) {
    const uint64_t selection = Selection();
    if (selection == 0)
        return;
    g_guid = selection;
    g_reason = why;
}

} // namespace Target::Reason
