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

// `UnitSpellHaste(unit)` — backport of the TBC+ spell-haste getter to 1.12,
// which has no native haste API. It reads UNIT_MOD_CAST_SPEED (the cast-time
// multiplier at descriptor +0x22c that the server folds into
// `SpellEntry::GetCastTime` — `castTime *= GetFloatValue(UNIT_MOD_CAST_SPEED)`)
// and converts it to the modern haste PERCENTAGE:
//
//   haste% = (1 / modCastSpeed - 1) * 100
//
// so `1.0` (no modifier) -> `0`, `0.5` (half cast time) -> `100`, and a
// slowing effect like Curse of Tongues (`modCastSpeed > 1.0`) -> a negative
// value. This is the same field nampower surfaces raw as `modCastSpeed`;
// `UnitSpellHaste` is the Blizzard-shaped percentage over it, so addons can
// drop the `GetUnitField("player", "modCastSpeed")` nampower dependency.
// (Consumers that need the raw multiplier can recover it as
// `1 / (1 + UnitSpellHaste(unit) / 100)`.)
//
// Vanilla has very few caster-haste sources, so this reads `0` for most
// casters most of the time — the value is meaningful for Curse of Tongues,
// Nature's Grace, and the handful of other cast-speed effects (and, as the
// cast-time work showed, whatever bleeds into the field for hunter shots).
//
// `UNIT_SPELL_HASTE` fires when that field changes. 1.12 turns unit-field
// changes into events by index: FUN_0051bbb0 watches unit field i when slot
// i of the static event-name array is named, and FUN_0051bd50 signals event i
// once per token of the unit. UNIT_MOD_CAST_SPEED is index 139
// (OFF_UNIT_MOD_CAST_SPEED / 4) and slot 139 has no name, so naming it lets
// the engine's own watchers fire the event (see Event::Custom::EngineName).
// Field 139 is one of the few the unit's watch copy has no slot for, so the
// name also gives it the watched-field table's spare entry; without it, each
// watcher wrote the field over its object (FUN_UNIT_WATCHED_INDEX). Nothing
// else signals 139 by number: it is above the glue band and not in the
// chat-type event table.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"

#include <cstdint>

namespace Unit::SpellHaste {

namespace {

const Game::Doc::Field kEventPayload[] = {
    Game::Doc::Req("unitTarget", "UnitToken"),
};
const Game::Doc::Event kEventDoc{"Unit", "Fires when the unit's spell haste changes.",
                                 kEventPayload};
const Event::Custom::EngineName _event{Offsets::OFF_UNIT_MOD_CAST_SPEED / 4, "UNIT_SPELL_HASTE",
                                       &kEventDoc};

const uint8_t *Descriptor(const uint8_t *unit) {
    if (unit == nullptr)
        return nullptr;
    return *reinterpret_cast<const uint8_t *const *>(
        unit + Offsets::OFF_CGUNIT_OBJECT_FIELDS);
}

const uint8_t *ResolveUnit(const char *token) {
    if (token == nullptr)
        return nullptr;
    return static_cast<const uint8_t *>(Game::ResolveUnitToken(token));
}

// `UnitSpellHaste("unit")` — spell haste percentage. `0` for an unhasted or
// invalid unit; negative for a slowed one (Curse of Tongues).
static int __fastcall Script_UnitSpellHaste(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitSpellHaste(\"unit\")");
        return 0;
    }
    const uint8_t *desc = Descriptor(ResolveUnit(Game::Lua::ToString(L, 1)));
    if (desc == nullptr) {
        Game::Lua::PushNumber(L, 0.0);
        return 1;
    }
    const float mult = *reinterpret_cast<const float *>(
        desc + Offsets::OFF_UNIT_MOD_CAST_SPEED);
    // `mult` is the cast-time multiplier; haste% is its inverse deviation from
    // 1.0. Guard a non-positive / uninitialized field against div-by-zero
    // (treat it as "no haste").
    const double haste = (mult > 0.0f)
                             ? (1.0 / static_cast<double>(mult) - 1.0) * 100.0
                             : 0.0;
    Game::Lua::PushNumber(L, haste);
    return 1;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitSpellHaste", &Script_UnitSpellHaste);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::SpellHaste
