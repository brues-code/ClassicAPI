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

// Per-character addon enable state: `C_AddOns.GetAddOnEnableState`,
// `EnableAddOn`, `DisableAddOn`, `EnableAllAddOns`, `DisableAllAddOns`,
// `SaveAddOns`, `ResetAddOns` and `Enum.AddOnEnableState`.
//
// 3.80.2 keeps one list of `name: enabled` records per character
// (ADDONSTATELIST), read from and written to that character's AddOns.txt.
// 1.12 keeps the same thing: one node per character on the realm, built by the
// character screen's roster, with a reader (FUN_ADDON_ENABLE_RESOLVE), a
// setter (FUN_ADDON_ENABLE_SET), a reload (FUN_ADDON_ENABLE_LOAD) and a writer
// (FUN_ADDON_ENABLE_SAVE) that the world shutdown also runs after the
// SavedVariables. Each function here is one call into them, with 3.80.2's
// arguments:
//   - `character` is a character name, matched without regard to case; nil,
//     "0" or "" means every character (Addons::CharacterArg). A name with no
//     node changes nothing and reads as the addon's DefaultState.
//   - The reader runs with its fallback on, as 3.80.2's FUN_141af5e60 always
//     does: a character with no record for the addon counts as the state over
//     all characters (Some counting as DefaultState). Some (1) therefore
//     comes back only for all characters.
//   - The setters write records for unknown names too, as 3.80.2's do. The
//     "All" setters walk the index list only, so Blizzard addons and the
//     embedded ones are never touched.

#include "Game.h"
#include "Offsets.h"
#include "addons/Registry.h"

#include <cstdint>

