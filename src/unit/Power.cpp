// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// The modern power API over 1.12's unit power fields, as the 3.80.2 client
// does it: UnitPower (0x140E40FC0), UnitPowerMax (0x140E417E0),
// UnitPowerMissing (0x140E41AF0), UnitPartialPower (0x140E40210),
// UnitHasPowerType (0x140E385C0), UnitPowerDisplayMod (0x140E416C0), an
// extended UnitPowerType, and Enum.PowerType.
//
// - Types are 3.80.2's Enum.PowerType (see Power.h): 1.12's slots 0..3 keep
//   their numbers, happiness (slot 4) is 27, and type 4 is the player's combo
//   points (Target::ComboPoints), which 1.12 keeps outside the power block.
// - The type argument (0x140B0AC20) is nil for the unit's display power, else
//   an integer whose low byte is 0..29; anything else is the usage error.
// - The gate (0x141BAFF50): a type other than the unit's display power or an
//   alternate power answers 0 unless the unit is the player or its pet.
//   UnitPowerMax has no gate.
// - Values are divided by the engine's per-slot display divisor
//   (FUN_POWER_DISPLAY_DIVISOR: rage 10, happiness 1000) unless `unmodified`.
// - A token whose unit is out of view falls back to the group roster's one
//   power (Group::MemberStats), as 3.80.2's cores do with the member record;
//   UnitPartialPower has no fallback.
//
// 1.12 predicts no power, so the partial-power accumulator UnitPartialPower
// reads in 3.80.2 is always 0 here.

#include "Game.h"
#include "Offsets.h"
#include "group/MemberStats.h"
#include "object/Resolve.h"
#include "target/ComboPoints.h"
#include "unit/Identity.h"
#include "unit/Power.h"

#include <cstdint>

namespace Unit::Power {

int TypeOfSlot(int slot) { return slot == 4 ? kTypeHappiness : slot; }

int SlotOfType(int type) {
    if (type >= 0 && type <= 3)
        return type;
    return type == kTypeHappiness ? 4 : -1;
}

const char *TypeToken(int type) {
    switch (type) {
    case 0: return "MANA";
    case 1: return "RAGE";
    case 2: return "FOCUS";
    case 3: return "ENERGY";
    case kTypeComboPoints: return "COMBO_POINTS";
    case kTypeHappiness: return "HAPPINESS";
    }
    return "UNKNOWN";
}

uint32_t Stored(const uint8_t *fields, int slot) {
    return Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_POWER1 + slot * 4);
}

uint32_t StoredMax(const uint8_t *fields, int slot) {
    return Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_MAXPOWER1 + slot * 4);
}

unsigned int DisplayDivisor(int slot) {
    using Divisor_t = unsigned int(__fastcall *)(int powerType);
    return reinterpret_cast<Divisor_t>(
        static_cast<uintptr_t>(Offsets::FUN_POWER_DISPLAY_DIVISOR))(slot);
}

