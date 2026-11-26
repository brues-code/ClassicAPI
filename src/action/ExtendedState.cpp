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

// See ExtendedState.h. Every test below is the engine function it names,
// decompiled (benilla-re systems/ui/actionbar.md), with `s_actionSlots[slot]`
// replaced by the word handed in and GetActionSlotSpell by
// Action::Extended::SpellFor. Where the engine calls a helper, so does this.
//
// Recount. FUN_ACTION_RECOUNT_ITEM_SLOTS is co-hooked: after the engine has
// recounted and repainted its own slots of an item, our item words of that
// entry get FUN_ACTION_ITEM_COUNT and Notify, the same moment. Its callers
// are the item-record callback FUN_ACTION_ITEM_COUNT hands GetRecord (so a
// template asked for by a count arrives through here), the container slot
// handlers, and the item field watchers. A template asked for while there is
// no active player goes through Item::Data instead (the count call returns 0
// before it asks), so its completion repaints our slots of that item too.

#include "action/ExtendedState.h"

#include "Game.h"
#include "Offsets.h"
#include "action/Extended.h"
#include "equipmentset/Action.h"
#include "event/Custom.h"
#include "item/Cooldown.h"
#include "item/Data.h"
#include "item/Record.h"
#include "object/Resolve.h"
#include "spell/Cooldown.h"
#include "spell/Lookup.h"
#include "spell/Range.h"
#include "spell/SkillLine.h"
#include "spell/Usable.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Action::ExtendedState {

namespace {

using ItemCount_t = uint32_t(__fastcall *)(uint32_t itemID);
using ItemSpellHeld_t = uint32_t(__fastcall *)(uint32_t itemID);
using SpellIsUsable_t = int(__fastcall *)(const uint8_t *record, int *outNoMana);
using TargetingSpell_t = uint32_t(__cdecl *)();
using FindItem_t = const uint8_t *(__thiscall *)(const void *invMgr, uint32_t itemID,
                                                 uint32_t flags);
using Recount_t = void(__fastcall *)(uint32_t itemID);

constexpr int kCount = Extended::kEndSlot0 - Extended::kFirstSlot0;

struct State {
    uint32_t count = 0;
    bool usable = false;
    bool noMana = false;
};
State g_state[kCount];

Recount_t g_origRecount = nullptr;

State &At(int slot0) { return g_state[slot0 - Extended::kFirstSlot0]; }

uint32_t ItemOf(uint32_t word) {
    return word != 0 && (word & Offsets::ACTION_TYPE_MASK) == Offsets::ACTION_TYPE_ITEM_BY_ID
               ? word & Offsets::ACTION_PAYLOAD_MASK_ITEM_BY_ID
               : 0;
}

uint32_t CountItem(uint32_t itemID) {
    return reinterpret_cast<ItemCount_t>(Offsets::FUN_ACTION_ITEM_COUNT)(itemID);
}

// The resolved spell's record, as the tests read it: `records[id]` for an id in
// the table, null otherwise (the -1 of an unresolved macro name included).
const uint8_t *SpellRecord(uint32_t word, uint32_t *book) {
    const uint32_t spell = Extended::SpellFor(word, book);
    if (spell == 0 || static_cast<int>(spell) < 0)
        return nullptr;
    return ::Spell::Lookup::RecordForID(static_cast<int>(spell));
}

// The notifier's recompute: the pair FUN_ACTION_SLOT_USABLE answers.
void Recompute(int slot0) {
    State &s = At(slot0);
    s.usable = Usable(Extended::Get(slot0), s.count, &s.noMana);
}

void Recount(int slot0) {
    const uint32_t item = ItemOf(Extended::Get(slot0));
    At(slot0).count = item != 0 ? CountItem(item) : 0;
}

void RepaintItem(uint32_t itemID) {
    if (!Extended::Loaded() || itemID == 0)
        return;
    bool held = false;
    for (int slot0 = Extended::kFirstSlot0; slot0 < Extended::kEndSlot0 && !held; ++slot0)
        held = ItemOf(Extended::Get(slot0)) == itemID;
    if (!held)
        return;
    const uint32_t count = CountItem(itemID);
    for (int slot0 = Extended::kFirstSlot0; slot0 < Extended::kEndSlot0; ++slot0) {
        if (ItemOf(Extended::Get(slot0)) != itemID)
            continue;
        At(slot0).count = count;
        Notify(slot0);
    }
}

void __fastcall Recount_h(uint32_t itemID) {
    g_origRecount(itemID);
    RepaintItem(itemID);
}

void OnItemLoaded(uint32_t itemID, bool success) {
    if (success)
        RepaintItem(itemID);
}

const Game::HookAutoRegister _hookRecount{
    Offsets::FUN_ACTION_RECOUNT_ITEM_SLOTS, reinterpret_cast<void *>(&Recount_h),
    reinterpret_cast<void **>(&g_origRecount)};
const Item::Data::LoadSubscriber _itemLoaded{&OnItemLoaded};

} // namespace

// --- Our slots ----------------------------------------------------------------

void Stored(int slot0) {
    if (!Extended::Owns(slot0))
        return;
    Recount(slot0);
    Notify(slot0);
}

