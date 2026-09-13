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

// `Unit::CreatureID` — the creature-template / NPC id behind a unit, and the
// Lua `UnitCreatureID(unit)` (the token twin of `C_CreatureInfo.GetCreatureID`).
//
// Three places carry an "entry", and on this server family they DISAGREE:
//
//  - The unit's INSTANCE BLOCK entry (`[[unit + OFF_UNIT_GUID_PTR] +
//    OFF_UNIT_INSTANCE_ENTRY]`) is the LIVE creature_template entry — the
//    field the engine keys the creature NAME query on (`FUN_00604600` calls
//    the creature cache with exactly this read, on creation and as the
//    OBJECT-bank field-0xC handler). It is the `creature_template` primary
//    key, hence also the id Wowhead / Turtle's database key NPC pages on.
//  - The descriptor's OBJECT_FIELD_ENTRY slot reads 0 for creatures on this
//    client (as it does for items — see the CGItem note in Offsets.h). A first
//    version read it and silently fell back to the GUID bits.
//  - The GUID's bits 24-47 are whatever the server put there at spawn. The
//    (v)mangos family — tortoise verified — builds a creature GUID from the
//    spawn row's FIRST id (`Creature::CreateFromProto` →
//    `Object::_Create(guidlow, creature_id[0], …)`), then applies the rolled
//    template through `UpdateEntry` → `SetEntry`. A `creature` row holds up
//    to four ids (6,935 of Turtle's 79,773 rows use more than one) and a
//    respawn re-rolls, so a mob whose name says template B can carry template
//    A in its GUID: Turtle's Gordunni rows are {5236 Shaman, 5238
//    Battlemaster, 5239 Mage-Lord}, and a Gordunni Mage-Lord probes as
//    block 5239 / GUID 5236 — "different names, same creature id" when read
//    from the GUID. Pet GUIDs are worse: their entry bits are the PET NUMBER
//    (`Pet::Create` → `_Create(guidlow, pet_number, HIGHGUID_PET)`), never a
//    template.
//
// So the id is read from the instance block whenever the object is
// resolvable, and the GUID bits are only the out-of-view fallback for
// creature GUIDs (exact on every single-id spawn, i.e. all of stock vanilla).
//
// `UnitCreatureID` returns nil for players (entry 0), an unresolvable-but-
// valid token (`target` with nothing targeted, an empty `partyN` slot), and
// any non-creature / non-pet GUID. Raises a Lua error on a missing /
// non-string `unit` or a garbage token — the standard `UnitX` "Unknown unit"
// behavior (via the token resolver).

#include "Game.h"
#include "Offsets.h"
#include "guid/Guid.h"
#include "object/Resolve.h"
#include "unit/CreatureID.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Unit::CreatureID {

uint32_t ForObject(const void *unit) {
    if (unit == nullptr)
        return 0;
    auto *block = Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_GUID_PTR);
    if (block == nullptr)
        return 0;
    return Game::Read<uint32_t>(block, Offsets::OFF_UNIT_INSTANCE_ENTRY);
}

uint32_t ForGuid(uint64_t guid) {
    const Guid::Type type = Guid::Classify(guid);
    if (type != Guid::Type::Creature && type != Guid::Type::Pet)
        return 0;
    if (const void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid)) {
        const uint32_t entry = ForObject(unit);
        if (entry != 0)
            return entry;
    }
    // Out of view: only a creature GUID packs a template (a pet's packs the
    // pet number → Guid::CreatureEntry yields 0 for it).
    return Guid::CreatureEntry(guid);
}

namespace {

int __fastcall Script_UnitCreatureID(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitCreatureID(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);
    if (token == nullptr)
        return 0;

    // Token -> GUID (player fast-path, focus / nameplateN / markN, raw GUID),
    // then the shared block-first lookup. A valid-but-empty token resolves to
    // GUID 0 -> 0 -> nil; a player resolves to a player GUID -> 0 -> nil.
    const uint32_t entryID = ForGuid(Unit::Identity::GuidForToken(token));
    if (entryID == 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushNumber(L, static_cast<double>(entryID));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitCreatureID", &Script_UnitCreatureID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::CreatureID
