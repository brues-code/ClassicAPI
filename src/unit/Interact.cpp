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

// `InteractUnit(unitToken [, exactMatch])` — 4.3.4's Script_InteractUnit
// (GameUI_Scripts.cpp), the C global behind the INTERACTTARGET and
// INTERACTMOUSEOVER bindings (src/bindings/Bindings.xml).
//
// 4.3.4 resolves the argument, then hands the GUID to
// CGGameUI::OnSpriteRightClick — the same function a mouse right-click in
// the world runs. 1.12 has that function whole (FUN_ON_SPRITE_RIGHT_CLICK),
// so this is a call, not a reconstruction: targeting, the object's own
// right-click (vendor, loot, gossip, mailbox, …) and the autoloot modifier
// all come from the engine.
//
// Resolution, as 4.3.4 does it:
//   - "mouseover" reads the hovered-object GUID directly
//     (VAR_MOUSEOVER_GUID_*, 4.3.4's m_currentObjectTrack), which is any
//     object type — so the mouseover binding works on game objects and
//     corpses, not only units.
//   - anything else goes through 4.3.4's ClosestObjectMatch(token,
//     TYPEMASK_OBJECT, 0, exact, FLT_MAX): a unit token resolves to its
//     GUID (an empty token is no match, never a name search); otherwise the
//     engine's by-name search runs. 1.12's by-name search
//     (FUN_UNIT_FIND_BY_NAME) lacks 4.3.4's leading token step, so the
//     token step is IsUnitToken + FUN_TOKEN_TO_GUID here.
//
// `C_PlayerInteractionManager.InteractUnit(unit [, exactMatch [,
// looseTargeting]])` is the modern name the INTERACTTARGET /
// INTERACTMOUSEOVER bindings call (with "anyinteract", see
// target/SoftInteract.cpp): the same resolve and right-click, returning a
// boolean. looseTargeting (default true) keeps the current target: on
// retail 12.1.5 the Interact Key on a nearer NPC opens its dialog while a
// farther unit stays targeted, and with no target it targets the NPC. Only
// a unit other than the target would be selected, so only then does the
// right-click run without its select (InteractKeepingTarget); everything
// else is the engine's own right-click, which nampower also detours.
//
// 4.3.4's taint gate is dropped: 1.12 has no secure environment, and every
// vanilla action function (TargetUnit, CastSpellByName, …) is callable.

#include "Game.h"
#include "Offsets.h"
#include "input/Modifier.h"
#include "object/Resolve.h"
#include "unit/Identity.h"
#include "unit/TokenResolve.h"

#include <cstdint>
#include <cstring>

namespace Unit::Interact {

namespace {

using OnSpriteRightClick_t = int(__stdcall *)(uint32_t guidLo, uint32_t guidHi);
using OnRightClick_t = void(__thiscall *)(void *object, int modifier);
using TokenToGuid_t = uint64_t(__fastcall *)(const char *token);

uint64_t ResolveGuid(void *L, const char *token) {
    if (_stricmp(token, "mouseover") == 0)
        return Game::Read<uint64_t>(Offsets::VAR_MOUSEOVER_GUID_LO);
    if (Unit::TokenResolve::IsUnitToken(token))
        return reinterpret_cast<TokenToGuid_t>(Offsets::FUN_TOKEN_TO_GUID)(token);
    return Unit::Identity::FindGuidByName(token, Offsets::TYPEMASK_OBJECT,
                                          Game::Lua::ToFlag(L, 2));
}

// FUN_ON_SPRITE_RIGHT_CLICK without its select (FUN_SET_SELECTION_BY_GUID):
// the active-player gate, then the object's right-click virtual.
bool InteractKeepingTarget(void *object) {
    if (Object::ByGuid(Offsets::TYPEMASK_PLAYER, Unit::Identity::PlayerGuid()) == nullptr)
        return true;
    const int modifier = Input::Modifier::IsKeyDown(0) ? 1 : 0;
    const auto vtable = *static_cast<uintptr_t *const *>(object);
    reinterpret_cast<OnRightClick_t>(
        vtable[Offsets::OFF_OBJECT_VTABLE_ON_RIGHT_CLICK / sizeof(uintptr_t)])(object, modifier);
    return true;
}

bool Interact(void *L, bool loose) {
    const uint64_t guid = ResolveGuid(L, Game::Lua::ToString(L, 1));
    const uint64_t target = Game::Read<uint64_t>(Offsets::VAR_CURRENT_SELECTION_GUID_LO);
    if (loose && target != 0 && guid != target) {
        // The select stores only units (slot 0x58), so only a unit would move
        // the target.
        if (void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid))
            return InteractKeepingTarget(unit);
    }
    return reinterpret_cast<OnSpriteRightClick_t>(Offsets::FUN_ON_SPRITE_RIGHT_CLICK)(
               static_cast<uint32_t>(guid), static_cast<uint32_t>(guid >> 32)) != 0;
}

int __fastcall Script_InteractUnit(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: InteractUnit(unitToken[, exactMatch])");
        return 0;
    }
    if (Interact(L, false))
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

int __fastcall Script_C_InteractUnit(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: C_PlayerInteractionManager.InteractUnit(unit[, exactMatch[, "
                            "looseTargeting]])");
        return 0;
    }
    const bool loose = Game::Lua::Type(L, 3) <= Game::Lua::TYPE_NIL || Game::Lua::ToFlag(L, 3);
    Game::Lua::PushBool(L, Interact(L, loose));
    return 1;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("unit", "UnitToken",
                   "A unit token, \"mouseover\" for whatever the cursor is over, or a name."),
    Game::Doc::Opt("exactMatch", "bool", "false",
                   "When a name is given, require the whole name instead of a prefix."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("interacted", "number", nullptr,
                   "1 when the right-click ran, nil when nothing matched."),
};
const Game::Doc::Function kInteractUnit{
    "Targets the unit or object and interacts with it, as a right-click in the world does.",
    kArgs, kRets, "UnitGlobals"};

const Game::Doc::Field kManagerArgs[] = {
    Game::Doc::Req("unit", "UnitToken",
                   "A unit token, \"mouseover\" for whatever the cursor is over, or a name. "
                   "\"anyinteract\" is what the Interact Key uses."),
    Game::Doc::Opt("exactMatch", "bool", "false",
                   "When a name is given, require the whole name instead of a prefix."),
    Game::Doc::Opt("looseTargeting", "bool", "true",
                   "Keep the current target: the unit becomes the target only when there is "
                   "none."),
};
const Game::Doc::Field kManagerRets[] = {
    Game::Doc::Req("success", "bool", "True when the interaction ran."),
};
const Game::Doc::Function kManagerInteractUnit{
    "Interacts with the unit or object, as a right-click in the world does.",
    kManagerArgs, kManagerRets, "PlayerInteractionManager"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("InteractUnit", &Script_InteractUnit, &kInteractUnit);
    Game::Lua::RegisterTableFunction("C_PlayerInteractionManager", "InteractUnit",
                                     &Script_C_InteractUnit, &kManagerInteractUnit);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::Interact
