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

// "Is this a unit token?" over the engine's own token resolver. See
// TokenResolve.h for the contract.
//
// Why ask the resolver and not match a token list: the resolver
// (`FUN_TOKEN_TO_GUID`, 0x00515970) returns 0 for a token pointing at nothing
// and, for anything it does not recognise, raises "Unknown unit name: %s".
// The hook on it (`unit/TokenExtensions.cpp`) catches that raise and answers
// "no unit", as 4.3.4 and retail do, and records it for the string being
// asked about. Asking that way is strictly better than re-listing the token
// families here, which would silently drift from both the engine's set and
// the ones other DLLs and the hook add.
//
// Exposed to Lua as `IsUnitToken(value)`, which is what lets a caller pick
// between the engine's TOKEN entry and its BY-NAME entry for the same verb:
//
//   if IsUnitToken(t) then TargetUnit(t) else TargetByName(t) end
//
// A new global rather than a merged `TargetUnit` on purpose. Merging would
// mean re-registering `TargetUnit` / `AssistUnit` / `FollowUnit`, and those
// are common enough for other DLLs and addons to replace that whoever
// registers last silently wins — a conflict with no error and no way for
// either side to notice. One additive predicate composes with all of them
// and shadows nothing.

#include "unit/TokenExtensions.h"
#include "unit/TokenResolve.h"

#include "Game.h"

namespace Unit::TokenResolve {

bool IsUnitToken(const char *s) {
    return Unit::TokenExtensions::IsKnown(s);
}

namespace {

// `IsUnitToken(value)` -> boolean. False (never an error) for a missing
// argument or a non-string, so callers can hand it whatever
// `SecureCmdOptionParse` returned without pre-checking the type.
int __fastcall Script_IsUnitToken(void *L) {
    const char *arg = Game::Lua::IsString(L, 1) ? Game::Lua::ToString(L, 1) : nullptr;
    Game::Lua::PushBoolean(L, IsUnitToken(arg) ? 1 : 0);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Opt("value", "string", nullptr,
                   "Anything; a missing or non-string value returns false."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("isUnitToken", "bool",
                   "True for any token the client accepts, also when no unit is there, and "
                   "for the name of you, a group member or one of your pets."),
};
const Game::Doc::Function kIsUnitToken{
    "Returns whether a value is a unit token rather than a name or other text.", kArgs, kRets,
    "UnitGlobals", true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsUnitToken", &Script_IsUnitToken, &kIsUnitToken);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::TokenResolve
