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

// `C_AddOns.DoesAddOnHaveLoadError(name)`.
//
// 3.80.2 marks an addon (FUN_141af7390) when, during its load, a script error
// reaches the error handler (FrameScript_HandleError marks the top of the
// loading stack), a file fails to load, or a SavedVariables file fails. The
// mark stays until the addon registry is torn down at reload or logout. The
// lookup (FUN_141af7620) compares names case-sensitively.
//
// Here the loading stack is AddOns::LoadState's. Two places report:
//   - the error handler FUN_FRAMESCRIPT_CALL_ERROR_HANDLER, which every
//     script error in 1.12 goes through: a file's compile error (0x00704B42
//     calls it) and run-time error (the pcall's errfunc), and errors in the
//     OnLoad / OnEvent scripts that run during the load. SavedVariables files
//     run through the same file loader, so their errors arrive here too;
//   - Script::LoadFile's failure subscribers, for a file the TOC names that is
//     missing.
// A dependency loads inside the addon that needs it, so an error while the
// dependency loads marks the dependency, as 3.80.2's stack does.

#include "Game.h"
#include "Offsets.h"
#include "addons/LoadState.h"
#include "addons/Registry.h"
#include "script/LoadFile.h"

#include <cstring>
#include <string>
#include <vector>

namespace AddOns::LoadError {

namespace {

// Registered names of the addons with a load error, in the order marked.
std::vector<std::string> g_marked;

void MarkCurrent() {
    const char *loading = AddOns::LoadState::Current();
    if (loading == nullptr)
        return;
    // The loader may have been called with the name in any case (LoadAddOn
    // passes the caller's string); keep the registered spelling.
    const uint8_t *record = Addons::FindRecord(loading);
    const char *name = record != nullptr ? Addons::RecordName(record) : loading;
    for (const std::string &m : g_marked)
        if (m == name)
            return;
    g_marked.emplace_back(name);
}

using ErrorHandler_t = int(__fastcall *)(void *L);
ErrorHandler_t g_errorHandler = nullptr;

int __fastcall ErrorHandler_h(void *L) {
    MarkCurrent();
    return g_errorHandler(L);
}

const Game::HookAutoRegister _hookErrorHandler{
    Offsets::FUN_FRAMESCRIPT_CALL_ERROR_HANDLER, reinterpret_cast<void *>(&ErrorHandler_h),
    reinterpret_cast<void **>(&g_errorHandler)};

void OnFileFailed(const char *) { MarkCurrent(); }

const Script::LoadFile::AutoFailure _failure{&OnFileFailed};

constexpr const char *kUsage = "Usage: local hadError = C_AddOns.DoesAddOnHaveLoadError(name)";

// Unknown name: false.
int __fastcall Script_DoesAddOnHaveLoadError(void *L) {
    const char *name = nullptr;
    Addons::ResolveAddOnArg(L, kUsage, &name);
    bool hadError = false;
    for (const std::string &m : g_marked)
        if (name != nullptr && m == name)
            hadError = true;
    Game::Lua::PushBool(L, hadError);
    return 1;
}

void PrepareForReload() { g_marked.clear(); }

const Game::ReloadAutoRegister _reload{&PrepareForReload};

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("name", "uiAddon", "An addon index or folder name, in its exact case."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("hadError", "bool",
                   "True when a script error happened while the addon loaded, or one of its "
                   "files or SavedVariables failed to load. It stays true until the next "
                   "reload or login."),
};
const Game::Doc::Function kDoesAddOnHaveLoadError{
    "Returns whether an addon had an error while it loaded.", kArgs, kRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_AddOns", "DoesAddOnHaveLoadError",
                                     &Script_DoesAddOnHaveLoadError, &kDoesAddOnHaveLoadError);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace AddOns::LoadError
