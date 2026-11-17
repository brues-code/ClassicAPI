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

// The group's names for the resolver hook. See TokenNames.h for the contract.
//
// Every unit call that misses ClassicAPI's own token families asks here, so
// the list is not rebuilt per call. It goes stale once per world frame and on
// the roster signals, and is rebuilt by the first lookup after that. A
// member's name is kept while its slot holds the same GUID, once the name
// cache has it: players are not renamed in a session. A pet's is read again
// on every rebuild, since hunters rename theirs.

#include "unit/TokenNames.h"

#include "Game.h"
#include "Offsets.h"
#include "event/SignalHook.h"
#include "group/Roster.h"
#include "player/Info.h"
#include "tick/WorldTick.h"
#include "unit/Identity.h"

#include <cstdio>

namespace Unit::TokenNames {

namespace {

// Slot 0 is you (`player` / `pet`), 1..4 the party, 5..44 the raid.
constexpr int kPartyBase = 1;
constexpr int kRaidBase = kPartyBase + Offsets::PARTY_MAX_SLOTS;
constexpr int kSlotCount = kRaidBase + Offsets::RAID_MAX_SLOTS;

// Script_GetGUIDFromString's name buffer: 48 characters and the NUL.
constexpr int kNameSize = 49;

struct Slot {
    uint64_t guid;
    int len; // 0 = no name known
    char name[kNameSize];
};

Slot g_members[kSlotCount];
Slot g_pets[kSlotCount];
char g_petTokens[kSlotCount][12];
bool g_inRaid = false;
bool g_stale = true;

void BuildPetTokens() {
    std::snprintf(g_petTokens[0], sizeof g_petTokens[0], "pet");
    for (int i = 0; i < Offsets::PARTY_MAX_SLOTS; ++i)
        std::snprintf(g_petTokens[kPartyBase + i], sizeof g_petTokens[0], "partypet%d", i + 1);
    for (int i = 0; i < Offsets::RAID_MAX_SLOTS; ++i)
        std::snprintf(g_petTokens[kRaidBase + i], sizeof g_petTokens[0], "raidpet%d", i + 1);
}

void Store(Slot &slot, uint64_t guid, const char *name) {
    slot.guid = guid;
    slot.len = 0;
    if (name == nullptr)
        return;
    while (name[slot.len] != '\0' && slot.len < kNameSize - 1) {
        slot.name[slot.len] = name[slot.len];
        ++slot.len;
    }
    slot.name[slot.len] = '\0';
}

// A member's name comes from the engine's name cache record, where
// Script_UnitName (0x00517020) reads a player's: the record is absent until
// the name arrives, then final. The unit's own name getter answers a
// placeholder ("Unknown") meanwhile, which would be kept as the name. An
// absent record leaves the slot nameless, so the next rebuild asks again.
void SetMember(int index, uint64_t guid) {
    Slot &slot = g_members[index];
    if (guid != slot.guid || slot.len == 0)
        Store(slot, guid, guid != 0 ? Unit::Identity::NameForGuid(guid) : nullptr);
}

// A pet only for a slot that holds a member: `partypetN` has no bound check
// and reads the member table as given (benilla-re ui/unittokens). Its name
// comes from the unit, read again on every rebuild.
void SetPet(int index, EngineResolve resolve) {
    const uint64_t guid = g_members[index].guid != 0 ? resolve(g_petTokens[index]) : 0;
    char name[kNameSize];
    const bool known = guid != 0 && Player::Info::NameFromGuid(guid, name, sizeof name);
    Store(g_pets[index], guid, known ? name : nullptr);
}

// The slots 4.3.4 compares, in its order: you, then the raid in a raid or
// else the party.
int First(int pass) {
    return pass == 0 ? 0 : (g_inRaid ? kRaidBase : kPartyBase);
}
int End(int pass) {
    if (pass == 0)
        return 1;
    return g_inRaid ? kRaidBase + Group::Roster::RaidCount()
                    : kPartyBase + Offsets::PARTY_MAX_SLOTS;
}

void Rebuild(EngineResolve resolve) {
    g_stale = false;
    if (g_petTokens[0][0] == '\0')
        BuildPetTokens();

    // No world, no group: the resolver reads the active player for `pet`.
    if (Game::Read<void *>(Offsets::VAR_LOCAL_PLAYER_PTR) == nullptr) {
        for (int i = 0; i < kSlotCount; ++i)
            g_members[i] = g_pets[i] = {};
        g_inRaid = false;
        return;
    }

    const int raidCount = Group::Roster::RaidCount();
    g_inRaid = raidCount > 0;
    SetMember(0, Unit::Identity::PlayerGuid());
    for (int i = 0; i < Offsets::PARTY_MAX_SLOTS; ++i)
        SetMember(kPartyBase + i, g_inRaid ? 0 : Group::Roster::PartyGuid(i));
    for (int i = 0; i < Offsets::RAID_MAX_SLOTS; ++i)
        SetMember(kRaidBase + i, i < raidCount ? Group::Roster::RaidGuid(i) : 0);

    for (int pass = 0; pass < 2; ++pass)
        for (int i = First(pass); i < End(pass); ++i)
            SetPet(i, resolve);
}

const Slot *FindIn(const Slot *slots, const char *name, int len) {
    for (int pass = 0; pass < 2; ++pass)
        for (int i = First(pass); i < End(pass); ++i)
            if (slots[i].len == len && Game::StrCmpI(slots[i].name, name, len) == 0)
                return &slots[i];
    return nullptr;
}

void MarkStale() {
    g_stale = true;
}

const Tick::WorldTick::AutoSubscribe _frame{&MarkStale};
const Event::SignalHook::AutoObserve _party{Offsets::EVENT_PARTY_MEMBERS_CHANGED, &MarkStale};
const Event::SignalHook::AutoObserve _raid{Offsets::EVENT_RAID_ROSTER_UPDATE, &MarkStale};

} // namespace

bool Find(const char *name, int len, EngineResolve resolve, Match *out) {
    if (len <= 0 || len >= kNameSize)
        return false;
    if (g_stale)
        Rebuild(resolve);
    if (const Slot *member = FindIn(g_members, name, len)) {
        *out = {member->guid, g_petTokens[member - g_members]};
        return true;
    }
    if (const Slot *pet = FindIn(g_pets, name, len)) {
        *out = {pet->guid, nullptr};
        return true;
    }
    return false;
}

const char *PetTokenFor(uint64_t guid, EngineResolve resolve) {
    if (guid == 0)
        return nullptr;
    if (g_stale)
        Rebuild(resolve);
    for (int pass = 0; pass < 2; ++pass)
        for (int i = First(pass); i < End(pass); ++i)
            if (g_members[i].guid == guid)
                return g_petTokens[i];
    return nullptr;
}

} // namespace Unit::TokenNames
