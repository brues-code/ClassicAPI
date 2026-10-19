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

// The protected-frame queries: `region:IsProtected()`,
// `region:CanChangeProtectedState()`, `frame:CanChangeAttribute()` and
// `frame:AllowAttributeChanges()`.
//
// Vanilla has no protected frames and no taint - the secure system arrived in
// 2.0 - and this client never gains one. Each method answers what 4.3.4's
// would for a frame that is not protected, in code that is not tainted:
//
//   - IsProtected: `nil, nil` (4.3.4 pushes 1 or nil for each).
//   - CanChangeProtectedState: 1, from `!IsProtected() || allowProtected`.
//   - CanChangeAttribute: 1, from `ProtectedFunctionsAllowed() || ...`, and
//     ProtectedFunctionsAllowed is `!IsProtected() || !lua_tainted || ...`.
//   - AllowAttributeChanges: nothing. 4.3.4 sets a per-frame grant that only
//     CanChangeAttribute and SetAttribute's protected check read, and both
//     already allow every change.
//
// Libraries ask these before they hook scripts or set attributes
// (AceHook-3.0's HookScript path asks IsProtected). IsProtected and
// CanChangeProtectedState are Region methods in 4.3.4 (CScriptRegion), the
// attribute pair Frame methods (CSimpleFrame).

#include "Game.h"
#include "Offsets.h"

namespace Frame::Protection {

namespace {

// The Region gate is the engine's own: its Region methods (Script GetLeft
// 0x007A1986) check `self` against VAR_REGION_LUA_TYPE_ID, as ResolveRegion
// does, and frames, textures and font strings all pass it.
int __fastcall Script_IsProtected(void *L) {
    if (Game::Lua::ResolveRegion(L) == nullptr)
        return 0;
    Game::Lua::PushNil(L);
    Game::Lua::PushNil(L);
    return 2;
}

int __fastcall Script_CanChangeProtectedState(void *L) {
    if (Game::Lua::ResolveRegion(L) == nullptr)
        return 0;
    Game::Lua::PushNumber(L, 1.0);
    return 1;
}

int __fastcall Script_CanChangeAttribute(void *L) {
    if (Game::Lua::ResolveFrame(L) == nullptr)
        return 0;
    Game::Lua::PushNumber(L, 1.0);
    return 1;
}

int __fastcall Script_AllowAttributeChanges(void *L) {
    Game::Lua::ResolveFrame(L);
    return 0;
}

const Game::Lua::FrameMethodEntry g_regionMethods[] = {
    {"IsProtected", &Script_IsProtected},
    {"CanChangeProtectedState", &Script_CanChangeProtectedState},
};

const Game::Lua::FrameMethodEntry g_frameMethods[] = {
    {"CanChangeAttribute", &Script_CanChangeAttribute},
    {"AllowAttributeChanges", &Script_AllowAttributeChanges},
};

const Game::Doc::Field kIsProtectedRets[] = {
    Game::Doc::Opt("isProtected", "number", nullptr, "Always nil: no frame is protected."),
    Game::Doc::Opt("isProtectedExplicitly", "number", nullptr, "Always nil."),
};
const Game::Doc::Field kAllowedRets[] = {
    Game::Doc::Req("allowed", "number", "Always 1."),
};
const Game::Doc::Function kIsProtected{"Returns whether the region is protected. No region is.", {},
                                       kIsProtectedRets};
const Game::Doc::Function kCanChangeProtectedState{
    "Returns whether code can change the region's protected state now. Always 1.", {}, kAllowedRets};
const Game::Doc::Function kCanChangeAttribute{"Returns whether code can change the frame's attributes now. Always 1.",
                                              {}, kAllowedRets};
const Game::Doc::Function kAllowAttributeChanges{
    "Lets code change the frame's attributes. Does nothing: every change is already allowed.", {}, {}};

const Game::Doc::Method g_regionDocs[] = {
    {"IsProtected", &kIsProtected},
    {"CanChangeProtectedState", &kCanChangeProtectedState},
};

const Game::Doc::Method g_frameDocs[] = {
    {"CanChangeAttribute", &kCanChangeAttribute},
    {"AllowAttributeChanges", &kAllowAttributeChanges},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_REGION_METHOD_REGISTRY), g_regionMethods,
        static_cast<int>(sizeof(g_regionMethods) / sizeof(g_regionMethods[0])), g_regionDocs,
        static_cast<int>(sizeof(g_regionDocs) / sizeof(g_regionDocs[0])));
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_frameMethods,
        static_cast<int>(sizeof(g_frameMethods) / sizeof(g_frameMethods[0])), g_frameDocs,
        static_cast<int>(sizeof(g_frameDocs) / sizeof(g_frameDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::Protection
