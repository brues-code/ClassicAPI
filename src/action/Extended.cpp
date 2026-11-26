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

// The store behind action slots 121..360 (see Extended.h).
//
// File: `ClassicAPI_ActionSlots.txt` in the character's WTF folder
// (Settings::Paths::CharacterFile), one `<slot> 0x<word>` line per filled
// slot, rewritten whole through Os::File::Write (temp file + replace) on every
// change. A line whose slot is outside 121..360, or whose word is empty or not
// a spell / macro / item word, is dropped on load.
//
// Loading. The path is resolved on the first read once the account, realm and
// character are known, and on world entry (Event::EnteringWorld). Lua can ask
// before that observer runs (PLAYER_LOGIN, the engine's own
// ACTIONBAR_SLOT_CHANGED(0) after it applies SMSG_ACTION_BUTTONS, PEW
// handlers), which is why every read resolves it too. The glue pass forgets
// it: the DLL is not reloaded across logout, and a character switch always
// passes through the glue screen, so the next character resolves its own file.

#include "action/Extended.h"

#include "Game.h"
#include "Offsets.h"
#include "action/Slot.h"
#include "aura/Data.h"
#include "event/Custom.h"
#include "event/EnteringWorld.h"
#include "item/Data.h"
#include "object/Resolve.h"
#include "os/File.h"
#include "settings/Paths.h"
#include "spell/Lookup.h"
#include "unit/Identity.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

namespace Action::Extended {

namespace {

constexpr const char *kLeaf = "ClassicAPI_ActionSlots.txt";
constexpr int kCount = kEndSlot0 - kFirstSlot0;

std::string g_path; // empty: not resolved for this character yet
uint32_t g_slots[kCount];

bool StorableWord(uint32_t word) {
    const uint32_t tag = word & Offsets::ACTION_TYPE_MASK;
    return word != 0 && (tag == Offsets::ACTION_TYPE_SPELL ||
                         tag == Offsets::ACTION_TYPE_BAG_OR_MACRO ||
                         tag == Offsets::ACTION_TYPE_ITEM_BY_ID);
}

void Load() {
    std::memset(g_slots, 0, sizeof g_slots);
    std::string text;
    if (!Os::File::Read(g_path, text))
        return; // no file yet: every slot empty
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        char *end = nullptr;
        const long slot = std::strtol(line.c_str(), &end, 10);
        const uint32_t word = static_cast<uint32_t>(std::strtoul(end, nullptr, 0));
        if (Owns(static_cast<int>(slot) - 1) && StorableWord(word))
            g_slots[slot - 1 - kFirstSlot0] = word;
    }
}

void Save() {
    std::string out = "# ClassicAPI action slots 121-360: <slot> <action word>\n";
    char line[32];
    for (int i = 0; i < kCount; ++i) {
        if (g_slots[i] == 0)
            continue;
        std::snprintf(line, sizeof line, "%d 0x%08X\n", kFirstSlot0 + i + 1,
                      static_cast<unsigned>(g_slots[i]));
        out += line;
    }
    Os::File::Write(g_path, out);
}

void Resolve() {
    if (!g_path.empty())
        return;
    g_path = Settings::Paths::CharacterFile(kLeaf);
    if (!g_path.empty())
        Load();
}

void Forget() {
    g_path.clear();
    std::memset(g_slots, 0, sizeof g_slots);
}

const Event::EnteringWorld::AutoObserve _enterWorld{&Resolve};
const Game::GlueModuleAutoRegister _glue{&Forget};

} // namespace

bool SlotArg(void *L, int idx, int *slot0) {
    if (!Game::Lua::IsNumber(L, idx))
        return false;
    const double v = Game::Lua::ToNumber(L, idx);
    if (!(v >= kFirstSlot0 + 1 && v < kEndSlot0 + 1))
        return false;
    *slot0 = static_cast<int>(v) - 1;
    return true;
}

bool Ready() {
    Resolve();
    return !g_path.empty();
}

uint32_t Get(int slot0) {
    if (!Owns(slot0) || !Ready())
        return 0;
    return g_slots[slot0 - kFirstSlot0];
}

