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

// Protected "is this a unit token?" probe over the engine's own token
// resolver. See TokenResolve.h for the contract.
//
// Why a protected call and not a token-list match: the resolver
// (`FUN_TOKEN_TO_GUID`, 0x00515970) is a chain of literal / prefix compares
// that returns 0 for a token pointing at nothing and, for anything it does
// not recognise, falls through to the engine's Lua error helper
// (`FUN_007040E0` → `luaG_runerror`) with "Unknown unit name: %s". That
// error is a real throw, which is why FrameXML-side code has to wrap
// `UnitExists` in `pcall` (Util/MacroOptions.lua does exactly that). So the
// only way to consult the resolver about an unknown string is to catch the
// throw — and catching it is strictly better than re-listing the token
// families here, which would silently drift from both the engine's set and
// the ones `unit/TokenExtensions.cpp` hooks on.
//
// The resolver is a pure read (object-manager lookups, string compares,
// party/raid slot arrays), so probing and then letting the engine resolve
// the same string again costs nothing but the second walk.
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

#include "unit/TokenResolve.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Unit::TokenResolve {

namespace {

using TokenToGuid_t = uint64_t(__fastcall *)(const char *token);

// Body of the protected call: resolve arg 1 and discard the result. Returns
// no values — the caller only reads the pcall status, since a token that
// resolves to 0 (empty slot) is still a token.
int __fastcall ProbeThunk(void *L) {
    const char *token = Game::Lua::ToString(L, 1);
    if (token)
        reinterpret_cast<TokenToGuid_t>(Offsets::FUN_TOKEN_TO_GUID)(token);
    return 0;
}

} // namespace

bool IsUnitToken(const char *s) {
    if (!s || !*s)
        return false;

    void *L = Game::Lua::State();
    if (!L)
        return false;

    const int base = Game::Lua::GetTop(L);
    Game::Lua::PushCClosure(L, &ProbeThunk, 0);
    Game::Lua::PushString(L, s);
    const int status = Game::Lua::PCall(L, /*nargs*/ 1, /*nresults*/ 0, /*errfunc*/ 0);
    // On failure pcall leaves the error object on the stack; restore either
    // way so the caller sees the stack it handed us.
    Game::Lua::SetTop(L, base);
    return status == 0;
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

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsUnitToken", &Script_IsUnitToken);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::TokenResolve
