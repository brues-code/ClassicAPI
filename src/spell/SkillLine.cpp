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
#include "dbc/Lookup.h"

#include <cstdint>

namespace Spell::SkillLine {

namespace {

using SkillLineToSlot_t = int(__thiscall *)(void *player, uint32_t skillLineID);

// Reads UNIT_FIELD_BYTES_0 from the local player's descriptor. Same
// pattern Spell::Level::PlayerDescriptor uses. Returns (0, 0) when the
// player isn't resolvable (pre-login, character select); callers treat
// that as "no filter" and accept the first SLA match.
void ReadPlayerClassRace(uint8_t *outClassByte, uint8_t *outRaceByte) {
    *outClassByte = 0;
    *outRaceByte = 0;
    auto *player = static_cast<const uint8_t *>(Game::ResolveUnitToken("player"));
    if (player == nullptr)
        return;
    auto *desc = Game::Read<const uint8_t *>(
        player, Offsets::OFF_UNIT_DESCRIPTOR);
    if (desc == nullptr)
        return;
    *outClassByte = *(desc + Offsets::OFF_UNIT_DESCRIPTOR_CLASS_BYTE);
    *outRaceByte = *(desc + Offsets::OFF_UNIT_DESCRIPTOR_RACE_BYTE);
}

// Walks SkillLineAbility.dbc looking for rows with `spellID`. Returns
// the skill ID of the best-matching row, or 0 if no row exists.
//
// "Best" prefers a row whose class/race masks match the local player —
// so race/class-locked variants (Turtle WoW has many of these) resolve
// to the row appropriate to the asker. Falls back to the first match
// when no player-matching row exists (so the function still answers for
// spells the player can't actually use, e.g. inspecting another class's
// spell).
uint32_t FindSkillIDForSpell(int spellID) {
    if (spellID <= 0)
        return 0;

    auto *const *records = Game::Read<const uint8_t *const *>(
        static_cast<uintptr_t>(Offsets::VAR_SKILL_LINE_ABILITY_RECORDS));
    const int count = Game::Read<int>(
        static_cast<uintptr_t>(Offsets::VAR_SKILL_LINE_ABILITY_COUNT));
    if (records == nullptr || count <= 0)
        return 0;

    uint8_t classByte = 0;
    uint8_t raceByte = 0;
    ReadPlayerClassRace(&classByte, &raceByte);
    const uint32_t playerClassBit = classByte ? (1u << (classByte - 1)) : 0;
    const uint32_t playerRaceBit = raceByte ? (1u << (raceByte - 1)) : 0;

    uint32_t fallback = 0;

    for (int i = 1; i <= count; ++i) {
        const uint8_t *rec = records[i];
        if (rec == nullptr)
            continue;
        const int rowSpell = Game::Read<int>(
            rec, Offsets::OFF_SLA_SPELL_ID);
        if (rowSpell != spellID)
            continue;

        const uint32_t skill = Game::Read<uint32_t>(
            rec, Offsets::OFF_SLA_SKILL_ID);
        if (skill == 0)
            continue;
        if (fallback == 0)
            fallback = skill;

        // If we don't know who "player" is yet, we can't preference —
        // the first match wins.
        if (playerClassBit == 0)
            return skill;

        const uint32_t classMask = Game::Read<uint32_t>(
            rec, Offsets::OFF_SLA_CLASS_MASK);
        const uint32_t raceMask = Game::Read<uint32_t>(
            rec, Offsets::OFF_SLA_RACE_MASK);
        const uint32_t excludeClass = Game::Read<uint32_t>(
            rec, Offsets::OFF_SLA_EXCLUDE_CLASS);
        const uint32_t excludeRace = Game::Read<uint32_t>(
            rec, Offsets::OFF_SLA_EXCLUDE_RACE);
        if ((excludeClass & playerClassBit) != 0)
            continue;
        if ((excludeRace & playerRaceBit) != 0)
            continue;
        const bool classOk = (classMask == 0) ||
                              (classMask & playerClassBit) != 0;
        const bool raceOk = (raceMask == 0) ||
                             (raceMask & playerRaceBit) != 0;
        if (classOk && raceOk)
            return skill;
    }

    return fallback;
}

// `C_SpellBook.GetSpellSkillLine(spellID) → (name, skillID)`
//
// Resolves a `spellID` to the SkillLine.dbc row it belongs to via
// SkillLineAbility.dbc. The skill ID is what populates the spellbook
// tab name (e.g. Shield Bash → Protection, Fireball → Fire), the
// profession header (Tailoring, Engineering), or the weapon-skill row
// (Swords, Maces).
//
// Returns `(localizedName, skillID)` for spells that have an SLA entry
// — `localizedName` is the user-facing tab/skill name in the engine's
// current locale, pulled from `SkillLine.dbc::Name[locale]`.
//
// Returns `(nil, nil)` for:
//   - non-numeric / non-positive input
//   - spellIDs with no SkillLineAbility row (most temporary auras,
//     proc spells, item-on-use effects, GM commands)
//   - rows whose `skillID` doesn't resolve in SkillLine.dbc (shouldn't
//     happen in practice but matches the engine's defensive behavior)
//
// For spells with multiple SLA rows (race-locked variants, Turtle WoW
// faction-specific entries), `FindSkillIDForSpell` prefers a row whose
// class/race masks match the local player. This gives the "what tab
// is this spell in *for me*" answer when one exists, and falls back to
// the first matching row otherwise — so the function still answers for
// spells the local player can't use (inspecting other classes, parsing
// combat log entries from unfamiliar specs).
int __fastcall Script_GetSpellSkillLine(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::PushNil(L);
        Game::Lua::PushNil(L);
        return 2;
    }
    const int spellID = static_cast<int>(Game::Lua::ToNumber(L, 1));

