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

// `UnitHealthMissing(unit)` — the health deficit, i.e. the equivalent of
// `UnitHealthMax(unit) - UnitHealth(unit)`. A convenience for healing addons
// (overheal checks, "missing health" bars) that otherwise call both engine
// functions and subtract in Lua every frame. Reads the two health fields off
// the unit's descriptor directly (same path `Unit::Power` uses for POWER),
// so it's a single resolve + two field reads.
//
// Health carries no display divisor — unlike rage/happiness, HEALTH and
// MAXHEALTH are stored at face value — so the deficit is a plain subtraction,
// clamped at 0 (a transient current > max should never surface as negative).

#include "Game.h"
#include "Offsets.h"
#include "group/MemberStats.h"

#include <cstdint>

namespace Unit::Health {

namespace {

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

// `UnitHealthMissing("unit")` — returns `max - current` health (never
// negative). Returns 0 for a valid-but-absent unit (e.g. `"target"` with no
// target), matching `UnitHealth`'s 0-for-missing convention.
static int __fastcall Script_UnitHealthMissing(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitHealthMissing(\"unit\")");
        return 0;
    }
    const uint8_t *desc = Descriptor(ResolveUnit(Game::Lua::ToString(L, 1)));
    if (desc == nullptr) {
        // No live object → out-of-range group member; the group roster caches
        // HP (same fallback the engine's UnitHealth uses). Reached only after
        // FUN_RESOLVE_UNIT_TOKEN returned null on an already-valid token, so
        // TokenToGUID can't raise.
        auto tokenToGuid = reinterpret_cast<uint64_t(__fastcall *)(const char *)>(
            static_cast<uintptr_t>(Offsets::FUN_TOKEN_TO_GUID));
        const Group::MemberStats::Stats s =
            Group::MemberStats::Lookup(tokenToGuid(Game::Lua::ToString(L, 1)));
        const uint32_t missing =
            (s.valid && s.maxHealth > s.health) ? s.maxHealth - s.health : 0u;
        Game::Lua::PushNumber(L, static_cast<double>(missing));
        return 1;
    }
    const uint32_t cur = *reinterpret_cast<const uint32_t *>(
        desc + Offsets::OFF_UNIT_FIELD_HEALTH);
    const uint32_t max = *reinterpret_cast<const uint32_t *>(
        desc + Offsets::OFF_UNIT_FIELD_MAXHEALTH);
    Game::Lua::PushNumber(L, static_cast<double>(max > cur ? max - cur : 0u));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("unit", "UnitToken"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("result", "number",
                   "0 at full health and for a token with no unit; never negative."),
};
const Game::Doc::Function kUnitHealthMissing{
    "Returns how much health a unit is missing.", kArgs, kRets, "UnitGlobals"};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitHealthMissing", &Script_UnitHealthMissing,
                                      &kUnitHealthMissing);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::Health
