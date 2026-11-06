// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// Modern power-API polyfill: `UnitPower(unit [, type [, unmodified]])`,
// `UnitPowerMax(unit [, type [, unmodified]])`, `UnitPowerMissing(unit
// [, type [, unmodified]])`, and an extended `UnitPowerType(unit)` that
// returns the modern 2-tuple `(powerType, powerToken)`.
//
// Shape modeled on 3.3.5's `Script_UnitPower` (`0x0060ED40` in the
// Frostmourne client) and `Script_UnitPowerMax` (`0x0060EF40`). The
// 1.12 descriptor layout shifts the field offsets but the algorithm
// is the same: read POWER[type] / MAXPOWER[type] off the unit's
// descriptor, defaulting `type` to the unit's primary power (the
// `UNIT_FIELD_BYTES_0` byte 3 at `descriptor + 0x7B`), then divide
// by the engine's per-power-type display divisor (`DisplayDivisor`)
// before pushing. Rage internally stores 0..1000 but displays 0..100
// (divisor 10); happiness uses divisor 1000. Same divisor logic
// vanilla's own `Script_UnitMana` (`0x00517670`) applies.
//
// Vanilla 1.12 already has `UnitPowerType(unit)` returning just the
// integer; we override it via the same `RegisterGlobalFunction` path,
// chain to the engine's original (`0x00517940`) to keep its pet /
// totem fallbacks intact, then append the modern `powerToken` string
// as a second return. Strict-superset signature: addons doing
// `local pt = UnitPowerType("player")` still get the integer in the
// same position; modern addons destructuring `(pt, token)` get both.

#include "Game.h"
#include "Offsets.h"
#include "group/MemberStats.h"
#include "unit/Power.h"

#include <cstdint>