void Notify(int slot0) {
    if (!Extended::Owns(slot0))
        return;
    Recompute(slot0);
    // The notifier's `quiet == 0` tail: FUN_FIRE_EVENT(0xD5, "%d", slot0 + 1)
    // (0x004E5998). Its CMSG_SET_ACTION_BUTTON branch is for its own slots.
    Event::Custom::Fire(Offsets::EVENT_ACTIONBAR_SLOT_CHANGED, "%d", slot0 + 1);
}

void Reload() {
    for (int slot0 = Extended::kFirstSlot0; slot0 < Extended::kEndSlot0; ++slot0) {
        Recount(slot0);
        Recompute(slot0);
    }
}

bool CachedUsable(int slot0, bool *noMana) {
    if (!Extended::Owns(slot0) || !Extended::Ready()) {
        *noMana = false;
        return false;
    }
    *noMana = At(slot0).noMana;
    return At(slot0).usable;
}

uint32_t CachedCount(int slot0) {
    if (!Extended::Owns(slot0) || !Extended::Ready() || ItemOf(Extended::Get(slot0)) == 0)
        return 0;
    return At(slot0).count;
}

int RefreshUsable(ChangeSink changed) {
    if (!Extended::Loaded())
        return 0;
    int n = 0;
    for (int slot0 = Extended::kFirstSlot0; slot0 < Extended::kEndSlot0; ++slot0) {
        State &s = At(slot0);
        bool noMana = false;
        const bool usable = Usable(Extended::Get(slot0), s.count, &noMana);
        if (usable == s.usable && noMana == s.noMana)
            continue;
        s.usable = usable;
        s.noMana = noMana;
        changed(slot0, usable, noMana);
        ++n;
    }
    return n;
}

// --- Word tests -----------------------------------------------------------------

bool Usable(uint32_t word, uint32_t count, bool *noMana) {
    *noMana = false;
    // EquipmentSet::Action's co-hook answers a set word ahead of the engine body.
    if (const uint32_t setID = EquipmentSet::Action::WordSetID(word))
        return EquipmentSet::Action::Usable(setID);
    if (Unit::Identity::PlayerObject() == nullptr)
        return false;
    if (const uint32_t item = ItemOf(word)) {
        if (count == 0)
            return false;
        if (reinterpret_cast<ItemSpellHeld_t>(Offsets::FUN_ITEM_SPELL_HELD)(item) != 0)
            return false;
    }
    uint32_t book = 0;
    const uint32_t spell = Extended::SpellFor(word, &book);
    if (spell == 0) {
        if (Game::Read<uint32_t>(Offsets::VAR_PLAYER_HAS_CONTROL) == 0)
            return false;
        // The engine's attack / ranged-repeat tests come next, but both
        // resolve this same spell, which is 0, so they answer false.
        if (ItemOf(word) != 0 || Extended::LiveMacroID(word) != 0)
            return true;
        // ActionSlotAuraActive, last: false for a word with no spell.
        return false;
    }
    if (static_cast<int>(spell) < 0)
        return false;
    const uint8_t *record = ::Spell::Lookup::RecordForID(static_cast<int>(spell));
    if (record == nullptr)
        return false;
    if (book == 0) {
        int out = 0;
        const bool usable =
            (reinterpret_cast<SpellIsUsable_t>(Offsets::FUN_SPELL_IS_USABLE)(record, &out) & 0xFF) != 0;
        *noMana = out != 0;
        return usable;
    }
    return ::Spell::Usable::PetSpell(static_cast<int>(spell), noMana);
}

bool IsAttack(uint32_t word) {
    uint32_t book = 0;
    const uint8_t *record = SpellRecord(word, &book);
    return record != nullptr &&
           Game::Read<int32_t>(record, Offsets::OFF_SPELL_RECORD_EFFECT) == Offsets::SPELL_EFFECT_ATTACK;
}

bool IsRangedRepeat(uint32_t word) {
    uint32_t book = 0;
    const uint8_t *record = SpellRecord(word, &book);
    return record != nullptr &&
           (Game::Read<uint32_t>(record, Offsets::OFF_SPELL_RECORD_ATTRIBUTES) &
            Offsets::SPELL_ATTR_RANGED) != 0 &&
           (Game::Read<uint32_t>(record, Offsets::OFF_SPELL_RECORD_ATTRIBUTES_EX2) &
            Offsets::SPELL_ATTR_EX2_AUTOREPEAT_FLAG) != 0;
}

bool IsAutoRepeat(uint32_t word) {
    // The engine starts the book at the slot number; the resolver writes it
    // for every word that has a spell.
    uint32_t book = 1;
    const uint32_t spell = Extended::SpellFor(word, &book);
    return spell != 0 && book == 0 &&
           spell == Game::Read<uint32_t>(Offsets::VAR_ACTIVE_AUTO_REPEAT_SPELL);
}

