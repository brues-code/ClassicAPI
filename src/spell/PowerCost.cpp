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

// `C_Spell.GetSpellPowerCost(spellIdentifier)` -> SpellPowerCostInfo[]
//
// Returns an array of power-cost tables, or nil if the spell isn't found
// or has no resource cost. Vanilla spells have exactly one power cost, so
// the array always holds at most one entry.
//
// `cost` is the **effective** cost for the local player — base + level
// scaling + ManaCostPercent-of-resource + the descriptor power-cost
// modifiers + the cost SpellMod (op 14) — via the engine's own cost
// helper `FUN_006e31b0` (the same value the engine charges). So talents
// like reduced-cost spells are reflected. Per-entry fields:
//
//   type            Enum.PowerType (0 mana, 1 rage, 2 focus, 3 energy,
//                   4 happiness) — the spell's PowerType (+0x7C)
//   name            power token ("MANA", "RAGE", …)
//   cost            effective cost (see above)
//   minCost         == cost; vanilla has no "optional" cost component
//   costPercent     ManaCostPercent (+0x270) — the spell's %-of-base-
//                   resource cost, or 0 for a flat cost
//   costPerSec      0 — vanilla doesn't expose a per-second channel cost
//                   in these terms
//   requiredAuraID  0 — vanilla has no form/aura-conditional costs here
//   hasRequiredAura false

#include "Game.h"
#include "Offsets.h"
#include "spell/Arg.h"
#include "spell/Lookup.h"
#include "unit/Power.h"

#include <cstdint>

namespace Spell::PowerCost {

namespace {

using GetSpellCost_t = uint32_t(__fastcall *)(int spellID, int unit);


uint32_t EffectiveCost(int spellID) {
    return reinterpret_cast<GetSpellCost_t>(Offsets::FUN_GET_SPELL_COST)(spellID, 0);
}

int __fastcall Script_GetSpellPowerCost(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    const uint8_t *rec = Spell::Lookup::RecordForID(spellID);
    if (rec == nullptr)
        return 0; // nil — spell not found

    const uint32_t cost = EffectiveCost(spellID);
    if (cost == 0xFFFFFFFF)
        return 0; // engine error (no player context, etc.)

    const int powerType = Game::Read<int>(rec, Offsets::OFF_SPELL_RECORD_POWER_TYPE);
    const int costPercent = Game::Read<int>(rec, Offsets::OFF_SPELL_RECORD_MANA_COST_PERCENT);
    if (cost == 0 && costPercent == 0)
        return 0; // nil — no resource cost

    // SpellPowerCostInfo[] — a one-element array: outer[1] = entry.
    Game::Lua::NewTable(L);                                  // outer
    Game::Lua::PushNumber(L, 1.0);                           // key 1
    Game::Lua::NewTable(L);                                  // entry (top)
    Game::Lua::SetFieldNumber(L, "type", static_cast<double>(powerType));
    Game::Lua::SetFieldString(L, "name", Unit::Power::PowerTypeToken(powerType));
    Game::Lua::SetFieldNumber(L, "cost", static_cast<double>(cost));
    Game::Lua::SetFieldNumber(L, "minCost", static_cast<double>(cost));
    Game::Lua::SetFieldNumber(L, "costPercent", static_cast<double>(costPercent));
    Game::Lua::SetFieldNumber(L, "costPerSec", 0);
    Game::Lua::SetFieldNumber(L, "requiredAuraID", 0);
    Game::Lua::SetFieldBool(L, "hasRequiredAura", false);
    Game::Lua::SetTable(L, -3);                              // outer[1] = entry
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kSpellPowerCostInfoFields[] = {
    Game::Doc::Req("type", "number",
                   "The power the spell spends: 0 mana, 1 rage, 2 focus, 3 energy, "
                   "4 happiness."),
    Game::Doc::Req("name", "string", "The power token, such as \"MANA\" or \"RAGE\"."),
    Game::Doc::Req("cost", "number", "What the cast costs the player now."),
    Game::Doc::Req("minCost", "number", "The same value as cost."),
    Game::Doc::Req("costPercent", "number",
                   "The share of the base power pool the cast takes, or 0 for a flat cost."),
    Game::Doc::Req("costPerSec", "number", "Always 0."),
    Game::Doc::Req("requiredAuraID", "number", "Always 0."),
    Game::Doc::Req("hasRequiredAura", "bool", "Always false."),
};
const Game::Doc::Structure kSpellPowerCostInfo{
    "SpellPowerCostInfo", "Spell", kSpellPowerCostInfoFields,
    "What one cast of a spell costs."};

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("spell", "SpellIdentifier", "A spell ID, spell link, or spell name."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("costs", "table", nullptr,
                   "An array of SpellPowerCostInfo entries; nil when the spell costs "
                   "nothing or is unknown."),
};
const Game::Doc::Function kGetSpellPowerCost{
    "What one cast of the spell costs the player in power.", kArgs, kRets};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellPowerCost", &Script_GetSpellPowerCost,
                                     &kGetSpellPowerCost);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::PowerCost
