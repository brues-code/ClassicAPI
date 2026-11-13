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

#include "Game.h"
#include "Offsets.h"
#include "spell/Lookup.h"

#include <cstdint>

namespace Spell::School {

namespace {

// Locale-independent English school names, indexed by the 0-based
// `Spell.dbc` School value at `+0x04`.
const char *const kSchoolNames[Offsets::SPELL_SCHOOL_COUNT] = {
    "Physical",  // 0
    "Holy",      // 1
    "Fire",      // 2
    "Nature",    // 3
    "Frost",     // 4
    "Shadow",    // 5
    "Arcane",    // 6
};

const uint8_t *GetSpellRecord(int spellID) {
    return Spell::Lookup::RecordForID(spellID);
}

// `GetSpellSchool(spellID)` — returns `(schoolID, schoolName)` for
// the given spell, where `schoolID` is 1-based (1=Physical, 7=Arcane)
// and `schoolName` is the locale-independent English name. Reads
// directly from `Spell.dbc` record `+0x04`, so works for any
// spellID — known to the player or not.
//
// Returns `nil` when the spellID is invalid (out of range) or the
// `Spell.dbc` slot is unpopulated. Vanilla 1.12 only stores
// single-school values; multi-school (TBC+ `SchoolMask`) doesn't
// apply here.
//
// Used by combat-log breakdown addons, dispel-eligibility checks,
// resistance-aware aura libraries, and damage-meter school tagging.
int __fastcall Script_GetSpellSchool(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0;
    const int spellID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const uint8_t *rec = GetSpellRecord(spellID);
    if (rec == nullptr)
        return 0;
    const uint32_t school0 = *reinterpret_cast<const uint32_t *>(
        rec + Offsets::OFF_SPELL_SCHOOL);
    if (school0 >= Offsets::SPELL_SCHOOL_COUNT)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(school0 + 1));
    Game::Lua::PushString(L, kSchoolNames[school0]);
    return 2;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("spellID", "number", "Any spell ID, known or not."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("schoolID", "number", nullptr,
                   "1 Physical, 2 Holy, 3 Fire, 4 Nature, 5 Frost, 6 Shadow, 7 Arcane; nil, "
                   "with the name, for an unknown spell ID."),
    Game::Doc::Opt("schoolName", "string", nullptr,
                   "The English school name, the same in every locale."),
};
const Game::Doc::Function kGetSpellSchool{
    "Returns the magic school of a spell.", kArgs, kRets, "SpellGlobals", true};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetSpellSchool", &Script_GetSpellSchool,
                                      &kGetSpellSchool);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::School
