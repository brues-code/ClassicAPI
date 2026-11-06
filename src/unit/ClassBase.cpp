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

namespace Unit::ClassBase {

namespace {

// `UnitClassBase(unit) → (classFile, classID)` — the locale-independent
// class token ("WARRIOR", "PALADIN", "HUNTER", "ROGUE", "PRIEST",
// "SHAMAN", "MAGE", "WARLOCK", "DRUID") plus the numeric classID
// (1=Warrior, 2=Paladin, 3=Hunter, 4=Rogue, 5=Priest, 7=Shaman, 8=Mage,
// 9=Warlock, 11=Druid; 6 and 10 have no ChrClasses row), or `(nil, nil)`.
//
// Unit detection is a step-for-step mirror of stock `Script_UnitClass`
// (0x00518350), so the two always agree on which unit and which class
// byte; only the pushed fields differ (token + id here, localized name
// + token there — stock returns exactly two values):
//   - `"player"` (whole-string SStrCmpI) reads the selected-character
//     record's class byte (`VAR_PLAYER_CLASS_BYTE`), filled at EnterWorld,
//     so it answers before the player object exists.
//   - Any other token: GetGUIDFromName + ClntObjMgrObjectPtr(TYPEMASK_UNIT)
//     (= `FUN_RESOLVE_UNIT_TOKEN`). A held object gives its descriptor
//     byte `+0x79`; ONLY when no object is held does it peek the NameCache
//     record's `+0x140` (null callback, no query) — out-of-range party /
//     raid members.
//   - The byte indexes ChrClasses by id; an out-of-range id or empty row
//     is `(nil, nil)`. Creatures carry a class byte too and go through
//     the same lookup, exactly as they do for `UnitClass`.
//
// Throws `Usage:` on a missing / non-string argument, like `UnitClass`.
int __fastcall Script_UnitClassBase(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitClassBase(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);
    if (token == nullptr) {
        Game::Lua::PushNil(L);
        Game::Lua::PushNil(L);
        return 2;
    }

    uint8_t classByte = 0;
    if (Unit::Identity::IsPlayerToken(token)) {
        classByte = Game::Read<uint8_t>(Offsets::VAR_PLAYER_CLASS_BYTE);
    } else if (auto *unit =
                   static_cast<const uint8_t *>(Game::ResolveUnitToken(token))) {
        auto *desc = Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_DESCRIPTOR);
        if (desc != nullptr)
            classByte = *(desc + Offsets::OFF_UNIT_DESCRIPTOR_CLASS_BYTE);
    } else {
        // No object held: the NameCache, as UnitClass does. `token` already
        // passed the resolver, so GuidForToken won't raise.
        const uint8_t *rec =
            Unit::Identity::PlayerInfoRecord(Unit::Identity::GuidForToken(token));
        if (rec != nullptr)
            classByte = static_cast<uint8_t>(
                Game::Read<uint32_t>(rec, Offsets::OFF_PLAYER_INFO_CLASS));
    }
    if (classByte == 0) {
        Game::Lua::PushNil(L);
        Game::Lua::PushNil(L);
        return 2;
    }

    const char *classFile = DBC::ClassToken(classByte);
    if (classFile == nullptr) {
        Game::Lua::PushNil(L);
        Game::Lua::PushNil(L);
        return 2;
    }
    Game::Lua::PushString(L, classFile);
    Game::Lua::PushNumber(L, static_cast<double>(classByte));
    return 2;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitClassBase", &Script_UnitClassBase);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::ClassBase
