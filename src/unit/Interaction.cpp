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

// The unit interaction tests a gamepad UI asks (docs/notes/
// gamepad-interact-api-3802.md). Each reads its unit with the generated
// UnitToken reader (lua/Arg) and answers one bool, never nil.
//   - UnitIsInteractable([unit]) — 3.80.2's 0x140E3C230: the unit is a soft
//     target candidate with the interact flag, as of the last rebuild
//     (Target::Priority; the flag has no range gate).
//   - UnitIsGameObject([unit]) — 3.80.2's 0x140E3B8A0: the GUID is a game
//     object loaded now. `softinteract` can name one.
//   - UnitHasLootInteraction(unit) — retail only; 3.80.2's interact test has
//     the same loot leg, and 1.12 the same test whole: CGUnit_C::IsLootable
//     (dead, the death delay passed, the lootable dynamic flag).
//   - UnitIsInInteractRange(unit) — retail only; 3.80.2's 0x1405E0EE0 is the
//     same "usable now" test: an interact candidate within the interact
//     reach, 1.12's own hover-cursor reach (Target::SoftInteract::InReach).
//   - SetPreferredGamepadInteractTarget([unit]) — retail only. Retail draws
//     its gamepad interact cursor glyph for that unit (SetUnitCursorTexture's
//     preferGamepadIcon); 1.12 has no such glyph, so the argument is read
//     and nothing is kept.

#include "Game.h"
#include "Offsets.h"
#include "lua/Arg.h"
#include "object/Resolve.h"
#include "target/Priority.h"
#include "target/SoftInteract.h"
#include "time/Clock.h"

#include <cstdint>

namespace Unit::Interaction {

namespace {

namespace Arg = Game::Lua::Arg;

int PushBool(void *L, bool value) {
    Game::Lua::PushBool(L, value);
    return 1;
}

int __fastcall Script_UnitIsInteractable(void *L) {
    uint64_t guid;
    if (!Arg::UnitToken(L, 1, "Usage: local result = UnitIsInteractable([unit])", guid, true))
        return 0;
    return PushBool(L, guid != 0 && Target::Priority::Has(guid, Target::Priority::Interact));
}

int __fastcall Script_UnitIsGameObject(void *L) {
    uint64_t guid;
    if (!Arg::UnitToken(L, 1, "Usage: local result = UnitIsGameObject([unit])", guid, true))
        return 0;
    return PushBool(L, guid != 0 && Object::ByGuid(Offsets::TYPEMASK_GAMEOBJECT, guid) != nullptr);
}

int __fastcall Script_UnitHasLootInteraction(void *L) {
    uint64_t guid;
    if (!Arg::UnitToken(L, 1, "Usage: local result = UnitHasLootInteraction(unit)", guid))
        return 0;
    void *unit = guid != 0 ? Object::ByGuid(Offsets::TYPEMASK_UNIT, guid) : nullptr;
    using IsLootable_t = int(__fastcall *)(void *unit, void *edx, uint32_t now);
    return PushBool(L, unit != nullptr && reinterpret_cast<IsLootable_t>(Offsets::FUN_UNIT_IS_LOOTABLE)(
                                              unit, nullptr, Time::Clock::NowMs()) != 0);
}

int __fastcall Script_UnitIsInInteractRange(void *L) {
    uint64_t guid;
    if (!Arg::UnitToken(L, 1, "Usage: local result = UnitIsInInteractRange(unit)", guid))
        return 0;
    if (guid == 0 || !Target::Priority::Has(guid, Target::Priority::Interact))
        return PushBool(L, false);
    void *object = Object::ByGuid(Offsets::TYPEMASK_UNIT | Offsets::TYPEMASK_GAMEOBJECT, guid);
    return PushBool(L, object != nullptr && Target::SoftInteract::InReach(object));
}

int __fastcall Script_SetPreferredGamepadInteractTarget(void *L) {
    uint64_t guid;
    Arg::UnitToken(L, 1, "Usage: SetPreferredGamepadInteractTarget([unit])", guid, true);
    return 0;
}

const Game::Doc::Field kUnitArg[] = {Game::Doc::Opt("unit", "UnitToken")};
const Game::Doc::Field kRequiredUnitArg[] = {Game::Doc::Req("unit", "UnitToken")};
const Game::Doc::Field kResult[] = {Game::Doc::Req("result", "bool")};

const Game::Doc::Function kUnitIsInteractable{
    "Returns true if the unit is a soft target candidate you can interact with (at any range).", kUnitArg,
    kResult, "Unit"};
const Game::Doc::Function kUnitIsGameObject{"Returns true if the unit token names a game object.", kUnitArg,
                                            kResult, "Unit"};
const Game::Doc::Function kUnitHasLootInteraction{"Returns true if the unit is dead and can be looted now.",
                                                  kRequiredUnitArg, kResult, "Unit"};
const Game::Doc::Function kUnitIsInInteractRange{
    "Returns true if the unit is a soft target candidate you can interact with and it is in reach.",
    kRequiredUnitArg, kResult, "Unit"};
const Game::Doc::Function kSetPreferredGamepadInteractTarget{
    "Names the unit a gamepad interact acts on. This client has no gamepad interact cursor to show for it.",
    kUnitArg, {}, "Target"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitIsInteractable", &Script_UnitIsInteractable, &kUnitIsInteractable);
    Game::Lua::RegisterGlobalFunction("UnitIsGameObject", &Script_UnitIsGameObject, &kUnitIsGameObject);
    Game::Lua::RegisterGlobalFunction("UnitHasLootInteraction", &Script_UnitHasLootInteraction,
                                      &kUnitHasLootInteraction);
    Game::Lua::RegisterGlobalFunction("UnitIsInInteractRange", &Script_UnitIsInInteractRange,
                                      &kUnitIsInInteractRange);
    Game::Lua::RegisterGlobalFunction("SetPreferredGamepadInteractTarget", &Script_SetPreferredGamepadInteractTarget,
                                      &kSetPreferredGamepadInteractTarget);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::Interaction
