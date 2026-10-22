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

// `CVar::ScriptMemory` — turn the interface memory watchdog off by default.
//
// The engine polls Lua's live allocation against the `scriptMemory` cvar
// (KB; registered with default "49152", so 48 MB) in `FUN_00495590`:
//
//     limit = *(int *)(scriptMemoryCVar + 0x28);   // parsed-int cache
//     if (enabled && limit > 0) {
//         if (luaKB() > limit) ForceFullGC();
//         if (luaKB() > limit) {
//             if (!deadline) { deadline = now + 15000;
//                              Fire(MEMORY_EXHAUSTED, "%d", mb); }
//             else if (now >= deadline) Quit();      // FUN_00401560(0)
//         } else if (deadline) { deadline = 0; Fire(MEMORY_RECOVERED); }
//     }
//
// FrameXML turns MEMORY_EXHAUSTED into the "disable some interface add-ons
// and restart" popup, and 15 seconds later the client EXITS. So the cap is
// not advisory: it ends the session.
//
// 48 MB was a reasonable budget for the addons of the day and is not one for
// a modern set, so the popup and the shutdown land on people whose addons
// are working correctly. The gate is `limit > 0`, so writing 0 disables the
// poll outright — the value Blizzard's own AddOns dialog at character select
// lets a user type, via `SetScriptMemory` (which multiplies by 1024;
// verified `SHL EAX,0xa` at 0x0046DA8D).
//
// Nothing else caps Lua memory. `lmemPool.cpp`'s allocator (`FUN_006FAE90`)
// is six size classes that fall through to the Storm heap for anything
// larger, with no ceiling of its own; its "not enough memory" is a genuine
// allocation failure. So 0 hands the bound to the process address space
// (this client is Large Address Aware) rather than to a number picked in
// 2006 — the engine's real limit instead of an invented one.
//
// TIMING IS THE WHOLE TRICK. The cvar is READ-ONLY in the world: on entering
// it, `CGGameUI::InitializeGame` sets `CVAR_FLAG_READ_ONLY` on `realmList`,
// `realmName` and `scriptMemory` (`FUN_CVAR_SET_READ_ONLY` at 0x0048F579),
// and `ShutdownGame` clears it again on the way out (0x00491252). With the bit
// set, `SetCVar` raises `"scriptMemory" is read only` and the innermost setter
// (`FUN_0063E0B0`) silently drops any C-side write. InitializeGame sets the
// bit BEFORE it jumps to the interface build that runs our in-game
// registrations, and `/reload` never clears it, so this only works from the
// GLUE registration — which every boot and every logout passes through.
// Verified in game: delete the Config.wtf line, launch, and the AddOns dialog
// shows 0, which saves back to Config.wtf on exit.
//
// We override the DEFAULT ONLY: if the live value differs from the value the
// cvar was registered with, someone chose it and we leave it alone. The cvar
// carries the archive flag, so our 0 persists to Config.wtf and the
// comparison then fails on every later boot — the write happens once per
// install, not once per session. The one false positive is a user who puts
// exactly the default in Config.wtf, which reads as untouched; recovering the
// watchdog means choosing any other value.
//
// Running on glue also means the AddOns dialog shows 0 rather than 48 on a
// first launch.

#include "cvar/Factory.h"

#include "Game.h"

#include <cstring>

namespace CVar::ScriptMemory {

namespace {

constexpr const char *kCVarName = "scriptMemory";

void ApplyDefaultOverride() {
    Factory::Handle cvar = Factory::Find(kCVarName);
    if (cvar == nullptr)
        return; // registered at boot by the engine; absent only if that moved
    const char *value = Factory::GetString(cvar);
    const char *fallback = Factory::GetDefaultString(cvar);
    if (value == nullptr || fallback == nullptr)
        return;
    if (std::strcmp(value, fallback) != 0)
        return; // deliberately set — the user's choice stands
    Factory::SetInt(cvar, 0); // writable here only; see the timing note
}

const Game::GlueModuleAutoRegister _glueAutoreg{&ApplyDefaultOverride};

} // namespace

} // namespace CVar::ScriptMemory
