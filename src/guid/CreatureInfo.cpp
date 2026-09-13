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
#include "guid/Guid.h"
#include "unit/CreatureID.h"

#include <cstdint>

namespace Guid::CreatureInfoBindings {

namespace {

// `C_CreatureInfo.GetCreatureID(guid) → creatureID | nil`
//
// The creature template / NPC ID for a unit GUID. Resolution lives in
// `Unit::CreatureID::ForGuid`: the unit's LIVE instance-block entry while the
// object is in view (the field the engine keys the creature's name on), else
// the entry packed in a creature GUID's bits 24-47. See unit/CreatureID.cpp
// for why the two differ on this server family (multi-id spawns re-roll the
// template but keep the GUID; pet GUIDs pack the pet number).
//
// Modern's `C_CreatureInfo.GetCreatureID` returns nil for any GUID that
// isn't a creature; we additionally answer pet GUIDs while the pet is in view
// (its template drives the pet bar icon). Game-object GUIDs carry entry IDs in
// the same bit range but modern doesn't surface them through
// `C_CreatureInfo` — addons that need those should look at the GUID prefix
// and shift manually.
//
// Returns nil for: non-string input, malformed GUIDs, non-creature /
// non-pet types, an out-of-view pet, and entry IDs of 0 (the engine never
// assigns entry 0 to anything; treat as "no info").
int __fastcall Script_GetCreatureID(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    uint64_t guid = 0;
    if (!Parse(Game::Lua::ToString(L, 1), &guid)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const uint32_t entryID = Unit::CreatureID::ForGuid(guid);
    if (entryID == 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushNumber(L, static_cast<double>(entryID));
    return 1;
}

void Register() {
    Game::Lua::RegisterTableFunction("C_CreatureInfo", "GetCreatureID",
                                     &Script_GetCreatureID);
}

} // namespace

static const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace Guid::CreatureInfoBindings
