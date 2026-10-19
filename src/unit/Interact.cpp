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
// 4.3.4's taint gate is dropped: 1.12 has no secure environment, and every
// vanilla action function (TargetUnit, CastSpellByName, …) is callable.

#include "Game.h"
#include "Offsets.h"
#include "unit/Identity.h"
#include "unit/TokenResolve.h"

#include <cstdint>
#include <cstring>

namespace Unit::Interact {

namespace {

using OnSpriteRightClick_t = int(__stdcall *)(uint32_t guidLo, uint32_t guidHi);
using TokenToGuid_t = uint64_t(__fastcall *)(const char *token);
using ToBool_t = int(__fastcall *)(void *L, int idx, int dflt);

uint64_t ResolveGuid(void *L, const char *token) {
    if (_stricmp(token, "mouseover") == 0)
        return Game::Read<uint64_t>(Offsets::VAR_MOUSEOVER_GUID_LO);
    if (Unit::TokenResolve::IsUnitToken(token))
        return reinterpret_cast<TokenToGuid_t>(Offsets::FUN_TOKEN_TO_GUID)(token);
    const int exact = reinterpret_cast<ToBool_t>(Offsets::FUN_LUA_TO_BOOLEAN_LOOSE)(L, 2, 0);
    return Unit::Identity::FindGuidByName(token, Offsets::TYPEMASK_OBJECT, exact != 0);
}

int __fastcall Script_InteractUnit(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: InteractUnit(unitToken[, exactMatch])");
        return 0;
    }

    const uint64_t guid = ResolveGuid(L, Game::Lua::ToString(L, 1));
    const int handled = reinterpret_cast<OnSpriteRightClick_t>(Offsets::FUN_ON_SPRITE_RIGHT_CLICK)(
        static_cast<uint32_t>(guid), static_cast<uint32_t>(guid >> 32));
    if (handled)
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
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

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("InteractUnit", &Script_InteractUnit, &kInteractUnit);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::Interact
