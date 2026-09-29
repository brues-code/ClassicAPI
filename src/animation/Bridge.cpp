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

// The animation system's native side.
//
// Later clients give every region CreateAnimationGroup / GetAnimationGroups /
// StopAnimating. The animation runtime itself is Lua, in the bundled addon
// (AddOns/!!!ClassicAPI/Util/Animation.lua), which hands its entry table to
// `_classicapi_SetAnimationRuntime` while it loads. This module keeps that
// table in a registry ref and registers the three methods on the region base
// registry - so frames, textures and font strings all reach them - as C
// functions that forward `(self, ...)` to the runtime. XML <Animations> blocks
// reach the runtime through the node loaders (xml/ParentKey.cpp), via
// PushRuntimeFunction.

#include "animation/Bridge.h"

#include "Game.h"
#include "Offsets.h"

namespace Animation::Bridge {

namespace {

using LuaRef_t = int(__fastcall *)(void *L, int t);
using LuaUnref_t = void(__fastcall *)(void *L, int t, int ref);
using RawGetI_t = void(__fastcall *)(void *L, int idx, int n);

int g_runtimeRef = 0; // registry ref of the runtime table; <= 0 when unset

// `_classicapi_SetAnimationRuntime(runtime)` - called once by the bundled
// addon's Util/Animation.lua. A later call replaces the table.
int __fastcall Script_SetAnimationRuntime(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: _classicapi_SetAnimationRuntime(runtime)");
        return 0;
    }
    if (g_runtimeRef > 0) {
        reinterpret_cast<LuaUnref_t>(static_cast<uintptr_t>(Offsets::LUA_REF_UNREF))(
            L, Game::Lua::REGISTRY_INDEX, g_runtimeRef);
        g_runtimeRef = 0;
    }
    Game::Lua::SetTop(L, 1);
    g_runtimeRef = reinterpret_cast<LuaRef_t>(static_cast<uintptr_t>(Offsets::LUA_REF_REF))(
        L, Game::Lua::REGISTRY_INDEX);
    return 0;
}

// Calls runtime[fn](self, ...) with the method's own arguments and returns
// whatever it returns.
int Forward(void *L, const char *fn, const char *usage) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "%s", usage);
        return 0;
    }
    const int nargs = Game::Lua::GetTop(L);
    if (!PushRuntimeFunction(L, fn)) {
        Game::Lua::Error(L, "%s", "animations are unavailable: the bundled animation runtime is not loaded");
        return 0;
    }
    Game::Lua::Insert(L, 1); // [fn, self, ...]
    Game::Lua::Call(L, nargs, Game::Lua::MULTRET);
    return Game::Lua::GetTop(L);
}

int __fastcall Script_CreateAnimationGroup(void *L) {
    return Forward(L, "CreateGroup", "Usage: region:CreateAnimationGroup([name [, template]])");
}
int __fastcall Script_GetAnimationGroups(void *L) {
    return Forward(L, "GetGroups", "Usage: region:GetAnimationGroups()");
}
int __fastcall Script_StopAnimating(void *L) {
    return Forward(L, "StopAll", "Usage: region:StopAnimating()");
}

const Game::Lua::FrameMethodEntry g_regionMethods[] = {
    {"CreateAnimationGroup", &Script_CreateAnimationGroup},
    {"GetAnimationGroups", &Script_GetAnimationGroups},
    {"StopAnimating", &Script_StopAnimating},
};

void RegisterLuaFunctions() {
    g_runtimeRef = 0; // a fresh Lua state: the bundled addon registers anew
    Game::Lua::RegisterGlobalFunction("_classicapi_SetAnimationRuntime",
                                      &Script_SetAnimationRuntime);
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_REGION_METHOD_REGISTRY), g_regionMethods,
        static_cast<int>(sizeof(g_regionMethods) / sizeof(g_regionMethods[0])));
}

void PrepareForReload() { g_runtimeRef = 0; }

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::ReloadAutoRegister _reload{&PrepareForReload};

} // namespace

bool PushRuntimeFunction(void *L, const char *name) {
    if (L == nullptr || g_runtimeRef <= 0)
        return false;
    reinterpret_cast<RawGetI_t>(Offsets::FUN_FRAMESCRIPT_PUSH_OBJECT)(
        L, Game::Lua::REGISTRY_INDEX, g_runtimeRef); // [runtime]
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::SetTop(L, -2);
        return false;
    }
    Game::Lua::PushString(L, name);
    Game::Lua::GetTable(L, -2); // [runtime, fn]
    Game::Lua::Remove(L, -2);   // [fn]
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_FUNCTION) {
        Game::Lua::SetTop(L, -2);
        return false;
    }
    return true;
}

} // namespace Animation::Bridge
