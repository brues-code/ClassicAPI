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

// `C_Spell.IsSpellDataCached(spellIdentifier)` and
// `C_Spell.RequestLoadSpellData(spellIdentifier)` + `SPELL_DATA_LOAD_RESULT`
// — the spell half of retail's async data-load trio (item and quest are in
// `item/Data.cpp` / `quest/Data.cpp`), which `SpellMixin` and the
// `SpellEventListener` in the embedded addon's AsyncCallbackSystem drive.
//
// Retail streams spell records on demand. This client keeps all of
// Spell.dbc resident, so a spell's data is "cached" exactly when its record
// exists, and a load request resolves at once. Retail marks
// SPELL_DATA_LOAD_RESULT a SynchronousEvent and AsyncCallbackSystem relies
// on that (a cached request fires and clears the callbacks before
// `AddCancelableCallback` returns), so the request fires the event inside
// the call: `(spellID, true)` for a real record, `(spellID, nil)` for an ID
// with none, so a listener's pending callbacks are cleared rather than kept
// forever. An identifier that resolves to no spell ID fires nothing.

#include "Arg.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "event/Custom.h"

#include <cstdint>

namespace Spell::Data {

namespace {

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kSpellArgs[] = {
    Game::Doc::Req("spellIdentifier", "SpellIdentifier", "A spell ID, spell link, or spell name."),
};
const Game::Doc::Field kIsCachedRets[] = {
    Game::Doc::Req("isCached", "bool", "True when the client holds data for the spell."),
};
const Game::Doc::Function kIsSpellDataCached{
    "Whether the data for a spell is loaded and ready to read.", kSpellArgs, kIsCachedRets};
const Game::Doc::Function kRequestLoadSpellData{
    "Asks for a spell's data to be loaded; SPELL_DATA_LOAD_RESULT reports when it is ready.",
    kSpellArgs, {}};

const Game::Doc::Field kLoadResultPayload[] = {
    Game::Doc::Req("spellID", "number"),
    Game::Doc::Req("success", "bool"),
};
const Game::Doc::Event kLoadResultDoc{
    "Spell", "A spell data request finished; success is false when no such spell exists.",
    kLoadResultPayload};
const Event::Custom::AutoReserve _reserveLoadResult{"SPELL_DATA_LOAD_RESULT", &kLoadResultDoc};

bool HasRecord(int spellID) {
    return DBC::Record(Offsets::VAR_SPELL_RECORDS, Offsets::VAR_SPELL_RECORD_COUNT,
                       static_cast<uint32_t>(spellID)) != nullptr;
}

} // namespace

static int __fastcall Script_IsSpellDataCached(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    Game::Lua::PushBool(L, spellID > 0 && HasRecord(spellID));
    return 1;
}

static int __fastcall Script_RequestLoadSpellData(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    if (spellID > 0)
        Event::Custom::FireIdSuccess(_reserveLoadResult.Slot(), spellID, HasRecord(spellID));
    return 0;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "IsSpellDataCached",
                                     &Script_IsSpellDataCached, &kIsSpellDataCached);
    Game::Lua::RegisterTableFunction("C_Spell", "RequestLoadSpellData",
                                     &Script_RequestLoadSpellData, &kRequestLoadSpellData);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::Data
