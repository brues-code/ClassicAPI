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

// `C_Spell.GetSchoolString(schoolMask)` — localized name for a damage
// school bitmask. Vanilla spells are single-school (the multi-school
// combos like "Frostfire" / "Shadowflame" / "Spellstorm" were
// TBC-and-later additions), so we resolve any single-bit mask to its
// `SPELL_SCHOOL<n>_CAP` global string and return "Unknown" for any
// combination — matching the modern API's documented fallback.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstdio>

namespace Spell::SchoolString {

namespace {

// Returns the 0-based school index for a single-bit mask, or -1 if
// the mask has zero or multiple bits set (or is out of the 7-school
// range vanilla supports).
int SingleBitSchoolIndex(uint32_t mask) {
    if (mask == 0 || (mask & (mask - 1)) != 0)
        return -1;
    for (int i = 0; i < 7; ++i) {
        if (mask == (1u << i))
            return i;
    }
    return -1;
}

} // namespace

static int __fastcall Script_GetSchoolString(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::PushString(L, "Unknown");
        return 1;
    }
    const auto mask = static_cast<uint32_t>(Game::Lua::ToNumber(L, 1));
    const int idx = SingleBitSchoolIndex(mask);
    if (idx < 0) {
        Game::Lua::PushString(L, "Unknown");
        return 1;
    }
    char globalName[24];
    std::snprintf(globalName, sizeof globalName, "SPELL_SCHOOL%d_CAP", idx);
    Game::Lua::PushLocalizedString(L, globalName, "Unknown");
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("schoolMask", "number",
                   "A damage-school bitmask, such as 4 for fire or 32 for shadow."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("school", "string",
                   "The localized school name, or \"Unknown\" when the mask names "
                   "more than one school."),
};
const Game::Doc::Function kGetSchoolString{
    "The localized name of a damage school.", kArgs, kRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "GetSchoolString",
                                     &Script_GetSchoolString, &kGetSchoolString);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::SchoolString
