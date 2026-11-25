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

// Which units get a nameplate, by kind — the per-type cvars as the 3.80.2
// client applies them (its unit test FUN_1415d54e0):
//   - a friendly unit needs nameplateShowFriendlyPlayers when it is a player
//     or a player's unit, else nameplateShowFriendlyNpcs. With the switch
//     off, the soft friend still gets its plate while SoftTargetNameplateFriend
//     is on, and the soft interact unit while SoftTargetNameplateInteract is;
//   - a unit a player owns (its charmer, else its creator, is a player) is a
//     totem (creature type 11), else a pet (a charmer or a summoner), else a
//     guardian (a creator alone), and needs that kind's cvar for its side:
//     nameplateShowEnemyTotems / Pets / Guardians or
//     nameplateShowFriendlyPlayerTotems / Pets / Guardians. Units of
//     non-players are never filtered by kind;
//   - nameplateShowEnemyMinions and nameplateShowFriendlyPlayerMinions set
//     their three kinds when they change (FUN_1419b4260); nothing reads them.
//
// 1.12's plate gate (FUN_NAMEPLATE_GATE) runs for each unit every frame and
// ends in a release or a bind. It is co-hooked: a unit these rules turn away
// gets the gate's own reject, ReleaseNamePlate, and the gate does not run.
// The side is the gate's own test (NamePlate::Side). Friendly totems take no
// plate in 1.12 (the gate's creature-type test); the type it reads is
// redirected so a player's totem passes while
// nameplateShowFriendlyPlayerTotems is on. Units of non-players keep that
// rule.
//
// Defaults: 1, which leaves 1.12's plates as they are, except
// nameplateShowFriendlyPlayerTotems, 0, as 1.12 shows no friendly totem.
// 3.80.2's are 0 for all of them (and SoftTargetNameplateFriend /
// Interact 0, kept). SoftTargetNameplateEnemy is retail's (default 1); 3.80.2
// has none (see SoftEnemyForced). Not ported: nameplateShowAll (combat-only plates),
// nameplateShowEnemyMinus (1.12 has no such classification), the hard rule
// that hides critters, and 3.80.2's target and distance overrides.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "nameplate/Side.h"
#include "target/SoftInteract.h"
#include "target/SoftUnit.h"
#include "unit/Flags.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace NamePlate::Filter {

