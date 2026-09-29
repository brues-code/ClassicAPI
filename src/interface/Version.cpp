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

// `INTERFACE_VERSION` global — the client's interface (TOC) version number,
// 11200 on this client. Read from the engine's own accessor (the same value
// the addon loader compares each `## Interface:` against) rather than a
// literal, and re-published on every `/reload` via the standard
// `ModuleAutoRegister` flow. It is a true fixed client constant, so it is a
// global number like `CLASSIC_API_VERSION`, not a function.
//
// The same number is appended to `GetBuildInfo()` as its 4th return,
// `tocversion`, which later clients return there (`version, build, date,
// tocversion`). Vanilla stops at 3, so the common port idiom
// `select(4, GetBuildInfo()) >= 30000` compares nil and throws at file load.
// Append-only: the first three values are the engine's own, and a caller
// that reads fewer returns sees no change. Both the in-game and the glue
// registrations are co-hooked so GlueXML sees the same shape.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Interface::Version {

namespace {

using ClientIfaceVer_t = uint32_t(__cdecl *)();
using ScriptFn_t = int(__fastcall *)(void *L);

uint32_t ClientInterfaceVersion() {
    return reinterpret_cast<ClientIfaceVer_t>(
        static_cast<uintptr_t>(Offsets::FUN_ADDON_CLIENT_INTERFACE_VERSION))();
}

void RegisterLuaFunctions() {
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    Game::Lua::SetGlobalNumber(L, "INTERFACE_VERSION",
                               static_cast<double>(ClientInterfaceVersion()));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// ---- GetBuildInfo() tocversion ---------------------------------------------

ScriptFn_t g_origGetBuildInfo = nullptr;
ScriptFn_t g_origGlueGetBuildInfo = nullptr;

// Vanilla pushes exactly (version, build, date). Only that shape gets the
// 4th value, so a client or DLL that already returns more is left alone.
int AppendTocVersion(void *L, int n) {
    if (n != 3)
        return n;
    Game::Lua::PushNumber(L, static_cast<double>(ClientInterfaceVersion()));
    return 4;
}

int __fastcall GetBuildInfo_h(void *L) {
    return AppendTocVersion(L, g_origGetBuildInfo(L));
}

int __fastcall GlueGetBuildInfo_h(void *L) {
    return AppendTocVersion(L, g_origGlueGetBuildInfo(L));
}

const Game::HookAutoRegister _getBuildInfoHook{
    Offsets::FUN_SCRIPT_GET_BUILD_INFO,
    reinterpret_cast<void *>(&GetBuildInfo_h),
    reinterpret_cast<void **>(&g_origGetBuildInfo)};

const Game::HookAutoRegister _glueGetBuildInfoHook{
    Offsets::FUN_SCRIPT_GLUE_GET_BUILD_INFO,
    reinterpret_cast<void *>(&GlueGetBuildInfo_h),
    reinterpret_cast<void **>(&g_origGlueGetBuildInfo)};

} // namespace

} // namespace Interface::Version
