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

// Glue-side bindings for the saved-credentials API:
//
//   SaveAccount(name, password)        -> bool
//   DeleteAccount(name)                -> bool
//   GetSavedAccounts()                 -> { "ACCT1", "ACCT2", ... }
//   LoginWithSavedAccount(name)        -> bool   -- dispatched, plaintext
//                                                   never crosses C↔Lua
//                                                   boundary on the way out
//
// The first three are thin wrappers over `Security::CredStore`. The
// fourth is the load-bearing one: it pulls the encrypted blob, calls
// the engine's own `FUN_GLUE_LOGIN_ATTEMPT` (the function that
// `Script_DefaultServerLogin` tail-calls into), then scrubs the
// stack buffer. The engine itself zero-fills the password buffer
// after consuming it (see Offsets::FUN_GLUE_LOGIN_ATTEMPT docs), so
// the scrub here is belt-and-suspenders for any prefix bytes the
// engine's strlen-based wipe might miss.
//
// Registered on the glue Lua state only — there's no use for these
// outside the AccountLogin screen, and keeping them off the in-game
// state means no addon can call `LoginWithSavedAccount` to trigger
// an unexpected logout-and-login flow.

#include "CredStore.h"
#include "Game.h"
#include "Offsets.h"

#include <windows.h>

#include <cstring>
#include <vector>

