// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Names.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Unit::RaceBase {

namespace {

// `UnitRaceBase(unit) → (raceFile, raceID)` — the locale-independent
// race token (`"Human"`, `"Orc"`, `"Dwarf"`, `"NightElf"`, `"Scourge"`,
// `"Tauren"`, `"Gnome"`, `"Troll"`) plus the numeric raceID (1..8), or
// `(nil, nil)`.
//
// Unit detection is a step-for-step mirror of stock `Script_UnitRace`
// (0x00518200), the same shape as `UnitClassBase` / `UnitClass`:
// `"player"` reads the selected-character record (`VAR_PLAYER_RACE_BYTE`);
// any other token takes a held object's descriptor byte `+0x78`, and ONLY
// when no object is held peeks the NameCache record's `+0x138`. The byte
// indexes ChrRaces by id; the token is `ChrRaces.dbc::Filename` at `+0x3C`
// (stock pushes the localized `Name[locale]` at `+0x44` plus this same
// field). Race 0 (most creatures) or a missing row is `(nil, nil)`.
int __fastcall Script_UnitRaceBase(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitRaceBase(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);
    if (token == nullptr) {
        Game::Lua::PushNil(L);
        Game::Lua::PushNil(L);
        return 2;
    }

    uint8_t raceByte = 0;
    if (Unit::Identity::IsPlayerToken(token)) {
        raceByte = Game::Read<uint8_t>(Offsets::VAR_PLAYER_RACE_BYTE);
    } else if (auto *unit =
                   static_cast<const uint8_t *>(Game::ResolveUnitToken(token))) {
        auto *desc = Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_DESCRIPTOR);
        if (desc != nullptr)
            raceByte = *(desc + Offsets::OFF_UNIT_DESCRIPTOR_RACE_BYTE);
    } else {
        // No object held: the NameCache, as UnitRace does. `token` already
        // passed the resolver, so GuidForToken won't raise.
        const uint8_t *rec =
            Unit::Identity::PlayerInfoRecord(Unit::Identity::GuidForToken(token));
        if (rec != nullptr)
            raceByte = static_cast<uint8_t>(
                Game::Read<uint32_t>(rec, Offsets::OFF_PLAYER_INFO_RACE));
    }
    if (raceByte == 0) {
        Game::Lua::PushNil(L);
        Game::Lua::PushNil(L);
        return 2;
    }

    const char *raceFile = DBC::RaceToken(raceByte);
    if (raceFile == nullptr) {
        Game::Lua::PushNil(L);
        Game::Lua::PushNil(L);
        return 2;
    }
    Game::Lua::PushString(L, raceFile);
    Game::Lua::PushNumber(L, static_cast<double>(raceByte));
    return 2;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitRaceBase", &Script_UnitRaceBase);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::RaceBase
