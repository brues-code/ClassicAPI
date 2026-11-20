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

// The damage meter's enumerations, with retail's values: the C_DamageMeter
// ones (DamageMeterConstantsDocumentation.lua; the seven 3.80.2 registers in
// FUN_140bfd3b0) and the meter window's Edit
// Mode settings (EditModeManagerConstantsDocumentation.lua), which
// Blizzard_DamageMeter reads.

#include "Game.h"
#include "damagemeter/Totals.h"

namespace DamageMeter::Enums {

namespace {

using Entry = Game::Lua::EnumIntegerEntry;

const Entry kType[] = {
    {"DamageDone", TYPE_DAMAGE_DONE},
    {"Dps", TYPE_DPS},
    {"HealingDone", TYPE_HEALING_DONE},
    {"Hps", TYPE_HPS},
    {"Absorbs", TYPE_ABSORBS},
    {"Interrupts", TYPE_INTERRUPTS},
    {"Dispels", TYPE_DISPELS},
    {"DamageTaken", TYPE_DAMAGE_TAKEN},
    {"AvoidableDamageTaken", TYPE_AVOIDABLE_DAMAGE_TAKEN},
    {"Deaths", TYPE_DEATHS},
    {"EnemyDamageTaken", TYPE_ENEMY_DAMAGE_TAKEN},
};

const Entry kSessionType[] = {
    {"Overall", SESSION_OVERALL},
    {"Current", SESSION_CURRENT},
    {"Expired", SESSION_EXPIRED},
};

const Entry kSourceDisplayType[] = {
    {"None", DISPLAY_NONE},
    {"Ally", DISPLAY_ALLY},
    {"Enemy", DISPLAY_ENEMY},
};

const Entry kStorageType[] = {
    {"Damage", STORAGE_DAMAGE},
    {"HealingAndAbsorbs", STORAGE_HEALING_AND_ABSORBS},
    {"Absorbs", STORAGE_ABSORBS},
    {"Interrupts", STORAGE_INTERRUPTS},
    {"Dispels", STORAGE_DISPELS},
    {"DamageTaken", STORAGE_DAMAGE_TAKEN},
    {"AvoidableDamageTaken", STORAGE_AVOIDABLE_DAMAGE_TAKEN},
    {"Deaths", STORAGE_DEATHS},
    {"EnemyDamageTaken", STORAGE_ENEMY_DAMAGE_TAKEN},
};

const Entry kSpellDetailsDisplayType[] = {
    {"SpellCasted", DETAILS_SPELL_CASTED},
    {"UnitSpecificSpellCasted", DETAILS_UNIT_SPECIFIC_SPELL_CASTED},
    {"SpellAffected", DETAILS_SPELL_AFFECTED},
    {"Deaths", DETAILS_DEATHS},
    {"EnemyDamageTaken", DETAILS_ENEMY_DAMAGE_TAKEN},
};

// Registered for the shape only: 1.12 has no challenge mode or arena to
// combine sessions over, and no DamageMeterOverride table.
const Entry kCombineSessionType[] = {
    {"None", 0},
    {"ChallengeMode", 1},
    {"Arena", 2},
    {"ArenaMultiRound", 3},
};

const Entry kOverrideType[] = {
    {"Ignore", 0},
    {"AllowFriendlyFire", 1},
    {"RedirectSourceToOwner", 2},
    {"RedirectSourceToAuraCaster", 3},
    {"IgnoreForAbsorbSpell", 4},
};

const Entry kStyle[] = {
    {"Default", 0},
    {"Thin", 1},
    {"Bordered", 2},
    {"FullBackground", 3},
};

const Entry kVisibility[] = {
    {"Always", 0},
    {"InCombat", 1},
    {"Hidden", 2},
    {"InGroup", 3},
};

const Entry kNumbers[] = {
    {"Minimal", 0},
    {"Compact", 1},
    {"Complete", 2},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterIntegerEnum("Enum", "DamageMeterType", kType, "DamageMeterConstants");
    Game::Lua::RegisterIntegerEnum("Enum", "DamageMeterSessionType", kSessionType,
                                   "DamageMeterConstants");
    Game::Lua::RegisterIntegerEnum("Enum", "DamageMeterSourceDisplayType", kSourceDisplayType,
                                   "DamageMeterConstants");
    Game::Lua::RegisterIntegerEnum("Enum", "DamageMeterStorageType", kStorageType,
                                   "DamageMeterConstants");
    Game::Lua::RegisterIntegerEnum("Enum", "DamageMeterSpellDetailsDisplayType",
                                   kSpellDetailsDisplayType, "DamageMeterConstants");
    Game::Lua::RegisterIntegerEnum("Enum", "DamageMeterCombineSessionType", kCombineSessionType,
                                   "DamageMeterConstants");
    Game::Lua::RegisterIntegerEnum("Enum", "DamageMeterOverrideType", kOverrideType,
                                   "DamageMeterConstants");
    Game::Lua::RegisterIntegerEnum("Enum", "DamageMeterStyle", kStyle, "EditModeManagerConstants");
    Game::Lua::RegisterIntegerEnum("Enum", "DamageMeterVisibility", kVisibility,
                                   "EditModeManagerConstants");
    Game::Lua::RegisterIntegerEnum("Enum", "DamageMeterNumbers", kNumbers,
                                   "EditModeManagerConstants");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace DamageMeter::Enums
