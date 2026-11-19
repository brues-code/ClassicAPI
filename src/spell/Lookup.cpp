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
#include "baselib/Ascii.h"
#include "dbc/Lookup.h"

namespace Spell::Lookup {

const uint8_t *RecordForID(int spellID) {
    if (spellID <= 0)
        return nullptr;
    return DBC::Record(Offsets::VAR_SPELL_RECORDS,
                       Offsets::VAR_SPELL_RECORD_COUNT,
                       static_cast<uint32_t>(spellID));
}

const char *Name(const uint8_t *spellRecord) {
    return DBC::LocalizedString(spellRecord, Offsets::OFF_SPELL_NAMES);
}

const char *Rank(const uint8_t *spellRecord) {
    return DBC::LocalizedString(spellRecord, Offsets::OFF_SPELL_RECORD_RANK);
}

int DurationMs(const uint8_t *spellRecord, bool applyMods) {
    // `pet = 0` selects the player as the caster; the third argument is skipMod.
    using GetDuration_t = int(__fastcall *)(const uint8_t *spellRecord, int pet, int skipMod);
    return reinterpret_cast<GetDuration_t>(static_cast<uintptr_t>(
        Offsets::FUN_GET_SPELL_DURATION))(spellRecord, 0, applyMods ? 0 : 1);
}

const char *IconPath(const uint8_t *spellRecord, bool active) {
    if (spellRecord == nullptr)
        return nullptr;
    const uint32_t iconID = Game::Read<uint32_t>(
        spellRecord, active ? Offsets::OFF_SPELL_RECORD_ACTIVE_ICON_ID
                            : Offsets::OFF_SPELL_RECORD_ICON_ID);
    return DBC::StringField(Offsets::VAR_SPELL_ICON_RECORDS,
                            Offsets::VAR_SPELL_ICON_COUNT, iconID,
                            Offsets::OFF_SPELLICON_PATH);
}

const char *DisplayIconPath(const uint8_t *spellRecord) {
    if (spellRecord == nullptr)
        return nullptr;
    using WeaponIconPath_t = const char *(__cdecl *)();
    if (Game::Read<int32_t>(spellRecord, Offsets::OFF_SPELL_RECORD_EFFECT) ==
        Offsets::SPELL_EFFECT_ATTACK)
        return reinterpret_cast<WeaponIconPath_t>(
            static_cast<uintptr_t>(Offsets::FUN_ATTACK_ICON_PATH))();
    if ((Game::Read<uint32_t>(spellRecord, Offsets::OFF_SPELL_RECORD_ATTRIBUTES) &
         Offsets::SPELL_ATTR_RANGED) != 0 &&
        (Game::Read<uint32_t>(spellRecord, Offsets::OFF_SPELL_RECORD_ATTRIBUTES_EX2) &
         Offsets::SPELL_ATTR_EX2_AUTOREPEAT_FLAG) != 0) {
        if (const char *ranged = reinterpret_cast<WeaponIconPath_t>(
                static_cast<uintptr_t>(Offsets::FUN_RANGED_ICON_PATH))())
            return ranged;
    }
    return IconPath(spellRecord, false);
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

bool HasEffect(const uint8_t *spellRecord, int effect) {
    if (spellRecord == nullptr)
        return false;
    for (int i = 0; i < Offsets::SPELL_RECORD_EFFECT_COUNT; ++i) {
        if (Game::Read<int>(spellRecord, Offsets::OFF_SPELL_RECORD_EFFECT + i * 4) == effect)
            return true;
    }
    return false;
}

int CastTimeMs(int spellID, bool pet, bool allowNegative) {
    using Fn = int(__fastcall *)(int spellID, int pet, int allowNegative);
    return reinterpret_cast<Fn>(Offsets::FUN_GET_CAST_TIME)(spellID, pet ? 1 : 0, allowNegative ? 1 : 0);
}

int32_t PowerCost(int spellID, bool pet) {
    using Fn = int32_t(__fastcall *)(int spellID, int pet);
    return reinterpret_cast<Fn>(Offsets::FUN_GET_SPELL_COST)(spellID, pet ? 1 : 0);
}

int FindAuraEffect(const uint8_t *spellRecord, int aura) {
    if (spellRecord == nullptr)
        return -1;
    for (int i = 0; i < Offsets::SPELL_RECORD_EFFECT_COUNT; ++i) {
        if (Game::Read<int>(spellRecord, Offsets::OFF_SPELL_RECORD_EFFECT_APPLY_AURA_NAME + i * 4) ==
            aura)
            return i;
    }
    return -1;
}

bool HasAuraEffect(const uint8_t *spellRecord, int aura) {
    return FindAuraEffect(spellRecord, aura) >= 0;
}

int SpellbookSlotToID(int slot1Based, int bookType) {
    const int slot = slot1Based - 1;
    if (slot < 0 || slot >= Offsets::SPELLBOOK_MAX_SLOTS)
        return 0;
    return BookArray(bookType)[slot];
}

const int *BookArray(int bookType) {
    return reinterpret_cast<const int *>(static_cast<uintptr_t>(
        bookType == 1 ? Offsets::VAR_PET_SPELLBOOK : Offsets::VAR_PLAYER_SPELLBOOK));
}

int BookCount(int bookType) {
    const int count = Game::Read<int>(static_cast<uintptr_t>(
        bookType == 1 ? Offsets::VAR_PET_SPELLBOOK_COUNT : Offsets::VAR_PLAYER_SPELLBOOK_COUNT));
    return count < 0 ? 0 : (count > Offsets::SPELLBOOK_MAX_SLOTS ? Offsets::SPELLBOOK_MAX_SLOTS : count);
}

int BookTypeArg(void *L, int idx) {
    if (!Game::Lua::IsString(L, idx))
        return 0;
    return Ascii::EqualCI(Game::Lua::ToString(L, idx), "pet") ? 1 : 0;
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

int RecipeRowSpellID(uintptr_t entriesVar, uintptr_t countVar, double index1) {
    const uint32_t count = *reinterpret_cast<const uint32_t *>(countVar);
    if (!(index1 >= 1.0 && index1 < static_cast<double>(count) + 1.0))
        return 0;
    auto **entries = *reinterpret_cast<const uint8_t ***>(entriesVar);
    if (entries == nullptr)
        return 0;
    auto *entry = entries[static_cast<int>(index1) - 1];
    if (entry == nullptr)
        return 0;
    return static_cast<int>(*reinterpret_cast<const uint32_t *>(
        entry + Offsets::OFF_CRAFT_ENTRY_SPELL_ID));
}

int NthRecipeReagentItemID(const uint8_t *spellRecord, double reagentIndex1) {
    if (spellRecord == nullptr ||
        !(reagentIndex1 >= 1.0 &&
          reagentIndex1 < static_cast<double>(Offsets::SPELL_RECIPE_MAX_REAGENTS) + 1.0))
        return 0;
    const int n = static_cast<int>(reagentIndex1);
    auto *reagents = reinterpret_cast<const uint32_t *>(
        spellRecord + Offsets::OFF_SPELL_REAGENT_ID);
    int found = 0;
    for (int i = 0; i < Offsets::SPELL_RECIPE_MAX_REAGENTS; ++i) {
        const int itemID = static_cast<int>(reagents[i]);
        if (itemID == 0)
            return 0;
        if (++found == n)
            return itemID;
    }
    return 0;
}

int FindSpellbookSlot(int spellID, int *outBookType) {
    if (spellID <= 0)
        return 0;
    for (int bookType = 0; bookType <= 1; ++bookType) {
        int slot = 0;
        ForEachBookSpell(bookType, [&](int i, int id) {
            if (id != spellID)
                return true;
            slot = i + 1;
            return false;
        });
        if (slot != 0) {
            if (outBookType != nullptr)
                *outBookType = bookType;
            return slot;
        }
    }
    return 0;
}

} // namespace Spell::Lookup
