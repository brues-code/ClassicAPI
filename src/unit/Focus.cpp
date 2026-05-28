// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU Lesser General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

// `FocusUnit(unit)` / `ClearFocus()` / `focus` unit token. Polyfills
// the post-vanilla focus-target system on top of the engine's
// existing token resolver via the hook in
// `unit/TokenExtensions.cpp`.
//
// Storage: a single 64-bit GUID held in-process. The 3.3.5 engine
// keeps the same shape (two globals at `DAT_00BD07D0`/`D4`); we
// don't share those because vanilla never reads them. The resolver
// hook reads our `Unit::Focus::Get()` directly.
//
// Setter mirrors 3.3.5's `FUN_0051FF20` minus the post-vanilla
// `UnitFlag bit 0x2000` ("focus glow" rendering hint, introduced in
// TBC for the default focus frame). Vanilla addons (pfUI) render
// their own focus indicator and don't need the hint.

#include "Focus.h"

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"

#include <cstdint>

namespace Unit::Focus {

namespace {

constexpr const char *kEventName = "PLAYER_FOCUS_CHANGED";
const Event::Custom::AutoReserve _reserve{kEventName};

uint64_t g_focusGUID = 0;

using TokenToGUID_t = uint64_t(__fastcall *)(const char *token);

uint64_t ResolveTokenGUID(const char *token) {
    if (token == nullptr || *token == '\0')
        return 0;
    auto fn = reinterpret_cast<TokenToGUID_t>(
        static_cast<uintptr_t>(Offsets::FUN_TOKEN_TO_GUID));
    return fn(token);
}

// `FocusUnit(unit)` — sets focus to whatever GUID `unit` currently
// resolves to. Modern signature: `FocusUnit(unit)` with the token
// argument. Modern also allows `FocusUnit()` (no arg) to mean
// `"target"`, matching the `/focus` slash command's default.
int __fastcall Script_FocusUnit(void *L) {
    if (Game::Lua::IsString(L, 1)) {
        const char *token = Game::Lua::ToString(L, 1);
        Set(ResolveTokenGUID(token));
    } else {
        Set(ResolveTokenGUID("target"));
    }
    return 0;
}

// `ClearFocus()` — drops the focus, fires PLAYER_FOCUS_CHANGED.
int __fastcall Script_ClearFocus(void *L) {
    (void)L;
    Set(0);
    return 0;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("FocusUnit", &Script_FocusUnit);
    Game::Lua::RegisterGlobalFunction("ClearFocus", &Script_ClearFocus);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

uint64_t Get() { return g_focusGUID; }

void Set(uint64_t guid) {
    if (guid == g_focusGUID)
        return; // no-op: same target → no event fire (matches 3.3.5)
    g_focusGUID = guid;
    const int slot = Event::Custom::Lookup(kEventName);
    if (slot >= 0)
        Event::Custom::Fire(slot, "");
}

} // namespace Unit::Focus