void Set(int slot0, uint32_t word) {
    if (!Owns(slot0) || !Ready())
        return;
    g_slots[slot0 - kFirstSlot0] = word;
    Save();
    // FUN_ACTION_SLOT_CHANGED_NOTIFY's `quiet == 0` tail:
    // FUN_FIRE_EVENT(0xD5, "%d", slot0 + 1) (0x004E5998). Its usable / noMana
    // recompute and the CMSG_SET_ACTION_BUTTON branch are for its own 120
    // slots.
    Event::Custom::Fire(Offsets::EVENT_ACTIONBAR_SLOT_CHANGED, "%d", slot0 + 1);
}

uint32_t LiveMacroID(uint32_t word) {
    if (word == 0 || (word & Offsets::ACTION_TYPE_MASK) != Offsets::ACTION_TYPE_BAG_OR_MACRO)
        return 0;
    const uint32_t id = word & Offsets::ACTION_PAYLOAD_MASK_BAG_OR_MACRO;
    return Action::Slot::MacroSlotForID(id) != 0 ? id : 0;
}

uint32_t SpellFor(uint32_t word, uint32_t *book) {
    if (word == 0)
        return 0;
    const uint32_t tag = word & Offsets::ACTION_TYPE_MASK;
    if (tag == Offsets::ACTION_TYPE_SPELL) {
        *book = 0;
        return word;
    }
    if (const uint32_t macroID = LiveMacroID(word)) {
        // GetMacroSlotSpell (0x004E5BA0): the macro record's cached spell and book.
        using MacroIDToEntry_t = const uint8_t *(__fastcall *)(uint32_t macroID);
        const uint8_t *entry =
            reinterpret_cast<MacroIDToEntry_t>(Offsets::FUN_MACRO_ID_TO_ENTRY)(macroID);
        if (entry == nullptr)
            return 0;
        *book = Game::Read<uint32_t>(entry, Offsets::OFF_MACRO_PRIMARY_SPELL_IS_PET);
        return Game::Read<uint32_t>(entry, Offsets::OFF_MACRO_PRIMARY_SPELL);
    }
    if (tag == Offsets::ACTION_TYPE_ITEM_BY_ID) {
        // The engine asks the item cache here (GetRecord with its record
        // callback); Item::Data::RecordOrRequest is this project's one asking
        // path.
        *book = 0;
        const uint8_t *record =
            Item::Data::RecordOrRequest(word & Offsets::ACTION_PAYLOAD_MASK_ITEM_BY_ID);
        if (record == nullptr)
            return 0;
        for (int i = 0; i < Offsets::ITEMSTATS_SPELL_SLOT_COUNT; ++i) {
            const uint32_t spell = Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_SPELL_ID + i * 4);
            if (spell != 0 && Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_SPELL_TRIGGER + i * 4) ==
                                  Offsets::ITEM_SPELLTRIGGER_ON_USE)
                return spell;
        }
    }
    return 0;
}

bool AuraActive(uint32_t spellID, uint32_t book) {
    const uint8_t *record = ::Spell::Lookup::RecordForID(static_cast<int>(spellID));
    if (record == nullptr ||
        Game::Read<uint32_t>(record, Offsets::OFF_SPELL_RECORD_ACTIVE_ICON_ID) == 0)
        return false;
    // The engine resolves the active player (book 0) or the pet bar's unit
    // (VAR_PET_GUID) with TYPEMASK_UNIT and compares each aura slot with the
    // record's own id column, which is the spell id.
    const uint8_t *unit =
        book == 0 ? Unit::Identity::PlayerObject()
                  : static_cast<const uint8_t *>(::Object::ByGuid(
                        Offsets::TYPEMASK_UNIT, Game::Read<uint64_t>(Offsets::VAR_PET_GUID)));
    if (unit == nullptr)
        return false;
    for (int slot = 0; slot < Offsets::UNIT_AURA_TOTAL; ++slot) {
        if (Aura::Data::ReadSpellID(unit, slot) == spellID &&
            (Aura::Data::SlotNibble(unit, slot) & Offsets::UNIT_AURA_FLAG_CANCELABLE) != 0)
            return true;
    }
    return false;
}

} // namespace Action::Extended
