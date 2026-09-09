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

#include "Lookup.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"

#include <cstring>

namespace Spell::Lookup {

const uint8_t *RecordForID(int spellID) {
    if (spellID <= 0)
        return nullptr;
    return DBC::Record(Offsets::VAR_SPELL_RECORDS,
                       Offsets::VAR_SPELL_RECORD_COUNT,
                       static_cast<uint32_t>(spellID));
}

bool IsFitToFamily(const uint8_t *spellRecord, uint32_t family,
                   uint64_t flagMask) {
    if (spellRecord == nullptr)
        return false;
    if (*reinterpret_cast<const uint32_t *>(
            spellRecord + Offsets::OFF_SPELL_RECORD_FAMILY_NAME) != family)
        return false;
    const uint64_t flags = *reinterpret_cast<const uint64_t *>(
        spellRecord + Offsets::OFF_SPELL_RECORD_FAMILY_FLAGS);
    return (flags & flagMask) != 0;
}

int SpellbookSlotToID(int slot1Based, int bookType) {
    const int slot = slot1Based - 1;
    if (slot < 0 || slot >= Offsets::SPELLBOOK_MAX_SLOTS)
        return 0;
    const auto base = (bookType == 1) ? Offsets::VAR_PET_SPELLBOOK
                                      : Offsets::VAR_PLAYER_SPELLBOOK;
    auto *array = reinterpret_cast<const int *>(static_cast<uintptr_t>(base));
    return array[slot];
}

int SpellBankArgToBookType(void *L, int idx) {
    if (!Game::Lua::IsNumber(L, idx))
        return 0;
    return static_cast<int>(Game::Lua::ToNumber(L, idx)) == 1 ? 1 : 0;
}

int SpellbookItemArgsToID(void *L, int slotIdx, int bankIdx) {
    if (!Game::Lua::IsNumber(L, slotIdx))
        return 0;
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, slotIdx));
    return SpellbookSlotToID(slot, SpellBankArgToBookType(L, bankIdx));
}

int RecipeSlotSpellID(uintptr_t entriesVar, uintptr_t countVar, int slotIndex0) {
    if (slotIndex0 < 0)
        return 0;
    const int count = static_cast<int>(
        *reinterpret_cast<const uint32_t *>(countVar));
    if (slotIndex0 >= count)
        return 0;
    auto **entries = *reinterpret_cast<const uint8_t ***>(entriesVar);
    if (entries == nullptr)
        return 0;
    auto *entry = entries[slotIndex0];
    if (entry == nullptr)
        return 0;
    return static_cast<int>(*reinterpret_cast<const uint32_t *>(
        entry + Offsets::OFF_CRAFT_ENTRY_SPELL_ID));
}

int NthRecipeReagentItemID(const uint8_t *spellRecord, int reagentIndex1) {
    if (spellRecord == nullptr || reagentIndex1 < 1)
        return 0;
    auto *reagents = reinterpret_cast<const uint32_t *>(
        spellRecord + Offsets::OFF_SPELL_REAGENT_ID);
    int found = 0;
    for (int i = 0; i < Offsets::SPELL_RECIPE_MAX_REAGENTS; ++i) {
        const int itemID = static_cast<int>(reagents[i]);
        if (itemID == 0)
            return 0;
        if (++found == reagentIndex1)
            return itemID;
    }
    return 0;
}

int FindSpellbookSlot(int spellID, int *outBookType) {
    if (spellID <= 0)
        return 0;
    // Walk the full SPELLBOOK_MAX_SLOTS range rather than break-on-zero
    // — the BSS arrays are zero-padded past the populated count, so any
    // 0 entry is "empty slot" and won't match a positive spellID. Cost
    // is 1024 dword reads per book = 8KB of cache-friendly memory.
    auto *playerArray = reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_PLAYER_SPELLBOOK));
    for (int i = 0; i < Offsets::SPELLBOOK_MAX_SLOTS; i++) {
        if (playerArray[i] == spellID) {
            if (outBookType != nullptr)
                *outBookType = 0;
            return i + 1;
        }
    }
    auto *petArray = reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_PET_SPELLBOOK));
    for (int i = 0; i < Offsets::SPELLBOOK_MAX_SLOTS; i++) {
        if (petArray[i] == spellID) {
            if (outBookType != nullptr)
                *outBookType = 1;
            return i + 1;
        }
    }
    return 0;
}

int SpellNameToID(const char *name) {
    if (name == nullptr || *name == '\0')
        return 0;
    const int locale = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_LOCALE_INDEX));

    // A trailing "(subtext)" pins a specific rank — the vanilla
    // "SpellName(Rank N)" addressing that CastSpellByName and friends use
    // (a space before the paren is tolerated: "Mind Blast (Rank 8)"). The
    // subtext is matched against the record's localized Rank field. With no
    // subtext, we return the highest known rank of the base name.
    char base[256];
    char wantRank[64];
    bool haveRank = false;
    {
        const char *open = std::strrchr(name, '(');
        const char *close = (open != nullptr) ? std::strrchr(name, ')') : nullptr;
        size_t baseLen;
        if (open != nullptr && close != nullptr && close > open + 1) {
            baseLen = static_cast<size_t>(open - name);
            size_t rankLen = static_cast<size_t>(close - open - 1);
            if (rankLen >= sizeof(wantRank))
                rankLen = sizeof(wantRank) - 1;
            std::memcpy(wantRank, open + 1, rankLen);
            wantRank[rankLen] = '\0';
            haveRank = true;
        } else {
            baseLen = std::strlen(name);
        }
        while (baseLen > 0 &&
               (name[baseLen - 1] == ' ' || name[baseLen - 1] == '\t'))
            --baseLen; // trim any space between the name and the paren
        if (baseLen == 0 || baseLen >= sizeof(base))
            return 0;
        std::memcpy(base, name, baseLen);
        base[baseLen] = '\0';
    }

    // Player book first, then pet — retail's precedence. For a plain name we
    // keep the LAST matching slot: a spell's ranks are stored in ascending
    // order, so the final match is the highest rank the player knows. For a
    // rank subtext we return the exact name+rank match.
    const uintptr_t books[2] = {Offsets::VAR_PLAYER_SPELLBOOK,
                                Offsets::VAR_PET_SPELLBOOK};
    for (uintptr_t bookBase : books) {
        auto *array = reinterpret_cast<const int *>(bookBase);
        int best = 0;
        for (int i = 0; i < Offsets::SPELLBOOK_MAX_SLOTS; ++i) {
            const int spellID = array[i];
            if (spellID <= 0)
                continue; // BSS-zeroed empty slot
            const uint8_t *record = RecordForID(spellID);
            if (record == nullptr)
                continue;
            const char *sname = *reinterpret_cast<const char *const *>(
                record + Offsets::OFF_SPELL_NAMES + locale * 4);
            if (sname == nullptr || std::strcmp(sname, base) != 0)
                continue;
            if (!haveRank) {
                best = spellID; // highest rank (last match)
                continue;
            }
            const char *srank = *reinterpret_cast<const char *const *>(
                record + Offsets::OFF_SPELL_RECORD_RANK + locale * 4);
            if (srank != nullptr && std::strcmp(srank, wantRank) == 0)
                return spellID; // exact name + rank
        }
        if (!haveRank && best != 0)
            return best;
    }
    return 0;
}

} // namespace Spell::Lookup