namespace Unit::Power {

// Modern power-type tokens. Vanilla descriptor byte values 0..4 map
// to MANA/RAGE/FOCUS/ENERGY/HAPPINESS. 5/6 (Runes / Runic Power)
// can't appear on a 1.12 unit but we map them anyway in case a
// private server pushes one through.
const char *PowerTypeToken(int type) {
    switch (type) {
    case 0: return "MANA";
    case 1: return "RAGE";
    case 2: return "FOCUS";
    case 3: return "ENERGY";
    case 4: return "HAPPINESS";
    case 5: return "RUNES";
    case 6: return "RUNIC_POWER";
    }
    return "UNKNOWN";
}

unsigned int DisplayDivisor(int type) {
    using Divisor_t = unsigned int(__fastcall *)(int powerType);
    return reinterpret_cast<Divisor_t>(
        static_cast<uintptr_t>(Offsets::FUN_POWER_DISPLAY_DIVISOR))(type);
}

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


// `Enum.PowerType` — modern Blizzard-style enum table. Slot 4 is
// `Happiness` in 1.12 (pet happiness 0..3); modern retail relabels
// slot 4 as `ComboPoints`. We use the vanilla name since this
// polyfill ships against the 1.12 engine — addons that need the
// modern label can `Enum.PowerType.Happiness` and add their own
// alias.
constexpr Game::Lua::EnumIntegerEntry kPowerTypeEntries[] = {
    {"HealthCost", -2}, // sentinel for "use HEALTH instead of POWER"
    {"None",       -1},
    {"Mana",        0},
    {"Rage",        1},
    {"Focus",       2},
    {"Energy",      3},
    {"Happiness",   4},
};

// Resolves the (unit, type) args to a `(descriptor, type)` pair.
// Returns true on success. `type` is normalized to 0..4 on return —
// if the caller omitted it (or passed -1 / 7 / any out-of-range
// sentinel), we substitute the unit's primary power type from its
// `UNIT_FIELD_BYTES_0` byte 3 (descriptor `+0x7B`).
//
// Returns false (and leaves out params untouched) if the unit token
// is missing/invalid, the unit isn't resolvable, or the resolved
// power type is post-WotLK (5/6) — vanilla 1.12 descriptors don't
// carry POWER fields for those slots.
bool ResolveArgs(void *L, const uint8_t **outDesc, int *outType) {
    if (!Game::Lua::IsString(L, 1))
        return false;
    const char *token = Game::Lua::ToString(L, 1);
    const uint8_t *unit = ResolveUnit(token);
    const uint8_t *desc = Descriptor(unit);
    if (desc == nullptr)
        return false;

    int type = -1;
    if (Game::Lua::IsNumber(L, 2))
        type = static_cast<int>(Game::Lua::ToNumber(L, 2));
    if (type < Offsets::UNIT_POWER_MIN_TYPE ||
        type > Offsets::UNIT_POWER_MAX_TYPE) {
        // Out of vanilla-valid range (or unspecified) → use the
        // unit's primary power type from descriptor +0x7B.
        type = *(desc + Offsets::OFF_UNIT_DESCRIPTOR_POWER_TYPE_BYTE);
    }
    if (type < Offsets::UNIT_POWER_MIN_TYPE ||
        type > Offsets::UNIT_POWER_MAX_TYPE)
        return false;
    *outDesc = desc;
    *outType = type;
    return true;
}

// Out-of-range group-member fallback. When a unit token has no live CGUnit —
// a party/raid member on a different map or otherwise out of range — the
// descriptor is gone, but the group roster still caches the member's PRIMARY
// power (`Group::MemberStats`, fed by SMSG_PARTY_MEMBER_STATS). This mirrors
// what the engine's Script_UnitMana / Script_UnitManaMax do. Reached only
// after ResolveArgs fails (no live object) — the token is already known valid,
// so TokenToGUID won't raise. Returns 0 if the GUID isn't rostered, or if an
// explicit powerType other than the member's primary was requested (the roster
// holds only the one power).
enum class RosterKind { Current, Max, Missing };

double RosterPowerFallback(void *L, RosterKind kind) {
    if (!Game::Lua::IsString(L, 1))
        return 0.0;
    auto tokenToGuid = reinterpret_cast<uint64_t(__fastcall *)(const char *)>(
        static_cast<uintptr_t>(Offsets::FUN_TOKEN_TO_GUID));
    const Group::MemberStats::Stats s =
        Group::MemberStats::Lookup(tokenToGuid(Game::Lua::ToString(L, 1)));
    if (!s.valid)
        return 0.0;

    const int reqType = Game::Lua::IsNumber(L, 2)
                            ? static_cast<int>(Game::Lua::ToNumber(L, 2))
                            : -1;
    if (reqType >= Offsets::UNIT_POWER_MIN_TYPE &&
        reqType <= Offsets::UNIT_POWER_MAX_TYPE && reqType != s.powerType)
        return 0.0;

    const uint32_t divisor =
        Game::Lua::ToBoolean(L, 3) ? 1u : DisplayDivisor(s.powerType);
    const uint32_t curDisp = s.power / divisor;
    const uint32_t maxDisp = s.maxPower / divisor;
    switch (kind) {
    case RosterKind::Current:
        return static_cast<double>(curDisp);
    case RosterKind::Max:
        return static_cast<double>(maxDisp);
    case RosterKind::Missing:
        return static_cast<double>(maxDisp > curDisp ? maxDisp - curDisp : 0u);
    }
    return 0.0;
}

// `UnitPower("unit" [, powerType])` — returns the current power
// value for the given type. Defaults to the unit's primary power
// when `powerType` is omitted or set to the modern -1 sentinel.
// Returns 0 for invalid units or unsupported types (DK powers in
// vanilla).
static int __fastcall Script_UnitPower(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitPower(\"unit\" [, type [, unmodified]])");
        return 0;
    }
    const uint8_t *desc;
    int type;
    if (!ResolveArgs(L, &desc, &type)) {
        // No live object → out-of-range group member; try the roster cache.
        Game::Lua::PushNumber(L, RosterPowerFallback(L, RosterKind::Current));
        return 1;
    }
    const uint32_t raw = *reinterpret_cast<const uint32_t *>(
        desc + Offsets::OFF_UNIT_FIELD_POWER1 + type * 4);
    // `unmodified` (arg 3, truthy) returns the raw internal value; otherwise
    // apply the per-type display divisor. Matches retail's third UnitPower arg.
    const uint32_t divisor = Game::Lua::ToBoolean(L, 3) ? 1u : DisplayDivisor(type);
    Game::Lua::PushNumber(L, static_cast<double>(raw / divisor));
    return 1;
}

