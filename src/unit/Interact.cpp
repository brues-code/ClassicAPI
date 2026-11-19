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
// boolean. looseTargeting (default true) sets the target as the 3.80.2
// client's InteractUnit does (FUN_1417409e0): for a `soft` / `any` token
// with InteractKey (and PLAYER_SOFT_TARGET_INTERACTION), for any other token
// with Auto — both loose reasons, so a chosen target stays (retail 12.1.5:
// the Interact Key on a nearer NPC opens its dialog while a farther unit
// stays targeted, and with no target it targets the NPC). Either way it is
// the engine's own right-click, which nampower also detours; with
// looseTargeting its select runs for the loose reason
// (Target::Reason::SelectFor) instead of as a click. Both refuse
// `anyleftinteract`, as 3.80.2's does.
//
// 4.3.4's taint gate is dropped: 1.12 has no secure environment, and every
// vanilla action function (TargetUnit, CastSpellByName, …) is callable.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "object/Resolve.h"
#include "target/Reason.h"
#include "unit/Identity.h"
#include "unit/TokenResolve.h"

#include <cstdint>
#include <cstring>

namespace Unit::Interact {

namespace {

using OnSpriteRightClick_t = int(__stdcall *)(uint32_t guidLo, uint32_t guidHi);

const Game::Doc::Event kInteractionDoc{
    "Unit", "Fires when the interact key uses a soft target.", {}};
const Event::Custom::AutoReserve _interaction{"PLAYER_SOFT_TARGET_INTERACTION",
                                              &kInteractionDoc};

uint64_t ResolveGuid(void *L, const char *token) {
    if (_stricmp(token, "mouseover") == 0)
        return Game::Read<uint64_t>(Offsets::VAR_MOUSEOVER_GUID_LO);
    if (Unit::TokenResolve::IsUnitToken(token))
        return ::Unit::Identity::GuidForToken(token);
    return Unit::Identity::FindGuidByName(token, Offsets::TYPEMASK_OBJECT,
                                          Game::Lua::ToFlag(L, 2));
}

bool RightClick(uint64_t guid) {
    return reinterpret_cast<OnSpriteRightClick_t>(Offsets::FUN_ON_SPRITE_RIGHT_CLICK)(
               static_cast<uint32_t>(guid), static_cast<uint32_t>(guid >> 32)) != 0;
}

bool Interact(void *L, bool loose) {
    const char *token = Game::Lua::ToString(L, 1);
    if (_stricmp(token, "anyleftinteract") == 0)
        return false;
    const uint64_t guid = ResolveGuid(L, token);
    if (!loose)
        return RightClick(guid);
    if (guid == 0 || Object::ByGuid(Offsets::TYPEMASK_OBJECT, guid) == nullptr)
        return false;
    const bool soft = _strnicmp(token, "soft", 4) == 0 || _strnicmp(token, "any", 3) == 0;
    if (soft)
        Event::Custom::Fire(_interaction.Slot(), "");
    // The select stores only units (slot 0x58); for anything else it is a
    // no-op, as it is for a chosen target the loose reason may not replace.
    const Target::Reason::SelectFor why{soft ? Target::Reason::InteractKey
                                             : Target::Reason::Auto};
    return RightClick(guid);
}

int __fastcall Script_InteractUnit(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: InteractUnit(unitToken[, exactMatch])");
        return 0;
    }
    Game::Lua::PushFlag(L, Interact(L, false));
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