namespace {

using CVar::Factory::GetInt;
using CVar::Factory::Handle;

constexpr int kCreatureTypeTotem = 11;

Handle g_friendlyNpcs = nullptr;
Handle g_friendlyPlayers = nullptr;
Handle g_enemyPets = nullptr;
Handle g_enemyGuardians = nullptr;
Handle g_enemyTotems = nullptr;
Handle g_friendlyPlayerPets = nullptr;
Handle g_friendlyPlayerGuardians = nullptr;
Handle g_friendlyPlayerTotems = nullptr;
Handle g_softFriend = nullptr;
Handle g_softInteract = nullptr;
Handle g_softEnemy = nullptr;

enum class Kind { Pet, Guardian, Totem };

int CreatureType(const void *unit) {
    using CreatureType_t = int(__fastcall *)(const void *unit);
    return reinterpret_cast<CreatureType_t>(Offsets::FUN_UNIT_CREATURE_TYPE)(unit);
}

bool HasGuid(const uint8_t *fields, uintptr_t offset) {
    return Game::Read<uint64_t>(fields, offset) != 0;
}

// The player that owns `unit` (its charmer, else its creator), or null; null
// for a player.
const void *OwningPlayer(const void *unit) {
    using ControllingPlayer_t = void *(__fastcall *)(const void *unit);
    const void *owner =
        reinterpret_cast<ControllingPlayer_t>(Offsets::FUN_UNIT_CONTROLLING_PLAYER)(unit);
    return owner == unit ? nullptr : owner;
}

Kind KindOf(const void *unit) {
    if (CreatureType(unit) == kCreatureTypeTotem)
        return Kind::Totem;
    const auto *fields = Game::Read<const uint8_t *>(unit, Offsets::OFF_OBJECT_FIELDS);
    if (fields == nullptr || HasGuid(fields, Offsets::OFF_UNIT_FIELD_CHARMEDBY) ||
        HasGuid(fields, Offsets::OFF_UNIT_FIELD_SUMMONEDBY) ||
        !HasGuid(fields, Offsets::OFF_UNIT_FIELD_CREATEDBY))
        return Kind::Pet;
    return Kind::Guardian;
}

Handle KindCvar(Kind kind, bool friendly) {
    switch (kind) {
    case Kind::Totem:
        return friendly ? g_friendlyPlayerTotems : g_enemyTotems;
    case Kind::Guardian:
        return friendly ? g_friendlyPlayerGuardians : g_enemyGuardians;
    default:
        return friendly ? g_friendlyPlayerPets : g_enemyPets;
    }
}

// The soft friend and the soft interact unit keep their plate past the
// friendly switches while their cvar is on.
bool SoftExempt(uint64_t guid) {
    return (GetInt(g_softFriend) != 0 &&
            guid == Target::SoftUnit::Get(Target::SoftUnit::Side::Friend)) ||
           (GetInt(g_softInteract) != 0 && guid == Target::SoftInteract::Get());
}

// A switch reads as on until its cvar is registered.
bool On(Handle cvar) { return GetInt(cvar, 1) != 0; }

// Every switch the veto reads lets everything through: 1.12's own plates. A
// friendly player's totem is left out: with its cvar off the gate turns it
// away itself, and with it on the veto passes it.
bool NothingFiltered() {
    return On(g_friendlyNpcs) && On(g_friendlyPlayers) && On(g_enemyPets) &&
           On(g_enemyGuardians) && On(g_enemyTotems) && On(g_friendlyPlayerPets) &&
           On(g_friendlyPlayerGuardians);
}

bool TurnedAway(const void *unit) {
    if (NothingFiltered())
        return false;
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr || player == unit)
        return false;
    const bool friendly = NamePlate::Side::IsFriendly(player, unit);
    const bool isPlayer = Unit::Flags::IsPlayerObject(static_cast<const uint8_t *>(unit));
    const bool playerOwned = !isPlayer && OwningPlayer(unit) != nullptr;
    if (friendly && !On(isPlayer || playerOwned ? g_friendlyPlayers : g_friendlyNpcs)) {
        const auto *base = Game::Read<const uint8_t *>(unit, Offsets::OFF_OBJECT_DATA);
        if (base == nullptr || !SoftExempt(Game::Read<uint64_t>(base, 0)))
            return true;
    }
    if (!playerOwned)
        return false;
    return !On(KindCvar(KindOf(unit), friendly));
}

using Gate_t = void(__fastcall *)(void *unit, void *edx, void *parent);
Gate_t g_gateOriginal = nullptr;

// Retail's SoftTargetNameplateEnemy (3.80.2 has no enemy one): the soft
// enemy keeps its plate while enemy plates are switched off. The gate runs
// with its enemy switch on for that one unit, so every other rule it applies
// (alive, on screen, in range, not stealthed) still decides.
bool SoftEnemyForced(const void *unit) {
    if (GetInt(g_softEnemy) == 0)
        return false;
    if ((Game::Read<uint8_t>(Offsets::VAR_NAMEPLATE_FLAGS) & Offsets::NAMEPLATE_FLAG_ENEMY) != 0)
        return false;
    const auto *base = Game::Read<const uint8_t *>(unit, Offsets::OFF_OBJECT_DATA);
    if (base == nullptr)
        return false;
    const uint64_t soft = Target::SoftUnit::Get(Target::SoftUnit::Side::Enemy);
    return soft != 0 && Game::Read<uint64_t>(base, 0) == soft;
}

void __fastcall Gate_h(void *unit, void *edx, void *parent) {
    if (unit != nullptr && TurnedAway(unit)) {
        using Release_t = void(__thiscall *)(void *unit);
        reinterpret_cast<Release_t>(Offsets::FUN_UNIT_RELEASE_NAMEPLATE)(unit);
        return;
    }
    if (unit != nullptr && SoftEnemyForced(unit)) {
        uint8_t &flags = Game::Ref<uint8_t>(Offsets::VAR_NAMEPLATE_FLAGS);
        flags |= Offsets::NAMEPLATE_FLAG_ENEMY;
        g_gateOriginal(unit, edx, parent);
        flags &= static_cast<uint8_t>(~Offsets::NAMEPLATE_FLAG_ENEMY);
        return;
    }
    g_gateOriginal(unit, edx, parent);
}

