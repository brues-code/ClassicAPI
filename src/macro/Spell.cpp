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

// `GetMacroSpell(macroSlot)` — modern (2.4+) reader for the spell a
// macro's first `/cast` (or `/castsequence`, or `CastSpellByName(...)`)
// directive resolves to. Returns `(name, rank, spellID)`, or nothing
// when the macro doesn't contain a cast.
//
// No parsing required — the vanilla engine already walks every macro
// body at create / edit / refresh time via the parser at
// `FUN_MACRO_PARSE_PRIMARY_SPELL` (`0x004EFE00`) and caches the
// resolved spellID on the macro struct at
// `+OFF_MACRO_PRIMARY_SPELL` (`+0x564`). Sentinel values:
//   - `0`          — macro contains no recognizable cast directive
//   - `0xFFFFFFFF` — directive matched but the spell name didn't
//                    resolve to the player's spellbook (unknown spell)
//   - any other    — the resolved spellID
//
// We read that cache and translate to the modern 3-tuple, going
// through `Spell::Lookup::RecordForID` and the locale-indexed name /
// rank string arrays in `Spell.dbc` (same path `GameTooltip:GetSpell`
// uses to render its title block).
//
// `Spell::CastNoToggle` hooks the same parser to register our
// `CastSpellNoToggle("<name>")` Lua helper as a cast pattern, so
// macros built around that function show up here too — symmetry the
// engine's parser doesn't natively provide.

#include "Game.h"
#include "Offsets.h"
#include "spell/Lookup.h"

#include <cstdint>

namespace Macro::Spell {

namespace {


using MacroSlotToEntry_t = const uint8_t *(__fastcall *)(unsigned slot0Based);

const uint8_t *EntryForSlot(int slot1Based) {
    if (slot1Based < 1 || slot1Based > Offsets::MACRO_SLOT_MAP_COUNT)
        return nullptr;
    auto fn = reinterpret_cast<MacroSlotToEntry_t>(
        Offsets::FUN_MACRO_SLOT_TO_ENTRY);
    return fn(static_cast<unsigned>(slot1Based - 1));
}

// `GetMacroSpell(macroSlot)` — three returns: (name, rank, spellID).
// Returns nothing when the slot is empty, the macro has no cast
// directive, or the directive's name didn't resolve to a known spell.
int __fastcall Script_GetMacroSpell(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetMacroSpell(macroSlot)");
        return 0;
    }
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const uint8_t *entry = EntryForSlot(slot);
    if (entry == nullptr)
        return 0;

    const uint32_t cached = *reinterpret_cast<const uint32_t *>(
        entry + Offsets::OFF_MACRO_PRIMARY_SPELL);
    if (cached == 0 || cached == 0xFFFFFFFFu)
        return 0;

    const int spellID = static_cast<int>(cached);
    const uint8_t *record = ::Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return 0;

    const int locale = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_LOCALE_INDEX));
    const char *name = *reinterpret_cast<const char *const *>(
        record + Offsets::OFF_SPELL_NAMES + locale * 4);
    if (name == nullptr)
        return 0;
    const char *rank = *reinterpret_cast<const char *const *>(
        record + Offsets::OFF_SPELL_RECORD_RANK + locale * 4);

    Game::Lua::PushString(L, name);
    // Rank slot is empty for most non-class spells (racials, procs,
    // etc.). Surface that as "" so callers can `.. " (" .. rank .. ")"`
    // without nil-handling — matches modern behavior.
    Game::Lua::PushString(L, rank != nullptr ? rank : "");
    Game::Lua::PushNumber(L, static_cast<double>(spellID));
    return 3;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("macroSlot", "luaIndex"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("name", "string", nullptr,
                   "Nil, with the rest, when the macro shows no spell you know."),
    Game::Doc::Opt("rank", "string", nullptr, "\"\" for a spell without a rank."),
    Game::Doc::Opt("spellID", "number"),
};
const Game::Doc::Function kGetMacroSpell{
    "Returns the spell a macro shows, from its #showtooltip or #show line or else its first "
    "cast line.",
    kArgs, kRets, "MacroGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetMacroSpell", &Script_GetMacroSpell, &kGetMacroSpell);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Macro::Spell
