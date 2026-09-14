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

// `C_ClassColor.GetClassColor(className)` — a class's color as a ColorMixin.
//
// Modern clients keep the class colors in the engine and let FrameXML read
// them out through this function. Neither half of that exists here: 1.12's
// `ChrClasses.dbc` has no color column (17 columns, all accounted for), and
// the binary carries no class-color table — searched it for the class float
// triples and for the hex strings, no hits.
//
// What this client does have is `RAID_CLASS_COLORS`, defined by FrameXML in
// `Fonts.xml` and given the mixin by `!!!ClassicAPI/Util/Color*.lua`. Every
// UI on the client already colors classes from it, so this reads that table
// instead of carrying a second copy of the nine colors — the same choice
// `C_UnitAuras.GetAuraDispelTypeColor` makes with the `DEBUFF_TYPE_*_COLOR`
// globals. One source of truth, one shared object per class, and a class
// another addon recolors stays recolored here (this client's stand-in for
// CUSTOM_CLASS_COLORS).
//
// The lookup goes through metamethods, so a `RAID_CLASS_COLORS` that answers
// for unknown keys answers here too — the pfUI shim in `Util/AddOnCompat.lua`
// hands back grey. A name that is not a string, and a missing table, both
// return nil.

#include "Game.h"

namespace Classes::Color {

namespace {

int __fastcall Script_GetClassColor(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_STRING) {
        Game::Lua::PushNil(L);
        return 1;
    }

    Game::Lua::PushString(L, "RAID_CLASS_COLORS");
    Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::PushNil(L);
        return 1;
    }

    // Index with the argument itself rather than with a `const char *` read
    // off the stack: the string stays anchored at index 1, so nothing it
    // points at can be collected while we look it up.
    Game::Lua::PushValue(L, 1);
    Game::Lua::GetTable(L, -2);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("className", "string", "A class file name, such as \"WARRIOR\"."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("classColor", "colorRGB", nullptr,
                   "The color for that class, or nil when the name is not a class."),
};
const Game::Doc::Function kGetClassColor{
    "The color a class is drawn in.", kArgs, kRets};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_ClassColor", "GetClassColor",
                                     &Script_GetClassColor, &kGetClassColor);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Classes::Color