    const uint32_t skillID = FindSkillIDForSpell(spellID);
    if (skillID == 0) {
        Game::Lua::PushNil(L);
        Game::Lua::PushNil(L);
        return 2;
    }

    const char *name = DBC::LocalizedField(
        Offsets::VAR_SKILL_LINE_RECORDS,
        Offsets::VAR_SKILL_LINE_COUNT,
        skillID,
        Offsets::OFF_SKILL_LINE_NAME);
    if (name == nullptr) {
        Game::Lua::PushNil(L);
        Game::Lua::PushNil(L);
        return 2;
    }

    Game::Lua::PushString(L, name);
    Game::Lua::PushNumber(L, static_cast<double>(skillID));
    return 2;
}

// `C_SpellBook.GetSkillLineName(skillLineID) → name`
//
// Localized `SkillLine.dbc::Name` for a skill-line id — the id → name
// half of the bridge addons build by hand. Pure DBC read: returns the
// name for ANY valid skill line (Herbalism = 182, Mining = 186, Swords,
// Defense, …) whether or not the player has learned it. `nil` for
// non-numeric / non-positive input or an id with no SkillLine.dbc row.
//
// Pairs with `GetSkillLineRank` (the learned-rank lookup): name comes
// from the DBC, rank from the player's live skill list.
int __fastcall Script_GetSkillLineName(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const int id = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const char *name =
        (id > 0) ? DBC::LocalizedField(Offsets::VAR_SKILL_LINE_RECORDS,
                                       Offsets::VAR_SKILL_LINE_COUNT,
                                       static_cast<uint32_t>(id),
                                       Offsets::OFF_SKILL_LINE_NAME)
                 : nullptr;
    if (name == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushString(L, name);
    return 1;
}

// `C_SpellBook.GetSkillLineRank(skillLineID) → curRank, maxRank, modifier`
//
// The local player's rank in a skill line, keyed by `SkillLine.dbc` id.
// Vanilla's `GetSkillLineInfo(index)` only walks the skill window by
// position and returns skills by *name*, so an addon starting from a
// SkillLine.dbc id (a quest's `requiredSkill`, "Herbalism" = 182, …) has
// to map id → localized name and string-match every skill line to recover
// the rank. This reads the player's live skill list keyed by id directly.
//
// Returns the same values `GetSkillLineInfo` reports for the line:
//   curRank  — current skill value (base, no temp bonus)
//   maxRank  — the line's cap at the player's level
//   modifier — temporary bonus (weapon oils, buffs; 0 if none)
//
// Returns `nil` (nothing) when the player hasn't learned the line
// (`FUN_SKILL_LINE_TO_SLOT` reports no slot), for bad input, or before
// the player object / skill sub-struct is resident (character select).
// Reads the CGPlayer `+0xE68` skill table; ranks verified in-game against
// Script_GetTradeSkillLine / Script_GetSkillLineInfo (Tailoring 261/300).
int __fastcall Script_GetSkillLineRank(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const int skillLineID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (skillLineID <= 0) {
        Game::Lua::PushNil(L);
        return 1;
    }

    auto *player = static_cast<uint8_t *>(Game::ResolveUnitToken("player"));
    if (player == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }

    auto toSlot = reinterpret_cast<SkillLineToSlot_t>(Offsets::FUN_SKILL_LINE_TO_SLOT);
    const int slot = toSlot(player, static_cast<uint32_t>(skillLineID));
    if (slot < 0) { // player doesn't have the skill line
        Game::Lua::PushNil(L);
        return 1;
    }

    auto *sub = Game::Read<const uint8_t *>(
        player, Offsets::OFF_CGPLAYER_INFO);
    if (sub == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }

    const uint8_t *rec = sub + Offsets::OFF_SKILL_INFO_TABLE +
                         slot * Offsets::SKILL_INFO_STRIDE;
    const uint32_t cur =
        Game::Read<uint16_t>(rec, Offsets::OFF_SKILL_INFO_CUR);
    const uint32_t max =
        Game::Read<uint16_t>(rec, Offsets::OFF_SKILL_INFO_MAX);
    const uint32_t modifier =
        Game::Read<uint16_t>(rec, Offsets::OFF_SKILL_INFO_BONUS);

    Game::Lua::PushNumber(L, static_cast<double>(cur));
    Game::Lua::PushNumber(L, static_cast<double>(max));
    Game::Lua::PushNumber(L, static_cast<double>(modifier));
    return 3;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_SpellBook", "GetSpellSkillLine",
                                      &Script_GetSpellSkillLine);
    Game::Lua::RegisterTableFunction("C_SpellBook", "GetSkillLineName",
                                      &Script_GetSkillLineName);
    Game::Lua::RegisterTableFunction("C_SpellBook", "GetSkillLineRank",
                                      &Script_GetSkillLineRank);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Spell::SkillLine