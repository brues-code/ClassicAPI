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

// Pet / minion classification of a unit:
//   - `UnitIsMinion(unit)`          — modern backport: is the unit a
//     minion of a player (pet, guardian, totem, charmed creature)?
//   - `UnitIsOtherPlayersPet(unit)` — the real API: is the unit a pet
//     controlled by a player OTHER than you?
//
// Neither exists in vanilla 1.12 nor in our 3.3.5 reference client
// (`UnitIsMinion` is a 12.0.0/5.5.4 addition; `UnitIsOtherPlayersPet`
// is 5.0.4/1.13.2), so both are derived from vanilla primitives:
//   * `Unit::Flags::IsPlayerControlled` — UNIT_FLAG_PLAYER_CONTROLLED,
//     set on players and every unit a player controls (pets, guardians,
//     totems, charmed mobs).
//   * The unit's GUID type — vanilla packs the entity type in the GUID's
//     high bits (`Guid::Classify`): players `0x0000`, pets `0xF140`,
//     creatures `0xF130`. A pet-type GUID is intrinsically a player's pet.
//
// `UnitIsMinion` = owned by a player (its SummonedBy/CreatedBy/CharmedBy
// GUID resolves to a player), OR player-controlled-and-not-a-player as a
// fallback. Owner-based is primary because the player-controlled flag
// alone misses summoned guardians — units shown as "X's Minion" that carry
// an owner GUID but not the flag (a real hunter/warlock "X's Pet" has
// both). The union keeps flagged pets working regardless of the owner
// fields.
//
// `UnitIsOtherPlayersPet` is **owner-based**, matching 5.4.8's real
// `Script_UnitIsOtherPlayersPet` (`0x0089FD70`): resolve the unit, read
// its owner GUID (SummonedBy / CreatedBy / CharmedBy), and return true iff
// that owner is a *player* other than you. Owner-based (not a pet-GUID
// heuristic) so it covers pets, guardians, totems, and charmed units of
// other players alike, and excludes *all* of your own minions (owner ==
// you), not just the active pet. The owner-field offsets are byte-verified
// / adjacency-derived — see `OFF_UNIT_FIELD_CHARMEDBY`.

#include "Game.h"
#include "Offsets.h"
#include "guid/Guid.h"
#include "unit/Flags.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Unit::Pet {

namespace {

// `int __fastcall(unit)` → 1 for a controllable pet, non-1 otherwise.
// The engine's pet-vs-minion title discriminator. See
// FUN_UNIT_PET_MINION_CLASS.
using PetMinionClass_t = int(__fastcall *)(const void *unit);

const uint8_t *ResolveUnit(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return nullptr;
    const char *token = Game::Lua::ToString(L, 1);
    if (token == nullptr)
        return nullptr;
    return static_cast<const uint8_t *>(Game::ResolveUnitToken(token));
}

// The spell that summoned this unit (its UNIT_CREATED_BY_SPELL descriptor
// field): the totem-drop spell for a totem, the summon spell for a
// pet/guardian, etc. 0 for a unit not summoned by a spell (players, world
// creatures) or an object with no descriptor.
uint32_t CreatedBySpell(const uint8_t *unit) {
    auto *desc = Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_DESCRIPTOR);
    if (desc == nullptr)
        return 0;
    return Game::Read<uint32_t>(desc, Offsets::OFF_UNIT_FIELD_CREATED_BY_SPELL);
}

// `UnitIsMinion(unit)` — true iff the unit is a player's minion (pet,
// guardian, totem, or charmed creature): player-controlled but not itself
// a player. NPCs, world creatures, and players return false. A bad or
// unresolved unit token returns false (no error), matching the tolerant
// modern predicate convention.
int __fastcall Script_UnitIsMinion(void *L) {
    const uint8_t *unit = ResolveUnit(L);
    if (unit == nullptr) {
        Game::Lua::PushBool(L, 0);
        return 1;
    }
    // Primary: owned by a player (pet, guardian, totem, charmed).
    bool minion = Guid::IsPlayer(Unit::Identity::OwnerGuid(unit));
    // Fallback: a player-controlled non-player with no owner GUID.
    if (!minion && Unit::Flags::IsPlayerControlled(unit))
        minion = !Guid::IsPlayer(Unit::Identity::GuidForObject(unit));
    Game::Lua::PushBool(L, minion);
    return 1;
}