namespace {

// Enum.PowerType as the 3.80.2 client has it, plus retail's HealthCost / None.
constexpr Game::Lua::EnumIntegerEntry kPowerTypeEntries[] = {
    {"HealthCost", -2},       {"None", -1},
    {"Mana", 0},              {"Rage", 1},
    {"Focus", 2},             {"Energy", 3},
    {"ComboPoints", 4},       {"Runes", 5},
    {"RunicPower", 6},        {"SoulShards", 7},
    {"LunarPower", 8},        {"HolyPower", 9},
    {"Alternate", 10},        {"Maelstrom", 11},
    {"Chi", 12},              {"Insanity", 13},
    {"BurningEmbers", 14},    {"DemonicFury", 15},
    {"ArcaneCharges", 16},    {"Fury", 17},
    {"Pain", 18},             {"Essence", 19},
    {"RuneBlood", 20},        {"RuneFrost", 21},
    {"RuneUnholy", 22},       {"AlternateQuest", 23},
    {"AlternateEncounter", 24}, {"AlternateMount", 25},
    {"Balance", 26},          {"Happiness", 27},
    {"ShadowOrbs", 28},       {"RuneChromatic", 29},
};

constexpr int kDisplayType = -1; // the type argument was nil

// The alternate powers every unit answers for (3.80.2's list at 0x143CDC500).
bool IsAlternate(int type) { return type == 10 || type == 23 || type == 24 || type == 25; }

// 0x140B0AC20: nil or none → the display power; an integer whose low byte is
// 0..29 → that type; anything else → false (the usage error).
bool ReadType(void *L, int idx, int *out) {
    if (Game::Lua::Type(L, idx) <= Game::Lua::TYPE_NIL) {
        *out = kDisplayType;
        return true;
    }
    if (!Game::Lua::IsNumber(L, idx))
        return false;
    const auto low = static_cast<uint8_t>(static_cast<int>(Game::Lua::ToNumber(L, idx)));
    if (low >= kTypeCount)
        return false;
    *out = low;
    return true;
}

struct Args {
    const char *token;
    int type;
    bool unmodified;
};

bool ReadArgs(void *L, Args *a, bool typeRequired = false) {
    if (!Game::Lua::IsString(L, 1))
        return false;
    a->token = Game::Lua::ToString(L, 1);
    if (!ReadType(L, 2, &a->type) || (typeRequired && a->type == kDisplayType))
        return false;
    a->unmodified = Game::Lua::ToBoolean(L, 3);
    return true;
}

// One power of a live unit, as 3.80.2's type→slot map (0x1414CF890) finds it.
struct Reading {
    bool hasSlot = false; // the type has a store on this unit
    uint32_t current = 0;
    uint32_t max = 0;
    unsigned divisor = 1;
};

struct LiveUnit {
    const void *object;
    const uint8_t *fields;
    int displayType; // -1 when the byte names no 1.12 slot
    bool isPlayer;
    bool isPet;
};

// The live unit `token` names, or false.
bool ResolveLive(const char *token, LiveUnit *u) {
    u->object = Game::ResolveUnitToken(token);
    u->fields = ::Object::Fields(u->object);
    if (u->fields == nullptr)
        return false;
    const int slot = u->fields[Offsets::OFF_UNIT_DESCRIPTOR_POWER_TYPE_BYTE];
    u->displayType = slot < Offsets::UNIT_POWER_TYPE_COUNT ? TypeOfSlot(slot) : -1;
    const uint64_t guid = ::Object::Guid(u->object);
    u->isPlayer = guid == ::Unit::Identity::GuidForToken("player");
    const uint64_t pet = ::Unit::Identity::GuidForToken("pet");
    u->isPet = pet != 0 && guid == pet;
    return true;
}

// The player's class table in 3.80.2 gives each class its own powers; 1.12's
// player has all five slots, and the server sets a maximum only for the
// powers the class uses, so a slot with a maximum is a power the player has.
Reading Read(const LiveUnit &u, int type) {
    Reading r;
    if (type == kTypeComboPoints) {
        if (u.isPlayer && ::Target::ComboPoints::ClassUses(u.fields)) {
            r.hasSlot = true;
            r.current = ::Target::ComboPoints::Count();
            r.max = ::Target::ComboPoints::kMax;
        }
        return r;
    }
    const int slot = SlotOfType(type);
    if (slot < 0)
        return r;
    r.current = Stored(u.fields, slot);
    r.max = StoredMax(u.fields, slot);
    r.divisor = DisplayDivisor(slot);
    // A non-player has its display power and happiness (3.80.2's non-player
    // slots 0 and 3; its alternate and combo slots have no 1.12 store).
    r.hasSlot = u.isPlayer ? r.max != 0 : (type == u.displayType || type == kTypeHappiness);
    return r;
}

// UnitPower's gate (0x141BAFF50).
bool PassesGate(const LiveUnit &u, int type) {
    return type == u.displayType || IsAlternate(type) || u.isPlayer || u.isPet;
}

double Shown(uint32_t value, unsigned divisor, bool unmodified) {
    return static_cast<double>(unmodified || divisor == 0 ? value : value / divisor);
}

// The group roster's one power for an out-of-view member (Group::MemberStats,
// fed by SMSG_PARTY_MEMBER_STATS), as the cores' member-record fallback reads
// it: any other type answers 0.
bool RosterReading(const Args &a, Reading *r) {
    const Group::MemberStats::Stats s =
        Group::MemberStats::Lookup(::Unit::Identity::GuidForToken(a.token));
    if (!s.valid)
        return false;
    if (a.type != kDisplayType && a.type != TypeOfSlot(s.powerType))
        return false;
    r->hasSlot = true;
    r->current = s.power;
    r->max = s.maxPower;
    r->divisor = DisplayDivisor(s.powerType);
    return true;
}

enum class Kind { Current, Max, Missing };

// UnitPower / UnitPowerMax / UnitPowerMissing.
double Value(const Args &a, Kind kind) {
    LiveUnit u;
    Reading r;
    if (ResolveLive(a.token, &u)) {
        const int type = a.type == kDisplayType ? u.displayType : a.type;
        if (type < 0)
            return 0.0;
        r = Read(u, type);
        // Only the current-value core is gated; UnitPowerMissing calls both.
        if (!PassesGate(u, type))
            r.current = 0;
    } else if (!RosterReading(a, &r)) {
        return 0.0;
    }
    const double cur = Shown(r.current, r.divisor, a.unmodified);
    const double max = Shown(r.max, r.divisor, a.unmodified);
    switch (kind) {
    case Kind::Current: return cur;
    case Kind::Max: return max;
    case Kind::Missing: return max - cur; // not clamped, as 3.80.2's
    }
    return 0.0;
}

int Push(void *L, double v) {
    Game::Lua::PushNumber(L, v);
    return 1;
}

int __fastcall Script_UnitPower(void *L) {
    Args a;
    if (!ReadArgs(L, &a)) {
        Game::Lua::Error(L, "Usage: local power = UnitPower(unitToken [, powerType, unmodified])");
        return 0;
    }
    return Push(L, Value(a, Kind::Current));
}

int __fastcall Script_UnitPowerMax(void *L) {
    Args a;
    if (!ReadArgs(L, &a)) {
        Game::Lua::Error(L,
                         "Usage: local maxPower = UnitPowerMax(unitToken [, powerType, unmodified])");
        return 0;
    }
    return Push(L, Value(a, Kind::Max));
}

int __fastcall Script_UnitPowerMissing(void *L) {
    Args a;
    if (!ReadArgs(L, &a)) {
        Game::Lua::Error(
            L, "Usage: local result = UnitPowerMissing(unitToken [, powerType, unmodified])");
        return 0;
    }
    return Push(L, Value(a, Kind::Missing));
}

// 1.12 has no partial power: 0 for every unit, after 3.80.2's argument checks.
int __fastcall Script_UnitPartialPower(void *L) {
    Args a;
    if (!ReadArgs(L, &a)) {
        Game::Lua::Error(
            L, "Usage: local partialPower = UnitPartialPower(unitToken [, powerType, unmodified])");
        return 0;
    }
    return Push(L, 0.0);
}

int __fastcall Script_UnitHasPowerType(void *L) {
    Args a;
    if (!ReadArgs(L, &a, true)) {
        Game::Lua::Error(L, "Usage: local hasPower = UnitHasPowerType(unitToken, powerType)");
        return 0;
    }
    bool has = false;
    LiveUnit u;
    if (ResolveLive(a.token, &u)) {
        has = Read(u, a.type).hasSlot &&
              (u.isPlayer || a.type == u.displayType || IsAlternate(a.type) || u.isPet);
    } else {
        const Group::MemberStats::Stats s =
            Group::MemberStats::Lookup(::Unit::Identity::GuidForToken(a.token));
        has = s.valid && TypeOfSlot(s.powerType) == a.type;
    }
    Game::Lua::PushBool(L, has);
    return 1;
}

// The display divisor of a type: 1 for a type 1.12 has no slot for.
int __fastcall Script_UnitPowerDisplayMod(void *L) {
    int slot = -1;
    if (Game::Lua::IsNumber(L, 1))
        slot = SlotOfType(static_cast<int>(Game::Lua::ToNumber(L, 1)));
    return Push(L, slot >= 0 ? static_cast<double>(DisplayDivisor(slot)) : 1.0);
}

// `UnitPowerType(unit)` → (powerType, powerToken). 1.12's own (0x00517940)
// resolves the unit with its pet / totem fallbacks and pushes the slot; the
// slot is swapped for its type and the token follows.
int __fastcall Script_UnitPowerType(void *L) {
    using Orig_t = int(__fastcall *)(void *L);
    auto orig = reinterpret_cast<Orig_t>(static_cast<uintptr_t>(Offsets::FUN_SCRIPT_UNIT_POWER_TYPE));
    const int n = orig(L);
    if (n != 1)
        return n;
    const int type = TypeOfSlot(static_cast<int>(Game::Lua::ToNumber(L, -1)));
    Game::Lua::SetTop(L, -2);
    Game::Lua::PushNumber(L, static_cast<double>(type));
    Game::Lua::PushString(L, TypeToken(type));
    return 2;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kPowerArgs[] = {
    Game::Doc::Req("unitToken", "UnitToken"),
    Game::Doc::Opt("powerType", "PowerType", nullptr, "Omitted uses the unit's display power."),
    Game::Doc::Opt("unmodified", "bool", "false",
                   "True returns rage and happiness at their internal scale, such as rage 0 to "
                   "1000."),
};
const Game::Doc::Field kPowerRets[] = {
    Game::Doc::Req("power", "number",
                   "0 when the token names no unit, and for a power other than the unit's own "
                   "unless the unit is the player or the pet."),
};
const Game::Doc::Function kUnitPower{
    "Returns a unit's current power of one type.", kPowerArgs, kPowerRets, "UnitGlobals"};

const Game::Doc::Field kPowerMaxRets[] = {
    Game::Doc::Req("maxPower", "number", "0 when the token names no unit."),
};
const Game::Doc::Function kUnitPowerMax{
    "Returns a unit's maximum power of one type.", kPowerArgs, kPowerMaxRets, "UnitGlobals"};

const Game::Doc::Field kPowerMissingRets[] = {
    Game::Doc::Req("result", "number", "UnitPowerMax minus UnitPower; can be negative."),
};
const Game::Doc::Function kUnitPowerMissing{
    "Returns how much power of one type a unit is missing.", kPowerArgs, kPowerMissingRets,
    "UnitGlobals"};

const Game::Doc::Field kPartialRets[] = {
    Game::Doc::Req("partialPower", "number", "Always 0."),
};
const Game::Doc::Function kUnitPartialPower{
    "Returns the fraction of a power point a unit has built up.", kPowerArgs, kPartialRets,
    "UnitGlobals"};

const Game::Doc::Field kHasPowerTypeArgs[] = {
    Game::Doc::Req("unitToken", "UnitToken"),
    Game::Doc::Req("powerType", "PowerType"),
};
const Game::Doc::Field kHasPowerTypeRets[] = {
    Game::Doc::Req("hasPower", "bool"),
};
const Game::Doc::Function kUnitHasPowerType{"Returns whether a unit has a power type.",
                                            kHasPowerTypeArgs, kHasPowerTypeRets, "UnitGlobals"};

const Game::Doc::Field kDisplayModArgs[] = {
    Game::Doc::Req("powerType", "PowerType"),
};
const Game::Doc::Field kDisplayModRets[] = {
    Game::Doc::Req("displayMod", "number", "10 for rage, 1000 for happiness, else 1."),
};
const Game::Doc::Function kUnitPowerDisplayMod{
    "Returns what a power type's internal value is divided by for display.", kDisplayModArgs,
    kDisplayModRets, "UnitGlobals"};

const Game::Doc::Field kPowerTypeArgs[] = {
    Game::Doc::Req("unit", "UnitToken"),
};
const Game::Doc::Field kPowerTypeRets[] = {
    Game::Doc::Req("powerType", "PowerType", "0 (Mana) when the token names no unit."),
    Game::Doc::Req("powerTypeToken", "string", "For example \"MANA\" or \"RAGE\"."),
};
const Game::Doc::Function kUnitPowerType{
    "Returns the type and token of a unit's display power.", kPowerTypeArgs, kPowerTypeRets,
    "UnitGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitPower", &Script_UnitPower, &kUnitPower);
    Game::Lua::RegisterGlobalFunction("UnitPowerMax", &Script_UnitPowerMax, &kUnitPowerMax);
    Game::Lua::RegisterGlobalFunction("UnitPowerMissing", &Script_UnitPowerMissing,
                                      &kUnitPowerMissing);
    Game::Lua::RegisterGlobalFunction("UnitPartialPower", &Script_UnitPartialPower,
                                      &kUnitPartialPower);
    Game::Lua::RegisterGlobalFunction("UnitHasPowerType", &Script_UnitHasPowerType,
                                      &kUnitHasPowerType);
    Game::Lua::RegisterGlobalFunction("UnitPowerDisplayMod", &Script_UnitPowerDisplayMod,
                                      &kUnitPowerDisplayMod);
    Game::Lua::RegisterGlobalFunction("UnitPowerType", &Script_UnitPowerType, &kUnitPowerType);
    Game::Lua::RegisterIntegerEnum("Enum", "PowerType", kPowerTypeEntries, "PowerTypeConstants");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::Power