namespace Security::Account {

namespace {

const char *ArgString(void *L, int idx) {
    if (!Game::Lua::IsString(L, idx))
        return nullptr;
    return Game::Lua::ToString(L, idx);
}

// Engine login entry. `__fastcall(ecx = account, edx = mutable
// password buffer)`. The engine zero-fills the password buffer after
// SRP consumes it.
using LoginAttempt_t = void(__fastcall *)(const char *account, char *passwordMutable);
// `reinterpret_cast` from integer to function-pointer isn't constant
// in C++17, so the cast lives in the call site instead of a constexpr
// const.
inline void LoginAttempt(const char *account, char *passwordMutable) {
    reinterpret_cast<LoginAttempt_t>(Offsets::FUN_GLUE_LOGIN_ATTEMPT)(
        account, passwordMutable);
}

int __fastcall Script_SaveAccount(void *L) {
    const char *name = ArgString(L, 1);
    const char *password = ArgString(L, 2);
    const bool ok =
        name && *name && password && *password &&
        Security::CredStore::Save(name, password);
    Game::Lua::PushBool(L, ok);
    return 1;
}

int __fastcall Script_DeleteAccount(void *L) {
    const char *name = ArgString(L, 1);
    const bool ok = name && *name && Security::CredStore::Delete(name);
    Game::Lua::PushBool(L, ok);
    return 1;
}

int __fastcall Script_GetSavedAccounts(void *L) {
    auto accounts = Security::CredStore::List();
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    int key = 1;
    for (const auto &acct : accounts) {
        // Push outer index → inner table { name=..., lastUsed=... }.
        Game::Lua::PushNumber(L, static_cast<double>(key++));
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldString(L, "name", acct.name.c_str());
        Game::Lua::SetFieldNumber(L, "lastUsed",
                                  static_cast<double>(acct.lastUsedUnix));
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

int __fastcall Script_LoginWithSavedAccount(void *L) {
    const char *name = ArgString(L, 1);
    if (!name || !*name) {
        Game::Lua::PushBool(L, false);
        return 1;
    }

    std::vector<char> password;
    if (!Security::CredStore::Load(name, password) || password.empty()) {
        Game::Lua::PushBool(L, false);
        return 1;
    }

    // Keep a copy of the plaintext on the stack — the engine's login
    // path zeros the buffer it consumes (see Offsets::FUN_GLUE_LOGIN_ATTEMPT
    // notes), but we need the plaintext for one more thing: re-saving
    // the credential so Windows updates LastWritten. Without that,
    // `lastUsed` in `GetSavedAccounts()` would only reflect the
    // original `SaveAccount` time, not actual logins.
    std::vector<char> copyForTouch(password.begin(), password.end());
    copyForTouch.push_back('\0');

    // CredStore::Load gives us a vector sized to the password length
    // with a trailing nul one past the end (resize to len after writing
    // len+1 bytes). The engine login function reads via strlen, so we
    // need the nul terminator accessible — `data()[size()]` is valid
    // on std::vector (the spec guarantees a writable element there
    // since C++11). Engine will zero out the bytes up through strlen
    // after SRP consumes them.
    LoginAttempt(name, password.data());

    // Refresh LastWritten by re-saving the credential. Done after
    // engine consumes so a no-op call (e.g. glue guards not satisfied)
    // doesn't update the timestamp on a login that didn't happen.
    // ... actually, the engine no-ops silently, so we can't gate on
    // success. We update unconditionally; in practice this only fires
    // from the AccountLogin screen, which has the guards satisfied.
    Security::CredStore::Save(name, copyForTouch.data());

    // Belt-and-suspenders scrub. Engine already zeroed `password.size()`
    // bytes, but we re-scrub the full vector capacity in case the
    // engine's wipe assumed an exact strlen match. Also scrub the
    // touch copy that lived through the engine call.
    SecureZeroMemory(password.data(), password.size());
    SecureZeroMemory(copyForTouch.data(), copyForTouch.size());

    Game::Lua::PushBool(L, true);
    return 1;
}

const Game::Doc::Field kSaveArgs[] = {
    Game::Doc::Req("name", "string", "The account name."),
    Game::Doc::Req("password", "string"),
};
const Game::Doc::Field kSaveRets[] = {
    Game::Doc::Req("success", "bool",
                   "False when either argument is empty, no realmlist is set, or Windows "
                   "refuses the write."),
};
const Game::Doc::Function kSaveAccount{
    "Saves an account's credentials, encrypted in the Windows Credential Manager, under the "
    "current realmlist.",
    kSaveArgs, kSaveRets, "AccountGlobals", true};

const Game::Doc::Field kNameArgs[] = {
    Game::Doc::Req("name", "string", "The account name."),
};
const Game::Doc::Field kDeleteRets[] = {
    Game::Doc::Req("deleted", "bool", "False when no such account is saved."),
};
const Game::Doc::Function kDeleteAccount{
    "Deletes the saved credentials of an account under the current realmlist.", kNameArgs,
    kDeleteRets, "AccountGlobals", true};

const Game::Doc::Field kSavedAccountFields[] = {
    Game::Doc::Req("name", "string", "The account name, as saved."),
    Game::Doc::Req("lastUsed", "number",
                   "When the account was last saved or logged in with, in seconds since "
                   "1970-01-01 UTC; 0 when unknown."),
};
const Game::Doc::Structure kSavedAccount{"SavedAccountInfo", "AccountGlobals",
                                         kSavedAccountFields};

const Game::Doc::Field kListRets[] = {
    Game::Doc::Req("accounts", "table",
                   "An array of SavedAccountInfo for the current realmlist; empty when no "
                   "realmlist is set."),
};
const Game::Doc::Function kGetSavedAccounts{
    "Returns the accounts saved under the current realmlist.", {}, kListRets, "AccountGlobals",
    true};

const Game::Doc::Field kLoginRets[] = {
    Game::Doc::Req("started", "bool", "False when no such account is saved."),
};
const Game::Doc::Function kLoginWithSavedAccount{
    "Logs in with an account's saved credentials without exposing the password to Lua.",
    kNameArgs, kLoginRets, "AccountGlobals", true};

void RegisterGlueFunctions() {
    Game::Lua::RegisterGlueFunction("SaveAccount", &Script_SaveAccount, &kSaveAccount);
    Game::Lua::RegisterGlueFunction("DeleteAccount", &Script_DeleteAccount, &kDeleteAccount);
    Game::Lua::RegisterGlueFunction("GetSavedAccounts", &Script_GetSavedAccounts,
                                    &kGetSavedAccounts);
    Game::Lua::RegisterGlueFunction("LoginWithSavedAccount",
                                    &Script_LoginWithSavedAccount, &kLoginWithSavedAccount);
}

const Game::GlueModuleAutoRegister _autoreg{&RegisterGlueFunctions};

} // namespace

} // namespace Security::Account