// `UnitPowerMax("unit" [, powerType])` — returns the max power
// for the given type. Same arg shape and fallback semantics as
// `UnitPower`.
static int __fastcall Script_UnitPowerMax(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitPowerMax(\"unit\" [, type [, unmodified]])");
        return 0;
    }
    const uint8_t *desc;
    int type;
    if (!ResolveArgs(L, &desc, &type)) {
        Game::Lua::PushNumber(L, RosterPowerFallback(L, RosterKind::Max));
        return 1;
    }
    const uint32_t raw = *reinterpret_cast<const uint32_t *>(
        desc + Offsets::OFF_UNIT_FIELD_MAXPOWER1 + type * 4);
    const uint32_t divisor = Game::Lua::ToBoolean(L, 3) ? 1u : DisplayDivisor(type);
    Game::Lua::PushNumber(L, static_cast<double>(raw / divisor));
    return 1;
}

// `UnitPowerMissing("unit" [, powerType [, unmodified]])` — the power
// deficit, i.e. `UnitPowerMax - UnitPower` for the given type. Same arg
// shape and fallbacks as `UnitPower`; `unmodified` (arg 3, truthy) skips
// the display divisor and returns the raw internal deficit, mirroring
// retail's third `UnitPower` arg.
//
// Computed as the difference of the two DISPLAY values (each integer-divided
// by the per-type divisor), NOT by dividing the raw difference — so it equals
// `UnitPowerMax(...) - UnitPower(...)` exactly. For rage those diverge:
// raw 1000/55 displays as 100/5, giving a deficit of 95, whereas
// `(1000-55)/10` truncates to 94.
static int __fastcall Script_UnitPowerMissing(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(
            L, "Usage: UnitPowerMissing(\"unit\" [, powerType [, unmodified]])");
        return 0;
    }
    const uint8_t *desc;
    int type;
    if (!ResolveArgs(L, &desc, &type)) {
        Game::Lua::PushNumber(L, RosterPowerFallback(L, RosterKind::Missing));
        return 1;
    }
    const uint32_t cur = *reinterpret_cast<const uint32_t *>(
        desc + Offsets::OFF_UNIT_FIELD_POWER1 + type * 4);
    const uint32_t max = *reinterpret_cast<const uint32_t *>(
        desc + Offsets::OFF_UNIT_FIELD_MAXPOWER1 + type * 4);
    const uint32_t divisor = Game::Lua::ToBoolean(L, 3) ? 1u : DisplayDivisor(type);
    const uint32_t curDisp = cur / divisor;
    const uint32_t maxDisp = max / divisor;
    Game::Lua::PushNumber(
        L, static_cast<double>(maxDisp > curDisp ? maxDisp - curDisp : 0u));
    return 1;
}

// `UnitPowerType(unit)` — extended to return `(powerType, powerToken)`.
// Vanilla 1.12's implementation pushes only the integer; we chain
// to the engine's original (`0x00517940`) to preserve its full
// resolution chain (object-manager lookup with pet / totem
// fallbacks) and then append the token string.
//
// The original returns 1 if it pushed a value, 0 on the usage-error
// path. We propagate the latter unchanged; in the former case we
// read the just-pushed integer back off the top of the stack and
// push the modern token alongside it. Lua callers that consume
// only the first return are unaffected.
constexpr uintptr_t kVanillaUnitPowerType = 0x00517940;

static int __fastcall Script_UnitPowerType(void *L) {
    using Orig_t = int(__fastcall *)(void *L);
    auto orig = reinterpret_cast<Orig_t>(kVanillaUnitPowerType);
    const int n = orig(L);
    if (n != 1)
        return n;
    const int powerType = static_cast<int>(Game::Lua::ToNumber(L, -1));
    Game::Lua::PushString(L, PowerTypeToken(powerType));
    return 2;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitPower", &Script_UnitPower);
    Game::Lua::RegisterGlobalFunction("UnitPowerMax", &Script_UnitPowerMax);
    Game::Lua::RegisterGlobalFunction("UnitPowerMissing", &Script_UnitPowerMissing);
    Game::Lua::RegisterGlobalFunction("UnitPowerType", &Script_UnitPowerType);
    Game::Lua::RegisterIntegerEnum("Enum", "PowerType", kPowerTypeEntries);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::Power
