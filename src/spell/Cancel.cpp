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

// `C_Spell.CancelSpellByID(spellID)` and `CancelSpellByName(name)` —
// modern aura-cancel primitives. Both reduce to a single direct call
// to the engine's CMSG_CANCEL_AURA sender at `FUN_CANCEL_AURA_SEND`
// (`__fastcall(spellID)`), skipping `Script_CancelPlayerBuff`'s
// client-side cancelable-flag gate. The server is the source of
// truth on what's actually cancelable — non-cancelable auras are
// rejected server-side as no-ops.
//
// Vanilla server only accepts cancel for the local player's own
// auras, so there's no `unit` parameter — matches what these modern
// functions actually do.

#include "Lookup.h"

#include "Game.h"
#include "Offsets.h"
#include "aura/Data.h"
#include "dbc/Lookup.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstring>

namespace Spell::Cancel {

namespace {

using CancelAuraSend_t = void(__fastcall *)(int spellID);

// Pre-validates `spellID` against Spell.dbc and ships the cancel
// packet. The engine's sender unconditionally derefs `[record+0x1C]`
// after a NULL-on-OOR record lookup, so skipping the
// `RecordForID != nullptr` check would AV on bad input.
void SendCancel(int spellID) {
    if (Spell::Lookup::RecordForID(spellID) == nullptr)
        return;
    auto fn = reinterpret_cast<CancelAuraSend_t>(
        static_cast<uintptr_t>(Offsets::FUN_CANCEL_AURA_SEND));
    fn(spellID);
}

int __fastcall Script_CancelSpellByID(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0;
    SendCancel(static_cast<int>(Game::Lua::ToNumber(L, 1)));
    return 0;
}

int __fastcall Script_CancelSpellByName(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return 0;
    const char *targetName = Game::Lua::ToString(L, 1);
    if (targetName == nullptr || *targetName == '\0')
        return 0;
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return 0;
    // Walk the player's buff range only (slots 0..31). Modern
    // CancelSpellByName matches against helpful auras; debuffs go
    // through dispel UX, not a Lua-side cancel.
    for (int slot = 0; slot < Offsets::UNIT_AURA_BUFF_COUNT; ++slot) {
        if (!Aura::Data::IsSlotPopulated(player, slot))
            continue;
        const uint32_t spellID = Aura::Data::ReadSpellID(player, slot);
        const char *name = DBC::LocalizedField(
            Offsets::VAR_SPELL_RECORDS, Offsets::VAR_SPELL_RECORD_COUNT,
            spellID, Offsets::OFF_SPELL_NAMES);
        if (name != nullptr && _stricmp(name, targetName) == 0) {
            SendCancel(static_cast<int>(spellID));
            return 0;
        }
    }
    return 0;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kByIDArgs[] = {
    Game::Doc::Req("spellID", "number", "The buff to cancel."),
};
const Game::Doc::Function kCancelSpellByID{
    "Cancels one of your own buffs by spell ID.", kByIDArgs, {}};

const Game::Doc::Field kByNameArgs[] = {
    Game::Doc::Req("name", "string",
                   "The buff's name, matched without regard to case; a \"(Rank N)\" "
                   "suffix is not read."),
};
const Game::Doc::Function kCancelSpellByName{
    "Cancels the first of your own buffs whose name matches.",
    kByNameArgs, {}, "SpellGlobals"};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "CancelSpellByID",
                                     &Script_CancelSpellByID, &kCancelSpellByID);
    Game::Lua::RegisterGlobalFunction("CancelSpellByName",
                                      &Script_CancelSpellByName, &kCancelSpellByName);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::Cancel
