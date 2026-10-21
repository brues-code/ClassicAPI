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

// The loose target and SoftTargetForce — retail's `IsTargetLoose()` and
// "Auto-set target to match soft target. 1 = for enemies, 2 = for friends".
//
// A target is locked when it was chosen (a click, Tab, TargetUnit, ...) and
// loose when the soft targets set it. Every selection goes through 1.12's
// setter, FUN_SET_SELECTION_BY_GUID (CGGameUI::SelectTarget), so a hook on it
// sees each choice: any select it did not make loose locks the target,
// re-selecting the loose target included (the setter itself does nothing
// for the current target, but the click still means "lock it").
//
// Retail 12.x probes, SoftTargetForce 1, one or two dummies:
//   - walking until another dummy is the nearer soft enemy: the loose target
//     follows it;
//   - turning the camera until no soft enemy is left: the loose target is
//     cleared, and IsTargetLoose is false;
//   - clicking the loose target: it stays, now locked (IsTargetLoose false);
//   - `/cleartarget`, standing still with the same soft enemy: the target
//     stays empty, and IsTargetLoose is TRUE; with Force 0 it is false;
//   - running around with a soft enemy and no target never selects one: a
//     loose target only comes from a spell cast on the soft enemy.
// So Force moves a loose target, and never makes one: when the soft unit
// changes while a loose target is selected, the new soft unit becomes the
// loose target, or with no soft unit the loose target is cleared. An empty
// or locked target is never touched. IsTargetLoose is retail's flag as the
// probes show it, not "a loose target is selected": a clear the player
// makes while Force is on leaves it set.

#include "target/Loose.h"

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "object/Resolve.h"
#include "target/SoftUnit.h"
#include "tick/FrameTick.h"

namespace Target::Loose {

namespace {

using Select_t = void(__stdcall *)(uint32_t guidLo, uint32_t guidHi);

Select_t g_selectOriginal = nullptr;

bool g_loose = false;      // IsTargetLoose
uint64_t g_looseGuid = 0;  // the loose target SetLoose selected, or 0
bool g_quiet = false; // a select made here (or not a choice) is running

CVar::Factory::Handle g_force = nullptr;

void Select(uint64_t guid) {
    reinterpret_cast<Select_t>(Offsets::FUN_SET_SELECTION_BY_GUID)(
        static_cast<uint32_t>(guid), static_cast<uint32_t>(guid >> 32));
}

// SoftTargetForce's side: 1 enemies, 2 friends.
bool ForceSide(SoftUnit::Side &side) {
    switch (CVar::Factory::GetInt(g_force)) {
    case 1:
        side = SoftUnit::Side::Enemy;
        return true;
    case 2:
        side = SoftUnit::Side::Friend;
        return true;
    default:
        return false;
    }
}

bool ForceOn() {
    SoftUnit::Side side;
    return ForceSide(side) && SoftUnit::Enabled(side);
}

void __stdcall Select_h(uint32_t guidLo, uint32_t guidHi) {
    g_selectOriginal(guidLo, guidHi);
    if (g_quiet)
        return;
    // Only a choice that took counts (the setter refuses while charmed, and
    // binds the object to an awaiting spell instead): a target chosen is
    // locked; a clear is loose while Force is on.
    const uint64_t guid = (static_cast<uint64_t>(guidHi) << 32) | guidLo;
    if (Selection() == guid) {
        g_loose = guid == 0 && ForceOn();
        g_looseGuid = 0;
    }
}

const Game::HookAutoRegister _hook{Offsets::FUN_SET_SELECTION_BY_GUID,
                                   reinterpret_cast<void *>(&Select_h),
                                   reinterpret_cast<void **>(&g_selectOriginal)};

// The soft unit the last frame saw, while a loose target is selected and
// Force is on. The first such frame only takes it in.
bool g_watching = false;
uint64_t g_lastSoft = 0;

void OnFrameTick() {
    if (!Object::RegistryReady()) {
        g_loose = false;
        g_looseGuid = 0;
        g_watching = false;
        return;
    }
    // Nothing to move without a loose target: no pick runs per frame then.
    SoftUnit::Side side;
    if (g_looseGuid == 0 || Selection() != g_looseGuid || !ForceSide(side) ||
        !SoftUnit::Enabled(side)) {
        g_watching = false;
        return;
    }
    const uint64_t soft = SoftUnit::Get(side);
    if (g_watching && soft != g_lastSoft)
        SetLoose(soft);
    g_watching = true;
    g_lastSoft = soft;
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
    "Whether the current target is loose: set by soft targeting (SoftTargetForce), not chosen.",
    {}, kIsTargetLooseRets, "TargetScript"};

void Register() {
    g_force = CVar::Factory::Register("SoftTargetForce", "1");
    Game::Lua::RegisterGlobalFunction("IsTargetLoose", &Script_IsTargetLoose, &kIsTargetLoose);
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

uint64_t Selection() { return Game::Read<uint64_t>(Offsets::VAR_CURRENT_SELECTION_GUID_LO); }

bool IsLoose() { return g_loose; }

uint64_t Locked() {
    const uint64_t selection = Selection();
    return selection == g_looseGuid ? 0 : selection;
}

void SelectKeepingLoose(uint64_t guid) {
    const uint64_t before = Selection();
    g_quiet = true;
    Select(guid);
    g_quiet = false;
    if (Selection() != before) {
        g_loose = false;
        g_looseGuid = 0;
    }
}

void SetLoose(uint64_t guid) {
    g_quiet = true;
    Select(guid);
    g_quiet = false;
    g_loose = guid != 0 && Selection() == guid;
    g_looseGuid = g_loose ? guid : 0;
}

} // namespace Target::Loose