namespace AddOns::EnableState {

namespace {

using Get_t = uint32_t(__fastcall *)(const char *name, const char *character, int fallback);
using Set_t = void(__fastcall *)(const char *name, const char *character, int state);
using Load_t = void(__fastcall *)(const char *character);
using Save_t = void (*)();
using GetSecurityIndex_t = uint32_t(__fastcall *)(const char *name);
using GetByIndex_t = const char *(__fastcall *)(int idx0Based);
using GetAddOnCount_t = int(__fastcall *)();

constexpr int STATE_NONE = 0;
constexpr int STATE_ALL = 2;

void Set(const char *name, const char *character, int state) {
    reinterpret_cast<Set_t>(Offsets::FUN_ADDON_ENABLE_SET)(name, character, state);
}

// Usage strings, verbatim from 3.80.2.
constexpr const char *kGetUsage =
    "Usage: local state = C_AddOns.GetAddOnEnableState(name [, character])";
constexpr const char *kEnableUsage = "Usage: C_AddOns.EnableAddOn(name [, character])";
constexpr const char *kDisableUsage = "Usage: C_AddOns.DisableAddOn(name [, character])";
constexpr const char *kEnableAllUsage = "Usage: C_AddOns.EnableAllAddOns([character])";
constexpr const char *kDisableAllUsage = "Usage: C_AddOns.DisableAllAddOns([character])";

// Unknown name: 0.
int __fastcall Script_GetAddOnEnableState(void *L) {
    const uint8_t *record = Addons::ResolveAddOnArg(L, kGetUsage, nullptr);
    const char *character = Addons::CharacterArg(L, 2, kGetUsage);
    uint32_t state = STATE_NONE;
    if (record != nullptr)
        state = reinterpret_cast<Get_t>(Offsets::FUN_ADDON_ENABLE_RESOLVE)(
            Addons::RecordName(record), character, 1);
    Game::Lua::PushNumber(L, static_cast<double>(state));
    return 1;
}

int SetOne(void *L, const char *usage, int state) {
    const char *name = nullptr;
    const uint8_t *record = Addons::ResolveAddOnArg(L, usage, &name);
    const char *character = Addons::CharacterArg(L, 2, usage);
    if (record != nullptr)
        name = Addons::RecordName(record);
    Set(name, character, state);
    return 0;
}

int __fastcall Script_EnableAddOn(void *L) { return SetOne(L, kEnableUsage, STATE_ALL); }

// 3.80.2 refuses a secure (Blizzard) addon. Its message also carries the
// taint report, which this client has no counterpart for.
int __fastcall Script_DisableAddOn(void *L) {
    const uint8_t *record = Addons::ResolveAddOnArg(L, kDisableUsage, nullptr);
    if (record != nullptr &&
        reinterpret_cast<GetSecurityIndex_t>(Offsets::FUN_ADDON_GET_SECURITY_INDEX)(
            Addons::RecordName(record)) == 0) {
        Game::Lua::Error(L, "DisableAddOn(): Cannot disable a Secure AddOn");
        return 0;
    }
    return SetOne(L, kDisableUsage, STATE_NONE);
}

int SetAll(void *L, const char *usage, int state) {
    const char *character = Addons::CharacterArg(L, 1, usage);
    auto count = reinterpret_cast<GetAddOnCount_t>(Offsets::FUN_ADDON_GET_COUNT);
    auto byIndex = reinterpret_cast<GetByIndex_t>(Offsets::FUN_ADDON_GET_BY_INDEX);
    for (int i = 0; i < count(); ++i)
        if (const char *name = byIndex(i))
            Set(name, character, state);
    return 0;
}

int __fastcall Script_EnableAllAddOns(void *L) { return SetAll(L, kEnableAllUsage, STATE_ALL); }

int __fastcall Script_DisableAllAddOns(void *L) {
    return SetAll(L, kDisableAllUsage, STATE_NONE);
}

// Writes each character whose state changed to its AddOns.txt now; the world
// shutdown does the same at logout and reload.
int __fastcall Script_SaveAddOns(void *) {
    reinterpret_cast<Save_t>(Offsets::FUN_ADDON_ENABLE_SAVE)();
    return 0;
}

// Rereads every character's AddOns.txt, dropping the changes not saved.
int __fastcall Script_ResetAddOns(void *) {
    reinterpret_cast<Load_t>(Offsets::FUN_ADDON_ENABLE_LOAD)(nullptr);
    return 0;
}

constexpr Game::Lua::EnumIntegerEntry kEnableStateEntries[] = {
    {"None", 0},
    {"Some", 1},
    {"All", 2},
};

const Game::Doc::Field kNameCharacterArgs[] = {
    Game::Doc::Req("name", "uiAddon", "An addon index or folder name."),
    Game::Doc::Opt("character", "cstring", "0",
                   "A character name. \"0\" or nil: all characters."),
};
const Game::Doc::Field kCharacterArg[] = {
    Game::Doc::Opt("character", "cstring", nullptr,
                   "A character name. Nil, \"0\" or \"\": all characters."),
};

const Game::Doc::Field kGetRets[] = {
    Game::Doc::Req("state", "AddOnEnableState",
                   "Some only for all characters, when some have it enabled and some not. "
                   "None for an unknown addon."),
};
const Game::Doc::Function kGetAddOnEnableState{
    "Returns whether an addon is enabled for a character, or for all characters.",
    kNameCharacterArgs, kGetRets};
const Game::Doc::Function kEnableAddOn{
    "Enables an addon for a character, or for all characters. It takes effect at the next "
    "reload or login.",
    kNameCharacterArgs, {}};
const Game::Doc::Function kDisableAddOn{
    "Disables an addon for a character, or for all characters. It takes effect at the next "
    "reload or login.",
    kNameCharacterArgs, {}};
const Game::Doc::Function kEnableAllAddOns{
    "Enables every addon in the addon list for a character, or for all characters.",
    kCharacterArg, {}};
const Game::Doc::Function kDisableAllAddOns{
    "Disables every addon in the addon list for a character, or for all characters.",
    kCharacterArg, {}};
const Game::Doc::Function kSaveAddOns{
    "Writes the changed enable states to disk now. Logout and reload also write them.", {},
    {}};
const Game::Doc::Function kResetAddOns{
    "Discards the enable-state changes not yet saved, for all characters.", {}, {}};

struct Entry {
    const char *name;
    Game::Lua::CFunction fn;
    const Game::Doc::Function *doc;
};
const Entry kEntries[] = {
    {"GetAddOnEnableState", &Script_GetAddOnEnableState, &kGetAddOnEnableState},
    {"EnableAddOn", &Script_EnableAddOn, &kEnableAddOn},
    {"DisableAddOn", &Script_DisableAddOn, &kDisableAddOn},
    {"EnableAllAddOns", &Script_EnableAllAddOns, &kEnableAllAddOns},
    {"DisableAllAddOns", &Script_DisableAllAddOns, &kDisableAllAddOns},
    {"SaveAddOns", &Script_SaveAddOns, &kSaveAddOns},
    {"ResetAddOns", &Script_ResetAddOns, &kResetAddOns},
};

void RegisterLuaFunctions() {
    for (const Entry &e : kEntries)
        Game::Lua::RegisterTableFunction("C_AddOns", e.name, e.fn, e.doc);
    Game::Lua::RegisterIntegerEnum("Enum", "AddOnEnableState", kEnableStateEntries, "AddOns");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace AddOns::EnableState
