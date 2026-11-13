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

// Class metadata accessors backed by `ChrClasses.dbc`.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Names.h"

#include <cstdint>

namespace Classes::Info {

namespace {

int Count() {
    return *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_CHRCLASSES_COUNT));
}

// `FillLocalizedClassList(table [, isFemale]) → table` — fills the
// passed-in table with `[classToken] = localizedName` pairs for every
// class in `ChrClasses.dbc`, then returns the same table for chaining.
//
// Modern API supports an optional `isFemale` boolean to fetch female-
// form names. Vanilla 1.12's ChrClasses.dbc has no separate female
// name array — see `OFF_CHRCLASSES_FILENAME` in Offsets.h for the
// layout argument. The arg is accepted for signature parity with
// modern callers but ignored: the same names are returned either way.
//
// Iterates 1..count (the records array is 1-based — index 0 is
// unused, matching the standard DBC convention). Sparse class IDs
// (e.g. classID 6 was unused in vanilla pre-Death-Knight) have a
// NULL record pointer; we skip those without writing the table.
int __fastcall Script_FillLocalizedClassList(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L,
            "Usage: FillLocalizedClassList(table [, isFemale])");
        return 0;
    }

    const int count = Count();

    // Drop the optional `isFemale` arg (and any further junk) so the
    // table sits alone at idx 1. Each loop iteration pushes key+value
    // and `SetTable(L, 1)` consumes both, returning the stack to
    // [table] before the next iteration.
    Game::Lua::SetTop(L, 1);
    for (int i = 1; i <= count; i++) {
        // DBC::* are null-safe (empty result if ChrClasses.dbc isn't loaded)
        // and skip empty strings, so a sparse/absent class id just falls
        // through — same "fill what's available, don't crash" behavior.
        const char *token = DBC::ClassToken(static_cast<uint32_t>(i));
        if (token == nullptr)
            continue;
        const char *name = DBC::ClassName(static_cast<uint32_t>(i));
        if (name == nullptr)
            continue;

        Game::Lua::PushString(L, token);
        Game::Lua::PushString(L, name);
        Game::Lua::SetTable(L, 1);
    }
    return 1; // return the same table the caller passed in
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("classTable", "table", "Gets a [classFile] = localized name pair per class."),
    Game::Doc::Opt("isFemale", "bool", "false", "Ignored; the names are the same either way."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("classTable", "table", "The same table."),
};
const Game::Doc::Function kFillLocalizedClassList{
    "Fills a table with the localized name of every class, keyed by class file name.",
    kArgs, kRets, "UnitGlobals"};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("FillLocalizedClassList",
                                      &Script_FillLocalizedClassList,
                                      &kFillLocalizedClassList);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Classes::Info
