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

// `GetSpellBaseCooldown(spellID)` — the spell's unmodified cooldown in ms.
//
// 4.3.4's Script_GetSpellBaseCooldown (0x0090F380) pushes nil for a missing
// spell or one with no SpellCooldowns row, else the larger of the row's
// category recovery and recovery time (signed). 1.12 keeps those three
// SpellCooldowns columns inline in Spell.dbc; 4.3.4's extractor split a row
// out only where one of them is set, so "no row" here is all three zero.

#include "Lookup.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Spell::BaseCooldown {

namespace {

int __fastcall Script_GetSpellBaseCooldown(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || Game::Lua::GetTop(L) != 1) {
        Game::Lua::Error(L, "Usage: GetSpellBaseCooldown(spellID)");
        return 0;
    }

    const uint8_t *spell =
        Spell::Lookup::RecordForID(static_cast<int>(Game::Lua::ToNumber(L, 1)));
    if (spell == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const int32_t recovery = Game::Read<int32_t>(spell, Offsets::OFF_SPELL_RECORD_RECOVERY_TIME);
    const int32_t categoryRecovery =
        Game::Read<int32_t>(spell, Offsets::OFF_SPELL_RECORD_CATEGORY_RECOVERY_TIME);
    const int32_t startRecovery =
        Game::Read<int32_t>(spell, Offsets::OFF_SPELL_RECORD_START_RECOVERY_TIME);
    if (recovery == 0 && categoryRecovery == 0 && startRecovery == 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushNumber(L, static_cast<double>(recovery > categoryRecovery ? recovery
                                                                            : categoryRecovery));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("spellID", "number", "The spell to read."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("cooldownMS", "number",
                   "The base cooldown in milliseconds, or nil if the spell has no cooldown data."),
};
const Game::Doc::Function kGetSpellBaseCooldown{
    "Returns the base cooldown of a spell, before talents and items change it.",
    kArgs, kRets, "SpellGlobals"};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetSpellBaseCooldown", &Script_GetSpellBaseCooldown,
                                      &kGetSpellBaseCooldown);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::BaseCooldown
