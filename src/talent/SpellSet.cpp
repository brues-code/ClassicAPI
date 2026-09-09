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

// `Talent::SpellSet` plus its one Lua consumer,
// `C_SpellBook.IsClassTalentSpellBookItem(slotIndex, spellBank)`.

#include "SpellSet.h"

#include "Game.h"
#include "Offsets.h"
#include "spell/Lookup.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace Talent::SpellSet {

namespace {

// Every spellID in a Talent.dbc rank column. Each record stores rank-N
// spellIDs at `OFF_TALENT_SPELL_RANK + N*4` for N = 0..8; unused ranks are 0.
//
// Cached for the session (Talent.dbc is static). A call before the engine
// has loaded the DBC (pre-login) leaves the set empty and later calls retry
// until it fills.
const std::unordered_set<int> &RankSet() {
    static std::unordered_set<int> set;
    if (!set.empty())
        return set;
    auto *records = Game::Read<const uint8_t *const *>(
        Offsets::VAR_TALENT_DBC_RECORDS);
    const int count = Game::Read<int>(Offsets::VAR_TALENT_DBC_COUNT);
    if (records == nullptr || count <= 0)
        return set;
    for (int i = 1; i <= count; ++i) {
        const uint8_t *rec = records[i];
        if (rec == nullptr)
            continue;
        auto *ranks = Game::Ptr<const uint32_t>(rec, Offsets::OFF_TALENT_SPELL_RANK);
        for (int j = 0; j < Offsets::TALENT_MAX_RANKS; ++j) {
            if (ranks[j] != 0)
                set.insert(static_cast<int>(ranks[j]));
        }
    }
    return set;
}

// `RankSet` closed over SkillLineAbility's next-rank links: every talent
// rank spell plus each higher rank reachable from it (12294 → 21551 →
// 21552). Built once, after `RankSet` is available.
const std::unordered_set<int> &LineSet() {
    static std::unordered_set<int> set;
    if (!set.empty())
        return set;
    const std::unordered_set<int> &ranks = RankSet();
    if (ranks.empty())
        return set;

    // spellID -> the spell that supersedes it, from every SLA row that
    // carries a link.
    std::unordered_map<int, int> next;
    auto *records = Game::Read<const uint8_t *const *>(
        Offsets::VAR_SKILL_LINE_ABILITY_RECORDS);
    const int count = Game::Read<int>(Offsets::VAR_SKILL_LINE_ABILITY_COUNT);
    if (records != nullptr) {
        for (int i = 1; i <= count; ++i) {
            const uint8_t *rec = records[i];
            if (rec == nullptr)
                continue;
            const int spellID = Game::Read<int>(rec, Offsets::OFF_SLA_SPELL_ID);
            const int superseded =
                Game::Read<int>(rec, Offsets::OFF_SLA_SUPERCEDED_BY_SPELL);
            if (spellID > 0 && superseded > 0)
                next[spellID] = superseded;
        }
    }

    for (int spellID : ranks) {
        int cur = spellID;
        // A chain is at most a handful of ranks; the bound guards a cyclic
        // link in a modded DBC.
        for (int hops = 0; cur > 0 && hops < 32 && set.insert(cur).second; ++hops) {
            const auto it = next.find(cur);
            cur = (it == next.end()) ? 0 : it->second;
        }
    }
    return set;
}

// `C_SpellBook.IsClassTalentSpellBookItem(slotIndex, spellBank)` -> bool.
// A pet-book slot is never a talent.
int __fastcall Script_IsClassTalentSpellBookItem(void *L) {
    const bool isPet = Spell::Lookup::SpellBankArgToBookType(L, 2) != 0;
    const int spellID = Spell::Lookup::SpellbookItemArgsToID(L, 1, 2);
    Game::Lua::PushBool(L, !isPet && spellID > 0 && IsTalentLine(spellID));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_SpellBook", "IsClassTalentSpellBookItem",
                                     &Script_IsClassTalentSpellBookItem);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

bool IsTalentRank(int spellID) {
    return RankSet().count(spellID) != 0;
}

bool IsTalentLine(int spellID) {
    return LineSet().count(spellID) != 0;
}

} // namespace Talent::SpellSet