bool IsCurrent(uint32_t word) {
    if (word == 0)
        return false;
    if (const uint32_t item = ItemOf(word)) {
        const void *castItem = ::Object::ByGuid(Offsets::TYPEMASK_ITEM,
                                                Game::Read<uint64_t>(Offsets::VAR_CAST_ITEM_GUID));
        return castItem != nullptr && ::Object::Entry(castItem) == item;
    }
    if (IsAttack(word)) {
        const uint8_t *player = Unit::Identity::PlayerObject();
        return player != nullptr && Game::Read<uint64_t>(player, Offsets::OFF_UNIT_ATTACK_TARGET) != 0;
    }
    uint32_t book = 1;
    const uint8_t *record = SpellRecord(word, &book);
    if (record == nullptr || book != 0)
        return false;
    const uint32_t id = Game::Read<uint32_t>(record, 0);
    if (id == Game::Read<uint32_t>(Offsets::VAR_CURRENT_CAST_SPELL) ||
        id == reinterpret_cast<TargetingSpell_t>(Offsets::FUN_TARGETING_SPELL_ID)())
        return true;
    const uint8_t *fields = Unit::Identity::PlayerDescriptor();
    if (fields != nullptr &&
        Game::Read<int32_t>(record, Offsets::OFF_SPELL_RECORD_EFFECT) == Offsets::SPELL_EFFECT_TRADE_SKILL) {
        const uint32_t misc = Game::Read<uint32_t>(record, Offsets::OFF_SPELL_RECORD_EFFECT_MISC_VALUE);
        if (misc == 0) {
            const uint8_t *ability = ::Spell::SkillLine::AbilityForUnit(fields, id);
            if (ability != nullptr && Game::Read<uint32_t>(Offsets::VAR_CURRENT_TRADESKILL_LINE) ==
                                          Game::Read<uint32_t>(ability, Offsets::OFF_SLA_SKILL_ID))
                return true;
        } else if (Game::Read<uint32_t>(Offsets::VAR_CRAFT_WINDOW_STATE) == misc) {
            return true;
        }
    }
    // The first effect applying SPELL_AURA_MOD_SHAPESHIFT decides: current
    // while its misc value is the player's form (byte 2 of UNIT_FIELD_BYTES_1).
    for (int i = 0; i < 3; ++i) {
        if (Game::Read<int32_t>(record, Offsets::OFF_SPELL_RECORD_EFFECT_APPLY_AURA_NAME + i * 4) !=
            Offsets::SPELL_AURA_MOD_SHAPESHIFT)
            continue;
        const uint32_t form =
            Game::Read<uint32_t>(record, Offsets::OFF_SPELL_RECORD_EFFECT_MISC_VALUE + i * 4);
        if (form == 0 || fields == nullptr)
            return false;
        return ((Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_BYTES_1) >> 16) & 0xFF) == form;
    }
    return false;
}

bool IsEquipped(uint32_t word) {
    const uint32_t item = ItemOf(word);
    if (item == 0 || Unit::Identity::PlayerObject() == nullptr)
        return false;
    return reinterpret_cast<FindItem_t>(Offsets::FUN_INVMGR_FIND_ITEM_BY_ID)(
               Unit::Identity::PlayerInventoryManager(), item, Offsets::INVMGR_SEARCH_EQUIPMENT) !=
           nullptr;
}

bool HasRange(uint32_t word) {
    uint32_t book = 1;
    const uint8_t *record = SpellRecord(word, &book);
    // Spell::Range::SpellIDHasRange is the same FUN_GET_MIN_MAX_RANGE call with
    // the active player and the same 2^-22 floor.
    return record != nullptr && book == 0 &&
           ::Spell::Range::SpellIDHasRange(Game::Read<int>(record, 0));
}

bool IsConsumableItem(uint32_t itemID) {
    const uint8_t *record = Item::PeekRecord(itemID);
    if (record == nullptr)
        return false;
    const uint32_t invType = Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);
    if (invType == Offsets::INVTYPE_AMMO || invType == Offsets::INVTYPE_THROWN)
        return true;
    for (int i = 0; i < Offsets::ITEMSTATS_SPELL_SLOT_COUNT; ++i) {
        if (Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_SPELL_ID + i * 4) != 0 &&
            Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_SPELL_TRIGGER + i * 4) ==
                Offsets::ITEM_SPELLTRIGGER_ON_USE &&
            Game::Read<int32_t>(record, Offsets::OFF_ITEMSTATS_SPELL_CHARGES + i * 4) < 0)
            return true;
    }
    return false;
}

Cooldown CooldownFor(uint32_t word) {
    Cooldown cd;
    if (word == 0)
        return cd;
    if (const uint32_t item = ItemOf(word)) {
        uint32_t enable = 0;
        Item::Cooldown::Query(static_cast<int>(item), &cd.startMs, &cd.durationMs, &enable);
        cd.enabled = enable != 0;
        return cd;
    }
    uint32_t book = 0;
    const uint32_t spell = Extended::SpellFor(word, &book);
    if (spell == 0)
        return cd;
    if (::Spell::Cooldown::Query(static_cast<int>(spell), book != 0, &cd.startMs, &cd.durationMs,
                                 &cd.enabled)) {
        cd.spellID = static_cast<int>(spell);
        cd.pet = book != 0;
    }
    return cd;
}

} // namespace Action::ExtendedState