const Game::HookAutoRegister _gateHook{Offsets::FUN_NAMEPLATE_GATE,
                                       reinterpret_cast<void *>(&Gate_h),
                                       reinterpret_cast<void **>(&g_gateOriginal)};

// The gate's friendly-totem test: a player's totem reads as no totem while
// nameplateShowFriendlyPlayerTotems is on.
int __fastcall GateCreatureType(const void *unit) {
    const int type = CreatureType(unit);
    if (type == kCreatureTypeTotem && GetInt(g_friendlyPlayerTotems) != 0 &&
        OwningPlayer(unit) != nullptr)
        return 0;
    return type;
}

// nameplateShow*Minions: a change sets the three kinds under it. Registration
// passes a new cvar's default with no previous value, and an existing one's
// value as both; neither is a change.
struct Minions {
    Handle *pets;
    Handle *guardians;
    Handle *totems;
};
const Minions kEnemyMinions{&g_enemyPets, &g_enemyGuardians, &g_enemyTotems};
const Minions kFriendlyPlayerMinions{&g_friendlyPlayerPets, &g_friendlyPlayerGuardians,
                                     &g_friendlyPlayerTotems};

int __fastcall OnMinionsChanged(Handle, const char *prevValue, const char *newValue,
                                void *userData) {
    if (prevValue == nullptr || *prevValue == '\0' || newValue == nullptr ||
        std::strcmp(prevValue, newValue) == 0)
        return 1;
    const auto *minions = static_cast<const Minions *>(userData);
    for (Handle *child : {minions->pets, minions->guardians, minions->totems})
        CVar::Factory::SetString(*child, newValue);
    return 1;
}

void Register() {
    const auto reg = [](const char *name, const char *help, const char *value) {
        return CVar::Factory::Register(name, help, Offsets::CVAR_CATEGORY_GAME, value);
    };
    g_friendlyNpcs = reg("nameplateShowFriendlyNpcs",
                         "Whether nameplates are shown for friendly npcs.", "1");
    g_friendlyPlayers = reg("nameplateShowFriendlyPlayers",
                            "Whether nameplates are shown for friendly players.", "1");
    g_enemyPets = reg("nameplateShowEnemyPets", "Whether enemy pet nameplates are shown.", "1");
    g_enemyGuardians =
        reg("nameplateShowEnemyGuardians", "Whether enemy guardian nameplates are shown.", "1");
    g_enemyTotems =
        reg("nameplateShowEnemyTotems", "Whether enemy totem nameplates are shown.", "1");
    g_friendlyPlayerPets = reg("nameplateShowFriendlyPlayerPets",
                               "Whether friendly player pet nameplates are shown.", "1");
    g_friendlyPlayerGuardians =
        reg("nameplateShowFriendlyPlayerGuardians",
            "Whether friendly player guardian nameplates are shown.", "1");
    g_friendlyPlayerTotems = reg("nameplateShowFriendlyPlayerTotems",
                                 "Whether friendly player totem nameplates are shown.", "0");
    CVar::Factory::Register("nameplateShowEnemyMinions",
                            "Whether enemy minion nameplates are shown.",
                            Offsets::CVAR_CATEGORY_GAME, "1", &OnMinionsChanged,
                            const_cast<Minions *>(&kEnemyMinions));
    CVar::Factory::Register("nameplateShowFriendlyPlayerMinions",
                            "Whether friendly player minion nameplates are shown.",
                            Offsets::CVAR_CATEGORY_GAME, "1", &OnMinionsChanged,
                            const_cast<Minions *>(&kFriendlyPlayerMinions));
    g_softFriend = reg("SoftTargetNameplateFriend",
                       "Always show nameplates for soft friend target", "0");
    g_softInteract = reg("SoftTargetNameplateInteract",
                         "Always show nameplates for soft interact target", "0");
    g_softEnemy = reg("SoftTargetNameplateEnemy", "Always show nameplates for soft enemy target", "1");

    Common::RedirectCall(Offsets::CALL_NAMEPLATE_GATE_CREATURE_TYPE,
                         Offsets::FUN_UNIT_CREATURE_TYPE, reinterpret_cast<void *>(&GateCreatureType));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace NamePlate::Filter
