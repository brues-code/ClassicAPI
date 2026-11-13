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

// `GetSavedCharacterOrder(realm)` / `SetSavedCharacterOrder(realm, order)`
// — glue-only globals for persisting the character-select reorder UI
// across sessions. Storage lives in the shared per-account settings
// file via `Settings::Account` (`WTF\Account\<account>\ClassicAPI.txt`),
// keyed by `CharacterOrder.<realm>=<opaque order>` lines.
//
// The `<order>` payload is opaque to this module — the Lua caller
// decides the encoding (the current GlueXML patch uses pipe-delimited
// character names).
//
// Account scoping is implicit via the file path (the engine writes
// `VAR_ACCOUNT_NAME_PTR` after auth response, so it's populated by the
// time the user lands on character-select). Realm scoping is explicit
// via the prefixed key: multiple realms under one account each get an
// independent line. Vanilla 1.12 glue has no general-purpose persistence
// API — only the autologin-saturated `GetSavedAccountName`/`Set` pair —
// so we layer onto our own settings file format.
//
// Registered on the **glue** Lua state only. In-world Lua sees a nil
// global; `/script GetSavedCharacterOrder("X")` from inside the world
// will error, by design — per TODO #94 the binding should not exist
// outside the screens that need it.

#include "Game.h"
#include "settings/Account.h"

#include <map>
#include <ostream>
#include <string>

namespace Charlist::Order {

namespace {

constexpr const char *kKeyPrefix = "CharacterOrder.";

// In-memory mirror of the on-disk file's `CharacterOrder.*` lines for
// the currently-loaded account. The `Settings::Account` registry owns
// the load/save cycle and the account-change detection; this module
// just exposes Reset/Serialize/Parse callbacks so the registry knows
// how to round-trip our slice of the file.
std::map<std::string, std::string> g_orders;

// Account-settings registry hooks ---------------------------------------

void SettingsReset() {
    g_orders.clear();
}

void SettingsSerialize(std::ostream &out) {
    for (const auto &kv : g_orders) {
        if (kv.second.empty())
            continue; // skip cleared entries
        out << kKeyPrefix << kv.first << '=' << kv.second << '\n';
    }
}

bool SettingsParse(const std::string &key, const std::string &value) {
    constexpr std::size_t prefixLen = 15; // strlen("CharacterOrder.")
    if (key.compare(0, prefixLen, kKeyPrefix) != 0)
        return false;
    std::string realm = key.substr(prefixLen);
    if (realm.empty())
        return true; // claim the key even if malformed — drop the value
    g_orders[std::move(realm)] = value;
    return true;
}

const Settings::Account::AutoRegister _accountSettings{
    &SettingsReset, &SettingsSerialize, &SettingsParse};

// Lua surface -----------------------------------------------------------

// CR/LF inside either arg would corrupt the line-oriented file format
// on the next save (a key with an embedded newline would parse as two
// half-lines on reload). Reject up-front rather than sanitize — the
// Lua caller can do its own escaping if it ever has multi-line names.
bool HasFileBreakChars(const char *s) {
    for (const char *p = s; *p != '\0'; ++p)
        if (*p == '\r' || *p == '\n')
            return true;
    return false;
}

// `GetSavedCharacterOrder(realm) -> string`. Always returns a string
// (never nil): empty for unknown realm, missing/non-string arg,
// pre-login (no account name), or unreadable file. Per TODO #94 the
// Lua side relies on `""` as the "first-run / cleared" sentinel.
int __fastcall Script_GetSavedCharacterOrder(void *L) {
    Settings::Account::EnsureLoaded();
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::PushString(L, "");
        return 1;
    }
    const char *realm = Game::Lua::ToString(L, 1);
    if (realm == nullptr || realm[0] == '\0') {
        Game::Lua::PushString(L, "");
        return 1;
    }
    const auto it = g_orders.find(realm);
    Game::Lua::PushString(L,
        it != g_orders.end() ? it->second.c_str() : "");
    return 1;
}

// `SetSavedCharacterOrder(realm, order)`. No return value. Empty
// `order` clears the realm's entry (removes the line on next save).
// Silent no-op when `realm` is missing/empty, when either arg
// contains a CR/LF, or when the account name isn't available yet.
int __fastcall Script_SetSavedCharacterOrder(void *L) {
    Settings::Account::EnsureLoaded();
    if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsString(L, 2))
        return 0;
    const char *realm = Game::Lua::ToString(L, 1);
    const char *order = Game::Lua::ToString(L, 2);
    if (realm == nullptr || realm[0] == '\0')
        return 0;
    if (order == nullptr)
        order = "";
    if (HasFileBreakChars(realm) || HasFileBreakChars(order))
        return 0;
    if (order[0] == '\0') {
        g_orders.erase(realm);
    } else {
        g_orders[realm] = order;
    }
    Settings::Account::Save();
    return 0;
}

const Game::Doc::Field kRealmArgs[] = {
    Game::Doc::Req("realm", "string"),
};
const Game::Doc::Field kGetSavedCharacterOrderRets[] = {
    Game::Doc::Req("order", "string", "Empty when no order is saved for the realm."),
};
const Game::Doc::Function kGetSavedCharacterOrder{
    "Returns the character order saved for a realm on this account.", kRealmArgs,
    kGetSavedCharacterOrderRets, "CharacterListGlobals", true};

const Game::Doc::Field kSetSavedCharacterOrderArgs[] = {
    Game::Doc::Req("realm", "string"),
    Game::Doc::Req("order", "string",
                   "Any text without line breaks, such as names joined by \"|\"; empty clears "
                   "the saved order."),
};
const Game::Doc::Function kSetSavedCharacterOrder{
    "Saves a character order for a realm on this account.", kSetSavedCharacterOrderArgs, {},
    "CharacterListGlobals", true};

void RegisterGlue() {
    Game::Lua::RegisterGlueFunction("GetSavedCharacterOrder",
                                    &Script_GetSavedCharacterOrder, &kGetSavedCharacterOrder);
    Game::Lua::RegisterGlueFunction("SetSavedCharacterOrder",
                                    &Script_SetSavedCharacterOrder, &kSetSavedCharacterOrder);
}

const Game::GlueModuleAutoRegister _autoreg{&RegisterGlue};

} // namespace

} // namespace Charlist::Order
