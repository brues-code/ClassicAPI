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

// `GetSpellDuration(spellID [, ignoreModifiers])` and its
// `C_Spell.GetSpellDuration` alias — the full duration of a spell in
// milliseconds.
//
// The unmodified answer is the server's own pre-modifier base
// (`Spell::Duration::BaseMs`): the SpellDuration.dbc row's base, a
// negative base taken as its absolute value, and -1 (until cancelled)
// read as 0. The default answer is the engine's duration helper
// (`FUN_GET_SPELL_DURATION`, skipMod = 0), which additionally scales by
// the local player's level and applies the player's duration SpellMods —
// the duration a cast by the local player lasts, and what nampower's
// `GetSpellDuration` reports. Before the player exists (glue screen,
// loading) there are no player modifiers to apply, so the base is
// returned.
//
// SpellDuration.dbc and its quirks are documented in Offsets.h. A
// combo-point finisher's row stores base = 1 combo point and max = 5;
// both answers here report the base, because only a real application
// knows how many points were spent (`AuraData.duration` has it).

#include "spell/Duration.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "spell/Arg.h"
#include "spell/Lookup.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Spell::Duration {

namespace {

using GetDuration_t = int(__fastcall *)(const uint8_t *spellRecord, int pet,
                                        char skipMod);

uint32_t PlayerMs(const uint8_t *spellRecord) {
    const int ms = reinterpret_cast<GetDuration_t>(
        static_cast<uintptr_t>(Offsets::FUN_GET_SPELL_DURATION))(
        spellRecord, 0, /*skipMod*/ 0);
    return ms > 0 ? static_cast<uint32_t>(ms) : 0;
}

bool IgnoreModifiersArg(void *L, int idx) {
    if (Game::Lua::Type(L, idx) == Game::Lua::TYPE_BOOLEAN)
        return Game::Lua::ToBoolean(L, idx) != 0;
    if (Game::Lua::IsNumber(L, idx))
        return Game::Lua::ToNumber(L, idx) != 0;
    return false;
}

int __fastcall Script_GetSpellDuration(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return 0; // nil for an unknown spell

    uint32_t ms = 0;
    if (!IgnoreModifiersArg(L, 2) && Unit::Identity::PlayerObject() != nullptr)
        ms = PlayerMs(record);
    if (ms == 0)
        ms = BaseMs(static_cast<uint32_t>(spellID));

    Game::Lua::PushNumber(L, static_cast<double>(ms));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kDurationArgs[] = {
    Game::Doc::Req("spell", "SpellIdentifier", "A spell ID, spell link, or spell name."),
    Game::Doc::Opt("ignoreModifiers", "boolean", nullptr,
                   "True for the server's pre-modifier base duration; false or nil "
                   "for the duration the local player's cast would apply."),
};
const Game::Doc::Field kDurationRets[] = {
    Game::Doc::Opt("durationMs", "number", nullptr,
                   "The full duration in milliseconds; 0 for a spell with no finite "
                   "duration; nil for an unknown spell."),
};
const Game::Doc::Function kGetSpellDuration{
    "The full duration of a spell in milliseconds.", kDurationArgs, kDurationRets,
    "SpellGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetSpellDuration", &Script_GetSpellDuration,
                                      &kGetSpellDuration);
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellDuration",
                                     &Script_GetSpellDuration, &kGetSpellDuration);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

uint32_t BaseMs(uint32_t spellID) {
    const uint8_t *spell = Spell::Lookup::RecordForID(static_cast<int>(spellID));
    if (spell == nullptr)
        return 0;
    const int durIdx = *reinterpret_cast<const int *>(
        spell + Offsets::OFF_SPELL_RECORD_DURATION_INDEX);
    if (durIdx < 0)
        return 0;
    const uint8_t *durRec = DBC::Record(Offsets::VAR_SPELL_DURATION_RECORDS,
                                        Offsets::VAR_SPELL_DURATION_COUNT,
                                        static_cast<uint32_t>(durIdx));
    if (durRec == nullptr)
        return 0;
    const int64_t base = *reinterpret_cast<const int *>(
        durRec + Offsets::OFF_SPELLDURATION_BASE_MS);
    if (base == -1)
        return 0;
    const int64_t ms = base < 0 ? -base : base;
    return static_cast<uint32_t>(ms);
}

} // namespace Spell::Duration
