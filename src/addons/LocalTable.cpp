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

// `C_AddOns.GetAddOnLocalTable(name)` — the public, gated view of an addon's
// private namespace table (the `local _, addonTable = ...` second vararg each
// addon file is called with; see luasyntax/AddonNamespace.h).
//
// Retail added this in 11.0 alongside the `## AllowAddOnTableAccess` TOC
// directive: one addon can read another's local table ONLY when the target
// opts in. We mirror that gate:
//   1. the named addon is currently LOADED, and
//   2. its `.toc` declares `## AllowAddOnTableAccess: 1`.
// Either check failing returns nil. An addon receiving its OWN table is a
// different, ungated path (its files are called with it at load) — an addon
// never needs permission for its own namespace.
//
// Directive spelling verified against the 1.15.8 Classic Era binary (both the
// `AllowAddOnTableAccess:` TOC key and the `GetAddOnLocalTable` name are
// present there). The vanilla 1.12 TOC parser drops unknown `##` directives,
// so we scan the `.toc` ourselves via `FUN_FILE_READ`.

#include "Game.h"
#include "Offsets.h"
#include "storm/File.h"
#include "addons/Registry.h"
#include "addons/Toc.h"
#include "luasyntax/AddonNamespace.h"
#include "storm/Memory.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace AddOns::LocalTable {

namespace {

// IS_LOADED is a by-name hash lookup returning the loaded byte (`entry+0x18`);
// passing the name string works because the lookup hashes it. See Offsets.h.
using IsLoadedFn_t = uint8_t(__fastcall *)(const char *name);

// Storm::File::Read routes through Embedded's file-read hook, which forwards
// every non-`!!!ClassicAPI` path to the original untouched — what we want here.

// True iff the addon's `.toc` has a line `## AllowAddOnTableAccess: <nonzero>`.
// The value is a TOC true/false value (Toc::Truthy), and one true line is
// enough, as in the 3.80.2 client. Not cached: GetAddOnLocalTable is not a
// hot path.
bool TocAllowsTableAccess(const char *name) {
    // A registered, loaded addon name never carries a path separator; reject
    // any that does so the read stays bounded to the addon's own folder.
    for (const char *p = name; *p; ++p)
        if (*p == '\\' || *p == '/')
            return false;

    char path[300];
    std::snprintf(path, sizeof path, "Interface\\AddOns\\%s\\%s.toc", name, name);

    void *buf = nullptr;
    size_t size = 0;
    if (!Storm::File::Read(path, &buf, &size, 1))
        return false;

    const bool allowed = AddOns::Toc::AnyTrue(static_cast<const char *>(buf), size,
                                              "AllowAddOnTableAccess");

    Storm::Free(buf, __FILE__, __LINE__);
    return allowed;
}

// `C_AddOns.GetAddOnLocalTable(name)` -> table, or nothing.
//
// The argument is read as every C_AddOns function reads it (an index or a
// name, with 3.80.2's errors). 3.80.2 returns no value, not nil, for an
// unknown addon and for one that stored no table.
int __fastcall Script_GetAddOnLocalTable(void *L) {
    const uint8_t *record = Addons::ResolveAddOnArg(
        L, "Usage: local table = C_AddOns.GetAddOnLocalTable(name)", nullptr);
    if (record == nullptr)
        return 0;
    const char *name = Addons::RecordName(record);

    auto isLoaded = reinterpret_cast<IsLoadedFn_t>(Offsets::FUN_ADDON_IS_LOADED);
    if (isLoaded(name) == 0) // not loaded
        return 0;

    if (!TocAllowsTableAccess(name)) // opted out / no directive
        return 0;

    LuaSyntax::PushAddonNamespace(L, name);
    return 1;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("name", "uiAddon", "An addon index or folder name."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("table", "table", nullptr,
                   "The table the addon's files receive as their second argument. Nothing is "
                   "returned unless the addon is loaded and its TOC sets "
                   "## AllowAddOnTableAccess: 1."),
};
const Game::Doc::Function kGetAddOnLocalTable{
    "Returns the private table of another addon that allows access to it.", kArgs, kRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_AddOns", "GetAddOnLocalTable",
                                     &Script_GetAddOnLocalTable, &kGetAddOnLocalTable);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace AddOns::LocalTable
