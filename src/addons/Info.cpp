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

// `C_AddOns.*` convenience wrappers. Modern WoW splits the legacy
// `GetAddOnInfo(arg) → 7-tuple` into a half-dozen single-field
// getters; addons backporting from later expansions call them
// directly and break without these wrappers.
//
// Implementation: every wrapper dispatches to the same per-field
// accessors `Script_GetAddOnInfo` calls internally — title/notes via
// the name-keyed lookups, security via the addon-registry hash
// table, loadable via the recursive can-load walker. Bypassing
// `Script_GetAddOnInfo` means we never push the 7 returns we'd
// have to immediately discard, and skips its `lua_error` on
// out-of-range numeric indices (we surface those as nil instead).

#include "Game.h"
#include "Offsets.h"
#include "addons/LoadState.h"
#include "addons/Registry.h"

#include <cstdint>

namespace AddOns::Info {

namespace {

using GetByIndex_t = void *(__fastcall *)(int idx0Based);
using GetAddOnCount_t = int(__fastcall *)();
using FieldByName_t = const char *(__fastcall *)(const char *name);
using EntryByteFn_t = uint8_t(__fastcall *)(const uint8_t *entry);
using GetSecurityIndex_t = uint32_t(__fastcall *)(const uint8_t *entry);
using CanLoadFn_t = uint8_t(__fastcall *)(const uint8_t *entry, char fullCheck,
                                          int *outStatus, int *outDepHandle,
                                          const char *accountName);
using GetAccountName_t = const char *(*)();
// Hash-lookup by name → the matched record's RequiredDeps descriptor
// (`record + 0x48`), NULL on miss. We use it only to recover the
// record base; see `Script_GetAddOnOptionalDependencies`.
using ResolveReqDeps_t = uint8_t *(__fastcall *)(const char *nameOrEntry);

// Resolves the user's arg to a `const uint8_t *` that the engine's
// per-field accessors can consume. They all read the addon name as
// a NUL-terminated string starting at the input pointer and case-
// insensitively match it against the registry, so either of these
// shapes works:
//   - the name pointer `GetByIndex` returns (`[entry+0x14]`)
//   - a raw `const char *` to a Lua string
//
// Mirrors `Script_GetAddOnInfo`'s own input handling exactly — it
// doesn't validate existence for string input either, just hands
// the string to each accessor. Unknown names produce NULL/default
// returns from each accessor, never an error. `DoesAddOnExist` is
// the dedicated existence-check API and walks the flat array
// directly (`AddOnExistsByName` below).
//
// Returns nullptr only for out-of-range numeric input — the engine
// returns NULL from `FUN_0051DF00` past the registered count.
const uint8_t *ResolveAddOnName(void *L) {
    const int t = Game::Lua::Type(L, 1);
    if (t == Game::Lua::TYPE_NUMBER) {
        const int idx = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
        auto fn = reinterpret_cast<GetByIndex_t>(Offsets::FUN_ADDON_GET_BY_INDEX);
        return static_cast<const uint8_t *>(fn(idx));
    }
    if (t == Game::Lua::TYPE_STRING)
        return reinterpret_cast<const uint8_t *>(Game::Lua::ToString(L, 1));
    return nullptr;
}

// Probes the engine's addon hash table for a name match using
// `FUN_ADDON_CAN_LOAD`, which writes one of 8 status codes via its
// out-param. Only `status == 1` ("MISSING") indicates a genuine
// hash-table miss — every other code (`DISABLED`, `INSECURE`,
// `BANNED`, `INTERFACE_VERSION`, `LOADABLE`, etc.) means the addon
// IS registered. The flat array `FUN_ADDON_GET_BY_INDEX` walks is
// user-facing only and excludes Blizzard_* LoD addons, so the array
// scan can't be used here.
//
// `FUN_ADDON_CAN_LOAD` toggles a recursion-guard byte at
// `entry+0x2D` during its dep walk but always restores it before
// returning, so calling this as a side-effect-free probe is safe.
bool AddOnExistsByName(const char *name) {
    if (name == nullptr || *name == '\0')
        return false;
    auto canLoad = reinterpret_cast<CanLoadFn_t>(Offsets::FUN_ADDON_CAN_LOAD);
    auto getAccount = reinterpret_cast<GetAccountName_t>(
        Offsets::FUN_GET_LOGIN_ACCOUNT_NAME);
    int status = 0;
    int depHandle = 0;
    canLoad(reinterpret_cast<const uint8_t *>(name), /*fullCheck=*/1,
            &status, &depHandle, getAccount());
    return status != 1;
}

// Pushes either the string returned by `accessor(entryAsName)` or
// nil if the entry is missing. The entry pointer doubles as the
// addon's name (inline buffer at +0), so the field accessors that
// take a name string can be fed the entry directly.
int PushFieldOrNil(void *L, FieldByName_t accessor) {
    const uint8_t *entry = ResolveAddOnName(L);
    if (entry == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushString(L,
        accessor(reinterpret_cast<const char *>(entry)));
    return 1;
}

// `C_AddOns.GetAddOnName(indexOrName)` — the addon's directory name.
// For numeric input, returns the engine's canonical casing. For
// string input, returns the input verbatim once existence is
// confirmed; nil for an unregistered name.
//
// String-input existence isn't free: `ResolveAddOnName` echoes the
// string for the field accessors below (`GetAddOnTitle`, etc.) which
// short-circuit cleanly on unknown names via the engine's own NULL-
// return paths. `GetAddOnName` doesn't call any engine accessor, so
// it'd otherwise echo any string. Probe the registry explicitly.
int __fastcall Script_GetAddOnName(void *L) {
    const uint8_t *entry = ResolveAddOnName(L);
    if (entry == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_STRING &&
        !AddOnExistsByName(reinterpret_cast<const char *>(entry))) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushString(L, reinterpret_cast<const char *>(entry));
    return 1;
}

// `C_AddOns.GetAddOnTitle(indexOrName)` — `## Title:` from the .toc,
// with color-code escapes applied. nil for missing addons.
int __fastcall Script_GetAddOnTitle(void *L) {
    return PushFieldOrNil(L, reinterpret_cast<FieldByName_t>(
                                 Offsets::FUN_ADDON_GET_TITLE_BY_NAME));
}

// `C_AddOns.GetAddOnNotes(indexOrName)` — `## Notes:` from the .toc.
// nil for missing addons or addons without notes.
int __fastcall Script_GetAddOnNotes(void *L) {
    return PushFieldOrNil(L, reinterpret_cast<FieldByName_t>(
                                 Offsets::FUN_ADDON_GET_NOTES_BY_NAME));
}

// `C_AddOns.IsAddOnLoadable(indexOrName [, character, demandLoaded])`
// — returns `(loadable, reason)`. Modern signature has two optional
// args (`character`, `demandLoaded`) that we currently ignore;
// vanilla 1.12 has no per-character addon enable state and no
// demand-load distinction the engine exposes.
//
// `loadable` is a real boolean; `reason` is the status string from
// the engine's reason table (e.g. `"INSECURE"`, `"DISABLED"`,
// `"INTERFACE_VERSION"`) or nil when the addon is loadable.
//
// Mirrors `Script_GetAddOnInfo`'s loadable branch
// (`FUN_004E26E0` → entries+0x008): load-on-demand addons always
// report loadable=true / reason=nil; everything else runs the
// recursive dep-resolving `CAN_LOAD` walker and reads the failure
// status code it writes.
int __fastcall Script_IsAddOnLoadable(void *L) {
    const uint8_t *entry = ResolveAddOnName(L);
    if (entry == nullptr) {
        Game::Lua::PushBool(L, 0);
        Game::Lua::PushNil(L);
        return 2;
    }

    // Already-loaded addons are trivially loadable — skip the
    // recursive dep walk and return (true, nil) directly. `IS_LOADED`
    // reads the byte at `entry+0x18` (the loader sets that to 1 on
    // successful load).
    auto isLoaded = reinterpret_cast<EntryByteFn_t>(
        Offsets::FUN_ADDON_IS_LOADED);
    if (isLoaded(entry) != 0) {
        Game::Lua::PushBoolean(L, 1);
        Game::Lua::PushNil(L);
        return 2;
    }

    auto canLoad = reinterpret_cast<CanLoadFn_t>(Offsets::FUN_ADDON_CAN_LOAD);
    auto getAccount = reinterpret_cast<GetAccountName_t>(
        Offsets::FUN_GET_LOGIN_ACCOUNT_NAME);

    int status = 0;
    int depHandle = 0;
    const uint8_t ok = canLoad(entry, /*fullCheck=*/1,
                               &status, &depHandle, getAccount());

    Game::Lua::PushBoolean(L, ok);

    // Reason: nil when the addon is loadable AND no status code /
    // dep handle was written; otherwise the static string at
    // `LOADABLE_REASON_TABLE[status]`. The engine's dep-fallback
    // path (`status==0 && depHandle!=0`) produces a "DEP: <name>"
    // string we don't reproduce — modern API consumers care about
    // the status enum, not the dep name. They'd get the canonical
    // `"LOADABLE"` slot-0 string in that case, which doesn't match
    // any modern reason key, so we suppress to nil.
    if (status == 0 && depHandle == 0) {
        Game::Lua::PushNil(L);
    } else if (status <= 0 || status > 7) {
        Game::Lua::PushNil(L);
    } else {
        auto *table = reinterpret_cast<const char *const *>(
            static_cast<uintptr_t>(Offsets::VAR_ADDON_LOADABLE_REASON_TABLE));
        Game::Lua::PushString(L, table[status]);
    }
    return 2;
}

// `C_AddOns.GetAddOnSecurity(indexOrName) → Enum.AddOnSecurityStatus`
// Matches modern shape: an integer enum, not a string. The engine's
// `FUN_ADDON_GET_SECURITY_INDEX` returns 0..2 directly matching the
// `Secure` / `Insecure` / `Banned` slots; we add `3` (`NotAvailable`)
// for unknown addons since the modern API uses that for the missing
// case rather than returning nil.
int __fastcall Script_GetAddOnSecurity(void *L) {
    const uint8_t *entry = ResolveAddOnName(L);
    int value;
    if (entry == nullptr) {
        // Numeric OOR — no entry to query.
        value = 3; // NotAvailable
    } else if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_STRING &&
               !AddOnExistsByName(reinterpret_cast<const char *>(entry))) {
        // String input that doesn't resolve in the hash table — the
        // security accessor would return its default (Insecure)
        // otherwise, masking unknown-addon as a real answer.
        value = 3; // NotAvailable
    } else {
        auto getIdx = reinterpret_cast<GetSecurityIndex_t>(
            Offsets::FUN_ADDON_GET_SECURITY_INDEX);
        value = static_cast<int>(getIdx(entry));
    }
    Game::Lua::PushNumber(L, static_cast<double>(value));
    return 1;
}

// `C_AddOns.IsAddOnLoaded(indexOrName)` → `(loadedOrLoading, loaded)`.
//
// Modern WoW distinguishes "load-in-progress" from "fully loaded", and
// the two really are distinguishable here: the engine's loaded byte
// (`entry+0x18`) is set at the START of a load, before the addon's files
// run, so the byte alone answers `loadedOrLoading`, not `loaded`. The
// window is reachable from Lua — an addon's own file-scope code, and any
// dependency pulled in during it, run while the byte already reads set.
//
// So `loadedOrLoading` is the engine's byte, and `loaded` additionally
// requires the addon not to be mid-load, which `AddOns::LoadState`
// tracks from the loader itself.
//
// Resolves both numeric indices (via `FUN_ADDON_GET_BY_INDEX`) and
// string names. `FUN_ADDON_GET_BY_INDEX` returns the entry's name
// pointer, so `IS_LOADED` (like the other per-field accessors) takes
// either that or a Lua-side string transparently — and that same
// pointer is the name `LoadState` matches on.
int __fastcall Script_IsAddOnLoaded(void *L) {
    const uint8_t *entry = ResolveAddOnName(L);
    if (entry == nullptr) {
        Game::Lua::PushBool(L, false);
        Game::Lua::PushBool(L, false);
        return 2;
    }
    auto isLoaded = reinterpret_cast<EntryByteFn_t>(
        Offsets::FUN_ADDON_IS_LOADED);
    const bool loadedOrLoading = isLoaded(entry) != 0;
    const bool loaded =
        loadedOrLoading &&
        !AddOns::LoadState::IsLoading(reinterpret_cast<const char *>(entry));
    Game::Lua::PushBool(L, loadedOrLoading);
    Game::Lua::PushBool(L, loaded);
    return 2;
}

// `C_AddOns.DoesAddOnExist(indexOrName)` — true iff the engine's
// addon registry has a matching entry. For numeric input we trust
// `GetByIndex`'s OOR-NULL; for string input we walk the flat array,
// since the engine field accessors don't distinguish "unknown
// addon" from "addon exists but field is empty" (Blizzard_* addons
// have empty titles, so we can't use `GetTitleByName` as a probe).
int __fastcall Script_DoesAddOnExist(void *L) {
    const int t = Game::Lua::Type(L, 1);
    if (t == Game::Lua::TYPE_NUMBER) {
        Game::Lua::PushBool(L, ResolveAddOnName(L) != nullptr);
        return 1;
    }
    if (t == Game::Lua::TYPE_STRING) {
        const char *name = Game::Lua::ToString(L, 1);
        Game::Lua::PushBool(L, AddOnExistsByName(name));
        return 1;
    }
    Game::Lua::PushBoolean(L, 0);
    return 1;
}

// `C_AddOns.GetAddOnOptionalDependencies(indexOrName)` — the addon's
// `## OptionalDeps:` names as multiple return values, in declared order.
// The exact shape of the stock `GetAddOnDependencies`, but reading the
// *optional* dep array instead of the required one. No optional deps or
// an unknown name → returns nothing.
//
// Vanilla parses `## OptionalDeps:` in the TOC parser `FUN_0051c9b0`
// into a `{cap, count, data, quantum}` name-array descriptor at
// `record+0x38` (count@+0x3C, data@+0x40), but ships no getter for it.
//
// Like stock, `lua_checkstack(count)` before the pushes — a C function
// is only guaranteed LUA_MINSTACK (20) free slots.
int __fastcall Script_GetAddOnOptionalDependencies(void *L) {
    const uint8_t *record = Addons::ResolveRecord(
        L, "Usage: C_AddOns.GetAddOnOptionalDependencies(index or \"name\")");
    if (record == nullptr)
        return 0;

    const uint32_t count = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ADDON_OPTIONALDEPS_COUNT);
    if (count == 0)
        return 0;
    auto names = *reinterpret_cast<const char *const *const *>(
        record + Offsets::OFF_ADDON_OPTIONALDEPS_ARRAY);
    if (names == nullptr)
        return 0;
    Game::Lua::CheckStack(L, static_cast<int>(count));
    for (uint32_t i = 0; i < count; ++i)
        Game::Lua::PushString(L, names[i]); // NULL → pushnil, per engine
    return static_cast<int>(count);
}

// `C_AddOns.GetAddOnInterfaceVersion(indexOrName)` — the `## Interface:`
// number the engine parsed into `record+0x1C`, the same value its
// loadability check compares against the client version. A comma-list
// is already reduced to retail's pick (closest value at or below the
// client version) by `AddOns::TocRewrite` before the parser sees it.
// nil for an unknown name.
int __fastcall Script_GetAddOnInterfaceVersion(void *L) {
    const uint8_t *record = Addons::ResolveRecord(
        L, "Usage: C_AddOns.GetAddOnInterfaceVersion(index or \"name\")");
    if (record == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushNumber(L, static_cast<double>(*reinterpret_cast<const uint32_t *>(
                                 record + Offsets::OFF_ADDON_INTERFACE_VERSION)));
    return 1;
}

// `Enum.AddOnSecurityStatus` — what `C_AddOns.GetAddOnSecurity`
// returns. Slots 0..2 match the engine's own ordering at
// `VAR_ADDON_SECURITY_TABLE`; slot 3 is our addition for
// unknown-addon input, matching the modern API.
constexpr Game::Lua::EnumIntegerEntry kAddOnSecurityStatusEntries[] = {
    {"Secure",       0},
    {"Insecure",     1},
    {"Banned",       2},
    {"NotAvailable", 3},
};

const Game::Doc::Field kInterfaceVersionArgs[] = {
    Game::Doc::Req("name", "uiAddon", "An addon index or folder name."),
};
const Game::Doc::Field kInterfaceVersionRets[] = {
    Game::Doc::Opt("interfaceVersion", "number", nullptr,
                   "The ## Interface: value from the TOC, or nil for an unknown name."),
};
const Game::Doc::Function kGetAddOnInterfaceVersion{
    "Returns the Interface directive of an addon TOC. For a comma-separated list, "
    "returns the closest value at or below the client interface version.",
    kInterfaceVersionArgs, kInterfaceVersionRets};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_AddOns", "GetAddOnName",
                                     &Script_GetAddOnName);
    Game::Lua::RegisterTableFunction("C_AddOns", "GetAddOnTitle",
                                     &Script_GetAddOnTitle);
    Game::Lua::RegisterTableFunction("C_AddOns", "GetAddOnNotes",
                                     &Script_GetAddOnNotes);
    Game::Lua::RegisterTableFunction("C_AddOns", "IsAddOnLoadable",
                                     &Script_IsAddOnLoadable);
    Game::Lua::RegisterTableFunction("C_AddOns", "GetAddOnSecurity",
                                     &Script_GetAddOnSecurity);
    Game::Lua::RegisterTableFunction("C_AddOns", "DoesAddOnExist",
                                     &Script_DoesAddOnExist);
    Game::Lua::RegisterTableFunction("C_AddOns", "IsAddOnLoaded",
                                     &Script_IsAddOnLoaded);
    // The stock global, mirrored into the namespace — the engine's own
    // handler already has the Lua C ABI and the modern return shape.
    Game::Lua::RegisterTableFunction(
        "C_AddOns", "LoadAddOn",
        reinterpret_cast<Game::Lua::CFunction>(
            static_cast<uintptr_t>(Offsets::FUN_SCRIPT_LOAD_ADDON)));
    Game::Lua::RegisterTableFunction("C_AddOns", "GetAddOnOptionalDependencies",
                                     &Script_GetAddOnOptionalDependencies);
    Game::Lua::RegisterTableFunction("C_AddOns", "GetAddOnInterfaceVersion",
                                     &Script_GetAddOnInterfaceVersion,
                                     &kGetAddOnInterfaceVersion);
    Game::Lua::RegisterIntegerEnum(
        "Enum", "AddOnSecurityStatus",
        kAddOnSecurityStatusEntries,
        sizeof(kAddOnSecurityStatusEntries) /
            sizeof(kAddOnSecurityStatusEntries[0]));
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace AddOns::Info
