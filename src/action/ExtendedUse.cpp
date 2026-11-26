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

// UseActionSlot (0x004E5EE0) for Action::Extended's slots, in the engine's
// order (disassembled; benilla-re ui/actionuse#using-an-action):
//
//   - checkCursor and a payload Place would take → place it.
//   - Empty → nothing.
//   - Item (0x004E5F73..0x004E607B), active player required: a template with
//     an inventory type and no copy in the equipment slots
//     (FUN_INVMGR_FIND_ITEM_BY_ID, flags INVMGR_SEARCH_EQUIPMENT) is found in
//     equipment, bags and keyring (FUN_INVMGR_FIND_ITEM_SLOT_BY_ID, flags 0)
//     and auto-equipped (FUN_PLAYER_AUTO_EQUIP_ITEM_IN_SLOT); otherwise the
//     first copy — skipping copies with no charges (flag 0x20) when the
//     template's first charges are neither 0 nor -1 — is used (FUN_ITEM_USE,
//     no target). An unknown template is used too: it has no inventory type.
//   - Macro → FUN_MACRO_RUN_BY_ID.
//   - Spell s (the word itself, book 0): its active icon is up → cancel the
//     aura (FUN_CANCEL_AURA_SEND); s is auto-repeating → stop it
//     (FUN_STOP_AUTO_REPEAT_SPELL); s awaits a target → the engine calls
//     FUN_STOP_PLACEMENT at CALL_USE_ACTION_STOP_PLACEMENT, which
//     GamePad::Cursor redirects to TakeBackOrPlace, so that is called here;
//     else FUN_CAST_SPELL_AS_ACTIVE_PLAYER(s, no item, the player when onSelf).
//
// nampower detours UseActionSlot, which these slots never reach; the cast
// still goes through its detoured TryCastSpell.

#include "action/ExtendedUse.h"

#include "Game.h"
#include "Offsets.h"
#include "action/Extended.h"
#include "action/ExtendedDrag.h"
#include "gamepad/Cursor.h"
#include "item/Record.h"
#include "item/Use.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Action::ExtendedUse {

namespace {

using FindItem_t = const uint8_t *(__thiscall *)(const void *invMgr, uint32_t itemID, uint32_t flags);
using FindItemSlot_t = const uint8_t *(__thiscall *)(const void *invMgr, uint32_t itemID,
                                                     uint64_t *outContainerGuid, uint32_t *outSlot,
                                                     uint32_t flags);
using AutoEquip_t = void(__thiscall *)(const void *player, uint32_t containerGuidLo,
                                       uint32_t containerGuidHi, uint32_t slot, int flag);
using RunMacro_t = void(__fastcall *)(uint32_t macroID);
using CancelAura_t = void(__fastcall *)(uint32_t spellID);
using StopAutoRepeat_t = void(__cdecl *)();
using TargetingSpell_t = uint32_t(__cdecl *)();
using CastSpell_t = uint8_t(__fastcall *)(uint32_t spellID, const void *item, uint32_t targetLo,
                                          uint32_t targetHi);

// The charges test that makes the search skip empty copies (0x004E604F).
constexpr uint32_t kSearchSkipNoCharges = 0x20;

void UseItem(uint32_t itemID) {
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return;
    const uint8_t *invMgr = player + Offsets::OFF_PLAYER_INVENTORY_MANAGER;
    const uint8_t *record = Item::PeekRecord(itemID);
    const auto find = reinterpret_cast<FindItem_t>(Offsets::FUN_INVMGR_FIND_ITEM_BY_ID);

    if (record != nullptr &&
        Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE) != 0 &&
        find(invMgr, itemID, Offsets::INVMGR_SEARCH_EQUIPMENT) == nullptr) {
        uint64_t container = 0;
        uint32_t slot = 0;
        if (reinterpret_cast<FindItemSlot_t>(Offsets::FUN_INVMGR_FIND_ITEM_SLOT_BY_ID)(
                invMgr, itemID, &container, &slot, 0) == nullptr)
            return;
        reinterpret_cast<AutoEquip_t>(Offsets::FUN_PLAYER_AUTO_EQUIP_ITEM_IN_SLOT)(
            player, static_cast<uint32_t>(container), static_cast<uint32_t>(container >> 32), slot,
            0);
        return;
    }

    uint32_t flags = 0;
    if (record != nullptr) {
        const int32_t charges = Game::Read<int32_t>(record, Offsets::OFF_ITEMSTATS_SPELL_CHARGES);
        if (charges != -1 && charges != 0)
            flags = kSearchSkipNoCharges;
    }
    if (const uint8_t *item = find(invMgr, itemID, flags))
        Item::Use::UseItem(item, 0);
}

void UseSpell(uint32_t word, bool onSelf) {
    uint32_t book = 0;
    const uint32_t spell = Extended::SpellFor(word, &book);
    if (Extended::AuraActive(spell, book)) {
        reinterpret_cast<CancelAura_t>(Offsets::FUN_CANCEL_AURA_SEND)(spell);
        return;
    }
    if (spell == Game::Read<uint32_t>(Offsets::VAR_ACTIVE_AUTO_REPEAT_SPELL)) {
        reinterpret_cast<StopAutoRepeat_t>(Offsets::FUN_STOP_AUTO_REPEAT_SPELL)();
        return;
    }
    if (spell == reinterpret_cast<TargetingSpell_t>(Offsets::FUN_TARGETING_SPELL_ID)()) {
        GamePad::Cursor::TakeBackOrPlace();
        return;
    }
    const uint64_t target = onSelf ? Unit::Identity::PlayerGuid() : 0;
    reinterpret_cast<CastSpell_t>(Offsets::FUN_CAST_SPELL_AS_ACTIVE_PLAYER)(
        spell, nullptr, static_cast<uint32_t>(target), static_cast<uint32_t>(target >> 32));
}

} // namespace

void Use(int slot0, bool checkCursor, bool onSelf) {
    if (!Extended::Owns(slot0))
        return;
    if (checkCursor && ExtendedDrag::CursorWouldPlace())
        return ExtendedDrag::Place(slot0);
    const uint32_t word = Extended::Get(slot0);
    if (word == 0)
        return;
    const uint32_t tag = word & Offsets::ACTION_TYPE_MASK;
    if (tag == Offsets::ACTION_TYPE_ITEM_BY_ID)
        return UseItem(word & Offsets::ACTION_PAYLOAD_MASK_ITEM_BY_ID);
    if (tag == Offsets::ACTION_TYPE_BAG_OR_MACRO) {
        reinterpret_cast<RunMacro_t>(Offsets::FUN_MACRO_RUN_BY_ID)(
            word & Offsets::ACTION_PAYLOAD_MASK_BAG_OR_MACRO);
        return;
    }
    UseSpell(word, onSelf);
}

} // namespace Action::ExtendedUse