// `UnitIsPet(unit)` — true iff the unit is a **pet** of a player (as
// opposed to a guardian/totem "minion"): owned by a player AND the
// engine's pet/minion classifier `FUN_UNIT_PET_MINION_CLASS` returns 1.
// That classifier — reading the creature *family*, not the GUID or the
// player-controlled flag — is exactly what the engine uses to title a unit
// "X's Pet" vs "X's Minion". On 1.12/Turtle the GUID can't be used (pets
// and guardians share the `0xF14…` prefix) and the PLAYER_CONTROLLED flag
// is unreliable (a "Minion" can carry it), so the family classifier is the
// only correct signal. The owner-is-player gate also rules out wild
// tameable beasts of the same family (which aren't anyone's pet).
// `UnitIsMinion` is the broad umbrella (pet + guardian + totem); this is
// the pet subset. A ClassicAPI extension — not a stock WoW global.
int __fastcall Script_UnitIsPet(void *L) {
    const uint8_t *unit = ResolveUnit(L);
    if (unit == nullptr) {
        Game::Lua::PushBool(L, 0);
        return 1;
    }
    // Owned by a player (rules out wild same-family beasts), AND the
    // engine's own pet/minion classifier says PET (1). The classifier —
    // not the GUID, not the player-controlled flag — is what the engine
    // uses to title "X's Pet" vs "X's Minion".
    bool pet = false;
    if (Guid::IsPlayer(Unit::Identity::OwnerGuid(unit))) {
        auto classify = reinterpret_cast<PetMinionClass_t>(
            Offsets::FUN_UNIT_PET_MINION_CLASS);
        pet = (classify(unit) == 1);
    }
    Game::Lua::PushBool(L, pet);
    return 1;
}

// `UnitIsOtherPlayersPet(unit)` — true iff the unit is a minion (pet,
// guardian, totem, charmed) whose owner is a player other than you.
// Owner-based, mirroring 5.4.8: read the unit's owner GUID, require it to
// be a player, and require it to differ from the local player. World
// creatures and players (no owner) return false; your own minions (owner
// == you) return false.
int __fastcall Script_UnitIsOtherPlayersPet(void *L) {
    const uint8_t *unit = ResolveUnit(L);
    if (unit == nullptr) {
        Game::Lua::PushBool(L, 0);
        return 1;
    }
    const uint64_t owner = Unit::Identity::OwnerGuid(unit);
    const bool otherPlayers = Guid::IsPlayer(owner) &&
                              owner != Unit::Identity::PlayerGuid();
    Game::Lua::PushBool(L, otherPlayers);
    return 1;
}

// `UnitOwnerGUID(unit)` — the GUID string of the unit's owner (the
// summoner of a pet/guardian/totem, or the charmer of a charmed unit),
// formatted `"0x…"`. Reuses the same owner field the pet predicates use
// (CharmedBy, else CreatedBy). Returns `nil` for an unresolved unit and
// for anything with no owner (players, world creatures).
int __fastcall Script_UnitOwnerGUID(void *L) {
    const uint8_t *unit = ResolveUnit(L);
    if (unit == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const uint64_t owner = Unit::Identity::OwnerGuid(unit);
    if (owner == 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    char buf[Guid::STRING_SIZE];
    Game::Lua::PushString(L, Guid::FormatAsString(owner, buf, sizeof buf));
    return 1;
}

// `UnitCreatedBySpell(unit)` — the spell ID that summoned this unit, read
// from its UNIT_CREATED_BY_SPELL field: the shaman totem-drop spell for a
// totem, the summon spell for a pet/guardian, and so on. This is the same
// field the engine's unit-title builder consults to pick the
// "Pet"/"Minion"/"Guardian"/"Creation" title. It is a broadcast descriptor
// field, so it works for any unit in range, not just your own summons.
// Returns nil for an unresolved unit and for anything not summoned by a
// spell (players, world creatures). Note this is the *summoning* spell —
// the spell a totem casts is server-side only and never reaches the client.
// A ClassicAPI extension — not a stock WoW global.
int __fastcall Script_UnitCreatedBySpell(void *L) {
    const uint8_t *unit = ResolveUnit(L);
    if (unit == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const uint32_t spellId = CreatedBySpell(unit);
    if (spellId == 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushNumber(L, static_cast<double>(spellId));
    return 1;
}

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitIsMinion", &Script_UnitIsMinion);
    Game::Lua::RegisterGlobalFunction("UnitIsPet", &Script_UnitIsPet);
    Game::Lua::RegisterGlobalFunction("UnitIsOtherPlayersPet",
                                      &Script_UnitIsOtherPlayersPet);
    Game::Lua::RegisterGlobalFunction("UnitOwnerGUID", &Script_UnitOwnerGUID);
    Game::Lua::RegisterGlobalFunction("UnitCreatedBySpell",
                                      &Script_UnitCreatedBySpell);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Unit::Pet
