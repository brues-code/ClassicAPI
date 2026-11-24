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

// `C_AddOns` getters and the load check, as the 3.80.2 client has them.
//
// Every function reads its addon argument as 3.80.2's FUN_140b281f0 does
// (`Addons::ResolveAddOnArg`): an index out of range, or a value that is
// neither a number nor a string, is an error with the function's usage
// string; an unknown name is no error. What a function answers for an
// unknown name is 3.80.2's value for that function (noted at each one).
//
// The fields come from 1.12's own record and accessors: the title and notes
// lookups, the security index, the LoadOnDemand / DefaultState bytes, and the
// load check FUN_0051E780, which takes the character whose enable state it
// reads and a demand flag.

#include "Game.h"
#include "Offsets.h"
#include "addons/LoadState.h"
#include "addons/Registry.h"
#include "cvar/Factory.h"

#include <cstdint>
#include <cstdio>

namespace AddOns::Info {

namespace {

using FieldByName_t = const char *(__fastcall *)(const char *name);
using EntryByteFn_t = uint8_t(__fastcall *)(const char *name);
// FUN_ADDON_CAN_LOAD: `demand` 1 adds NOT_DEMAND_LOADED for an addon without
// `## LoadOnDemand`; `character` NULL reads the enable state over every
// character.
using CanLoadFn_t = uint8_t(__fastcall *)(const char *name, char demand, int *outStatus,
                                          int *outDep, const char *character);
using FormatReason_t = char *(__fastcall *)(int code, int depCode, char *buf, unsigned cap);

uint8_t RecordByte(const uint8_t *record, uintptr_t offset) { return record[offset]; }

// 3.80.2's TOC parser seeds Title with the folder name, so a known addon
// without `## Title:` answers its name.
const char *TitleOf(const uint8_t *record) {
    const char *name = Addons::RecordName(record);
    const char *title =
        reinterpret_cast<FieldByName_t>(Offsets::FUN_ADDON_GET_TITLE_BY_NAME)(name);
    return (title != nullptr && *title != '\0') ? title : name;
}

const char *NotesOf(const uint8_t *record) {
    return reinterpret_cast<FieldByName_t>(Offsets::FUN_ADDON_GET_NOTES_BY_NAME)(
        Addons::RecordName(record));
}

int SecurityOf(const uint8_t *record) { return Addons::SecurityIndex(Addons::RecordName(record)); }

// 3.80.2's check FUN_141af6140 as GetAddOnInfo and IsAddOnLoadable call it:
// MISSING; then 1.12's check, called with no demand flag: DISABLED for the
// character's state 0, BANNED, INSECURE, INTERFACE_VERSION and a
// dependency's failure as `DEP_<reason>`; last, DEMAND_LOADED for a
// LoadOnDemand addon unless `demandLoaded`. It never asks whether the addon
// is already loaded. `reason` is "" when the addon can load.
bool CheckLoadable(const uint8_t *record, const char *character, bool demandLoaded,
                   char *reason, unsigned cap) {
    reason[0] = '\0';
    if (record == nullptr) {
        std::snprintf(reason, cap, "MISSING");
        return false;
    }
    int status = 0;
    int dep = 0;
    const uint8_t ok = reinterpret_cast<CanLoadFn_t>(Offsets::FUN_ADDON_CAN_LOAD)(
        Addons::RecordName(record), 0, &status, &dep, character);
    if (ok == 0) {
        if (status != 0 || dep != 0)
            reinterpret_cast<FormatReason_t>(Offsets::FUN_ADDON_FORMAT_REASON)(status, dep,
                                                                              reason, cap);
        return false;
    }
    if (!demandLoaded && RecordByte(record, Offsets::OFF_ADDON_ENTRY_LOAD_ON_DEMAND) != 0) {
        std::snprintf(reason, cap, "DEMAND_LOADED");
        return false;
    }
    return true;
}

int PushNames(void *L, const uint8_t *record, uintptr_t countOffset, uintptr_t arrayOffset) {
    if (record == nullptr)
        return 0;
    const uint32_t count = *reinterpret_cast<const uint32_t *>(record + countOffset);
    auto names = *reinterpret_cast<const char *const *const *>(record + arrayOffset);
    if (count == 0 || names == nullptr)
        return 0;
    Game::Lua::CheckStack(L, static_cast<int>(count));
    for (uint32_t i = 0; i < count; ++i)
        Game::Lua::PushString(L, names[i]);
    return static_cast<int>(count);
}

// Usage strings, verbatim from 3.80.2.
constexpr const char *kNameUsage = "Usage: local name = C_AddOns.GetAddOnName(index)";
constexpr const char *kTitleUsage = "Usage: local title = C_AddOns.GetAddOnTitle(name)";
constexpr const char *kNotesUsage = "Usage: local notes = C_AddOns.GetAddOnNotes(name)";
constexpr const char *kSecurityUsage =
    "Usage: local security = C_AddOns.GetAddOnSecurity(name)";
constexpr const char *kExistsUsage = "Usage: local exists = C_AddOns.DoesAddOnExist(name)";
constexpr const char *kLoadedUsage =
    "Usage: local loadedOrLoading, loaded = C_AddOns.IsAddOnLoaded(name)";
constexpr const char *kLoadUsage = "Usage: local loaded, value = C_AddOns.LoadAddOn(name)";
constexpr const char *kOptionalDepsUsage =
    "Usage: local (unpackedPrimitiveType)* = C_AddOns.GetAddOnOptionalDependencies(name)";
constexpr const char *kDepsUsage =
    "Usage: local (unpackedPrimitiveType)* = C_AddOns.GetAddOnDependencies(name)";
constexpr const char *kInterfaceUsage =
    "Usage: local interfaceVersion = C_AddOns.GetAddOnInterfaceVersion(name)";
constexpr const char *kLoadableUsage =
    "Usage: local loadable, reason = C_AddOns.IsAddOnLoadable(name [, character, "
    "demandLoaded])";
constexpr const char *kInfoUsage = "Usage: local name, title, notes, loadable, reason, "
                                   "security = C_AddOns.GetAddOnInfo(name)";
constexpr const char *kLoadOnDemandUsage =
    "Usage: local loadOnDemand = C_AddOns.IsAddOnLoadOnDemand(name)";
constexpr const char *kDefaultEnabledUsage =
    "Usage: local defaultEnabled = C_AddOns.IsAddOnDefaultEnabled(name)";
constexpr const char *kVersionCheckUsage = "Usage: C_AddOns.SetAddonVersionCheck(enabled)";

const uint8_t *Arg(void *L, const char *usage, const char **name = nullptr) {
    return Addons::ResolveAddOnArg(L, usage, name);
}

// Unknown name: nil. Otherwise the registered name, whatever case was asked.
int __fastcall Script_GetAddOnName(void *L) {
    const uint8_t *record = Arg(L, kNameUsage);
    Game::Lua::PushString(L, record != nullptr ? Addons::RecordName(record) : nullptr);
    return 1;
}

// Unknown name: nil.
int __fastcall Script_GetAddOnTitle(void *L) {
    const uint8_t *record = Arg(L, kTitleUsage);
    Game::Lua::PushString(L, record != nullptr ? TitleOf(record) : nullptr);
    return 1;
}

// Unknown name, or no `## Notes:`: nil.
int __fastcall Script_GetAddOnNotes(void *L) {
    const uint8_t *record = Arg(L, kNotesUsage);
    Game::Lua::PushString(L, record != nullptr ? NotesOf(record) : nullptr);
    return 1;
}

// Unknown name: 1, Insecure.
int __fastcall Script_GetAddOnSecurity(void *L) {
    const uint8_t *record = Arg(L, kSecurityUsage);
    Game::Lua::PushNumber(L, static_cast<double>(record != nullptr ? SecurityOf(record) : 1));
    return 1;
}

// Every registered addon exists, Blizzard's included.
int __fastcall Script_DoesAddOnExist(void *L) {
    Game::Lua::PushBool(L, Arg(L, kExistsUsage) != nullptr);
    return 1;
}

// The engine's loaded byte is set when a load starts; `loaded` waits for the
// load call to return (AddOns::LoadState). Unknown name: false, false.
int __fastcall Script_IsAddOnLoaded(void *L) {
    const uint8_t *record = Arg(L, kLoadedUsage);
    bool loadedOrLoading = false;
    bool loaded = false;
    if (record != nullptr) {
        const char *name = Addons::RecordName(record);
        loadedOrLoading =
            reinterpret_cast<EntryByteFn_t>(Offsets::FUN_ADDON_IS_LOADED)(name) != 0;
        loaded = loadedOrLoading && !AddOns::LoadState::IsLoading(name);
    }
    Game::Lua::PushBool(L, loadedOrLoading);
    Game::Lua::PushBool(L, loaded);
    return 2;
}

// 1.12's LoadAddOn (Script_LoadAddOn) does the load and names the failure;
// this gives its `1` / nil as true / false. Unknown name: false, "MISSING".
int __fastcall Script_LoadAddOn(void *L) {
    if (Arg(L, kLoadUsage) == nullptr) {
        Game::Lua::PushBool(L, false);
        Game::Lua::PushString(L, "MISSING");
        return 2;
    }
    Game::Lua::SetTop(L, 1);
    const int count = reinterpret_cast<Game::Lua::CFunction>(
        static_cast<uintptr_t>(Offsets::FUN_SCRIPT_LOAD_ADDON))(L);
    Game::Lua::PushBool(L, count >= 1 && Game::Lua::ToBoolean(L, 2) != 0);
    if (count >= 2)
        Game::Lua::PushValue(L, 3);
    else
        Game::Lua::PushNil(L);
    return 2;
}

// Unknown name: nothing.
int __fastcall Script_GetAddOnOptionalDependencies(void *L) {
    return PushNames(L, Arg(L, kOptionalDepsUsage), Offsets::OFF_ADDON_OPTIONALDEPS_COUNT,
                     Offsets::OFF_ADDON_OPTIONALDEPS_ARRAY);
}

// `## Dependencies:` / `## RequiredDeps:`. Unknown name: nothing.
int __fastcall Script_GetAddOnDependencies(void *L) {
    return PushNames(L, Arg(L, kDepsUsage), Offsets::OFF_ADDON_REQDEPS_COUNT,
                     Offsets::OFF_ADDON_REQDEPS_ARRAY);
}

// The `## Interface:` value the parser stored, already reduced to 3.80.2's
// pick by AddOns::TocRewrite. Unknown name: 0.
int __fastcall Script_GetAddOnInterfaceVersion(void *L) {
    const uint8_t *record = Arg(L, kInterfaceUsage);
    const uint32_t version =
        record != nullptr
            ? *reinterpret_cast<const uint32_t *>(record + Offsets::OFF_ADDON_INTERFACE_VERSION)
            : 0;
    Game::Lua::PushNumber(L, static_cast<double>(version));
    return 1;
}

// `IsAddOnLoadable(name [, character, demandLoaded])`: the check for that
// character (every character by default) and demand mode (false by default).
int __fastcall Script_IsAddOnLoadable(void *L) {
    const uint8_t *record = Arg(L, kLoadableUsage);
    const char *character = Addons::CharacterArg(L, 2, kLoadableUsage);
    const bool demandLoaded = Game::Lua::ToBoolean(L, 3) != 0;
    char reason[128];
    Game::Lua::PushBool(L, CheckLoadable(record, character, demandLoaded, reason,
                                         sizeof(reason)));
    Game::Lua::PushString(L, reason);
    return 2;
}

// Six values. Unknown name: the argument, nil, nil, false, "MISSING",
// "INSECURE".
int __fastcall Script_GetAddOnInfo(void *L) {
    const char *name = nullptr;
    const uint8_t *record = Arg(L, kInfoUsage, &name);
    Game::Lua::PushString(L, name);
    Game::Lua::PushString(L, record != nullptr ? TitleOf(record) : nullptr);
    Game::Lua::PushString(L, record != nullptr ? NotesOf(record) : nullptr);
    char reason[128];
    Game::Lua::PushBool(L, CheckLoadable(record, nullptr, false, reason, sizeof(reason)));
    Game::Lua::PushString(L, reason);
    const int security = record != nullptr ? SecurityOf(record) : 1;
    auto *names = reinterpret_cast<const char *const *>(
        static_cast<uintptr_t>(Offsets::VAR_ADDON_SECURITY_TABLE));
    Game::Lua::PushString(L, (security >= 0 && security <= 2) ? names[security] : "INSECURE");
    return 6;
}

// Unknown name: false.
int __fastcall Script_IsAddOnLoadOnDemand(void *L) {
    const uint8_t *record = Arg(L, kLoadOnDemandUsage);
    Game::Lua::PushBool(L, record != nullptr &&
                               RecordByte(record, Offsets::OFF_ADDON_ENTRY_LOAD_ON_DEMAND) != 0);
    return 1;
}

// `## DefaultState:`. Unknown name: false.
int __fastcall Script_IsAddOnDefaultEnabled(void *L) {
    const uint8_t *record = Arg(L, kDefaultEnabledUsage);
    Game::Lua::PushBool(L, record != nullptr &&
                               RecordByte(record, Offsets::OFF_ADDON_ENTRY_DEFAULT_STATE) != 0);
    return 1;
}

// The cvar 1.12's load check reads before it refuses an out-of-date addon
// (0x0051E876). 3.80.2 also turns the check back on when the interface
// version changes (its `lastAddonVersion` cvar); the 1.12 version never
// changes.
CVar::Factory::Handle VersionCheckCVar() { return CVar::Factory::Find("checkAddonVersion"); }

int __fastcall Script_IsAddonVersionCheckEnabled(void *L) {
    Game::Lua::PushBool(L, CVar::Factory::GetInt(VersionCheckCVar(), 1) != 0);
    return 1;
}

int __fastcall Script_SetAddonVersionCheck(void *L) {
    const int type = Game::Lua::Type(L, 1);
    if (type == Game::Lua::TYPE_NONE || type == Game::Lua::TYPE_NIL) {
        Game::Lua::ArgError(L, 1, kVersionCheckUsage);
        return 0;
    }
    CVar::Factory::SetString(VersionCheckCVar(), Game::Lua::ToBoolean(L, 1) != 0 ? "1" : "0");
    return 0;
}

constexpr Game::Lua::EnumIntegerEntry kAddOnSecurityStatusEntries[] = {
    {"Secure", 0},
    {"Insecure", 1},
    {"Banned", 2},
    {"NotAvailable", 3},
};

const Game::Doc::Field kNameArg[] = {
    Game::Doc::Req("name", "uiAddon", "An addon index or folder name."),
};

const Game::Doc::Field kGetNameRets[] = {
    Game::Doc::Opt("name", "cstring", nullptr,
                   "The folder name as the addon registered it. Nil for an unknown addon."),
};
const Game::Doc::Function kGetAddOnName{"Returns the folder name of an addon.", kNameArg,
                                        kGetNameRets};

const Game::Doc::Field kGetTitleRets[] = {
    Game::Doc::Opt("title", "cstring", nullptr,
                   "The folder name when the TOC has no title. Nil for an unknown addon."),
};
const Game::Doc::Function kGetAddOnTitle{"Returns the ## Title: of an addon's TOC file.",
                                         kNameArg, kGetTitleRets};

const Game::Doc::Field kGetNotesRets[] = {
    Game::Doc::Opt("notes", "cstring", nullptr,
                   "Nil for an unknown addon or one without notes."),
};
const Game::Doc::Function kGetAddOnNotes{"Returns the ## Notes: of an addon's TOC file.",
                                         kNameArg, kGetNotesRets};

const Game::Doc::Field kLoadableArgs[] = {
    Game::Doc::Req("name", "uiAddon", "An addon index or folder name."),
    Game::Doc::Opt("character", "cstring", "0",
                   "The character whose enable state counts. \"0\" or nil: all characters."),
    Game::Doc::Opt("demandLoaded", "bool", "false",
                   "True to ask about a load on demand. False refuses a load-on-demand addon "
                   "with DEMAND_LOADED, also one that is loaded."),
};
const Game::Doc::Field kLoadableRets[] = {
    Game::Doc::Req("loadable", "bool"),
    Game::Doc::Req("reason", "cstring",
                   "Why it cannot load, such as \"DISABLED\", \"DEMAND_LOADED\" or "
                   "\"DEP_DISABLED\". An empty string when it can load."),
};
const Game::Doc::Function kIsAddOnLoadable{"Returns whether an addon can be loaded, and why "
                                           "not when it cannot.",
                                           kLoadableArgs, kLoadableRets};

const Game::Doc::Field kSecurityRets[] = {
    Game::Doc::Req("security", "AddOnSecurityStatus", "Insecure for an unknown addon."),
};
const Game::Doc::Function kGetAddOnSecurity{"Returns the security status of an addon.",
                                            kNameArg, kSecurityRets};

const Game::Doc::Field kExistsRets[] = {
    Game::Doc::Req("exists", "bool"),
};
const Game::Doc::Function kDoesAddOnExist{"Returns whether an addon is installed.", kNameArg,
                                          kExistsRets};

const Game::Doc::Field kLoadedRets[] = {
    Game::Doc::Req("loadedOrLoading", "bool"),
    Game::Doc::Req("loaded", "bool", "False while the addon's own files are still running."),
};
const Game::Doc::Function kIsAddOnLoaded{"Returns whether an addon has started loading and "
                                         "whether it has finished.",
                                         kNameArg, kLoadedRets};

const Game::Doc::Field kLoadRets[] = {
    Game::Doc::Opt("loaded", "bool", nullptr, "True when the addon loaded or was loaded."),
    Game::Doc::Opt("value", "string", nullptr,
                   "Why the load failed, such as \"DISABLED\" or \"MISSING\"; nil on success."),
};
const Game::Doc::Function kLoadAddOn{"Loads a load-on-demand addon.", kNameArg, kLoadRets};

const Game::Doc::Field kOptionalDepsRets[] = {
    Game::Doc::Vararg("cstring", "The ## OptionalDeps: names, in order."),
};
const Game::Doc::Function kGetAddOnOptionalDependencies{
    "Returns the optional dependencies of an addon.", kNameArg, kOptionalDepsRets};

const Game::Doc::Field kDepsRets[] = {
    Game::Doc::Vararg("cstring", "The ## Dependencies: names, in order."),
};
const Game::Doc::Function kGetAddOnDependencies{"Returns the required dependencies of an addon.",
                                                kNameArg, kDepsRets};

const Game::Doc::Field kInterfaceVersionRets[] = {
    Game::Doc::Req("interfaceVersion", "number",
                   "The ## Interface: value from the TOC; 0 for an unknown addon."),
};
const Game::Doc::Function kGetAddOnInterfaceVersion{
    "Returns the Interface directive of an addon TOC: the client interface version when "
    "the list holds it, else the largest value below it, else 0.",
    kNameArg, kInterfaceVersionRets};

const Game::Doc::Field kInfoRets[] = {
    Game::Doc::Req("name", "cstring", "For an unknown addon, the name as given."),
    Game::Doc::Opt("title", "cstring", nullptr, "Nil for an unknown addon."),
    Game::Doc::Opt("notes", "cstring", nullptr, "Nil for an unknown addon or no notes."),
    Game::Doc::Req("loadable", "bool",
                   "As IsAddOnLoadable with all characters and demandLoaded false."),
    Game::Doc::Req("reason", "cstring", "An empty string when loadable."),
    Game::Doc::Req("security", "cstring",
                   "SECURE, INSECURE or BANNED; INSECURE for an unknown addon."),
};
const Game::Doc::Function kGetAddOnInfo{"Returns the name, title, notes, load state and "
                                        "security of an addon.",
                                        kNameArg, kInfoRets};

const Game::Doc::Field kLoadOnDemandRets[] = {
    Game::Doc::Req("loadOnDemand", "bool"),
};
const Game::Doc::Function kIsAddOnLoadOnDemand{"Returns whether an addon has ## LoadOnDemand:.",
                                               kNameArg, kLoadOnDemandRets};

const Game::Doc::Field kDefaultEnabledRets[] = {
    Game::Doc::Req("defaultEnabled", "bool",
                   "False when the TOC has ## DefaultState: disabled, or for an unknown addon."),
};
const Game::Doc::Function kIsAddOnDefaultEnabled{
    "Returns whether an addon is enabled for a character that has no setting for it.",
    kNameArg, kDefaultEnabledRets};

const Game::Doc::Field kVersionCheckRets[] = {
    Game::Doc::Req("isEnabled", "bool"),
};
const Game::Doc::Function kIsAddonVersionCheckEnabled{
    "Returns whether addons with an out-of-date ## Interface: are refused (the "
    "checkAddonVersion cvar).",
    {}, kVersionCheckRets};

const Game::Doc::Field kVersionCheckArgs[] = {
    Game::Doc::Req("enabled", "bool"),
};
const Game::Doc::Function kSetAddonVersionCheck{
    "Sets whether addons with an out-of-date ## Interface: are refused.", kVersionCheckArgs,
    {}};

struct Entry {
    const char *name;
    Game::Lua::CFunction fn;
    const Game::Doc::Function *doc;
};
const Entry kEntries[] = {
    {"GetAddOnName", &Script_GetAddOnName, &kGetAddOnName},
    {"GetAddOnTitle", &Script_GetAddOnTitle, &kGetAddOnTitle},
    {"GetAddOnNotes", &Script_GetAddOnNotes, &kGetAddOnNotes},
    {"IsAddOnLoadable", &Script_IsAddOnLoadable, &kIsAddOnLoadable},
    {"GetAddOnSecurity", &Script_GetAddOnSecurity, &kGetAddOnSecurity},
    {"DoesAddOnExist", &Script_DoesAddOnExist, &kDoesAddOnExist},
    {"IsAddOnLoaded", &Script_IsAddOnLoaded, &kIsAddOnLoaded},
    {"LoadAddOn", &Script_LoadAddOn, &kLoadAddOn},
    {"GetAddOnOptionalDependencies", &Script_GetAddOnOptionalDependencies,
     &kGetAddOnOptionalDependencies},
    {"GetAddOnDependencies", &Script_GetAddOnDependencies, &kGetAddOnDependencies},
    {"GetAddOnInterfaceVersion", &Script_GetAddOnInterfaceVersion, &kGetAddOnInterfaceVersion},
    {"GetAddOnInfo", &Script_GetAddOnInfo, &kGetAddOnInfo},
    {"IsAddOnLoadOnDemand", &Script_IsAddOnLoadOnDemand, &kIsAddOnLoadOnDemand},
    {"IsAddOnDefaultEnabled", &Script_IsAddOnDefaultEnabled, &kIsAddOnDefaultEnabled},
    {"IsAddonVersionCheckEnabled", &Script_IsAddonVersionCheckEnabled,
     &kIsAddonVersionCheckEnabled},
    {"SetAddonVersionCheck", &Script_SetAddonVersionCheck, &kSetAddonVersionCheck},
};

void RegisterLuaFunctions() {
    for (const Entry &e : kEntries)
        Game::Lua::RegisterTableFunction("C_AddOns", e.name, e.fn, e.doc);
    Game::Lua::RegisterIntegerEnum("Enum", "AddOnSecurityStatus", kAddOnSecurityStatusEntries,
                                   "AddOns");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace AddOns::Info
