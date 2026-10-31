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

// `UNIT_DISTANCE_CHECK_UPDATE` — a group member came into view or left it.
//
// Retail's one reader, CompactUnitFrame_GetCenterStatusIconState, shows the
// other-phase icon only for a member within 250 yards
// (DISTANCE_THRESHOLD_SQUARED): close enough that the client would draw it.
// That is the server's view distance (tortoise-wow's
// Visibility.Distance.Continents and .Instances default to 250), so in 1.12
// a member is in distance exactly while the client holds its object.
//
// UpdateRaidMemberRecord (FUN_UPDATE_RAID_MEMBER_RECORD) has four callers,
// which are a player object entering the object table (PostInit, and
// PostUpdate on reactivation) and leaving it (the release slot, and
// ~CGPlayer_C for an object never released). Each call points here: a
// group member's GUID is noted, and the next world tick fires for each of
// its tokens with whether the object table holds it then. The release slot
// runs before the manager moves the object to the released table, so a fire
// in place would show the member still in view to UnitIsVisible.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "object/Resolve.h"
#include "tick/WorldTick.h"
#include "unit/Identity.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Unit::DistanceCheck {

namespace {

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("unitTarget", "UnitToken"),
    Game::Doc::Req("isInDistance", "bool"),
};
const Game::Doc::Event kDoc{"Unit", "Fires when a group member comes into view or leaves it.",
                            kPayload};
const Event::Custom::AutoReserve kEvent{"UNIT_DISTANCE_CHECK_UPDATE", &kDoc};

using UpdateRaidRecord_t = void(__fastcall *)(void *player, uint32_t mode, uint32_t withPet);
using IsGroupMember_t = int(__fastcall *)(const uint64_t *guid);

std::vector<uint64_t> g_changed;

void __fastcall UpdateRaidRecordSite(void *player, uint32_t mode, uint32_t withPet) {
    reinterpret_cast<UpdateRaidRecord_t>(Offsets::FUN_UPDATE_RAID_MEMBER_RECORD)(player, mode,
                                                                               withPet);
    if (!Event::Custom::HasListeners(kEvent.Slot()))
        return;
    const uint64_t guid = Unit::Identity::GuidForObject(player);
    if (guid == 0 || guid == Unit::Identity::PlayerGuid() ||
        reinterpret_cast<IsGroupMember_t>(Offsets::FUN_UNIT_IS_GROUP_MEMBER_OR_PET)(&guid) == 0)
        return;
    if (std::find(g_changed.begin(), g_changed.end(), guid) == g_changed.end())
        g_changed.push_back(guid);
}

void OnWorldTick() {
    if (g_changed.empty())
        return;
    std::vector<uint64_t> changed;
    changed.swap(g_changed);
    const int slot = kEvent.Slot();
    void *L = Game::Lua::State();
    if (L == nullptr || !Event::Custom::HasListeners(slot))
        return;
    const int top = Game::Lua::GetTop(L);
    char tokens[16][32];
    for (const uint64_t guid : changed) {
        const bool inView = Object::ByGuid(Offsets::TYPEMASK_PLAYER, guid) != nullptr;
        const int n = Unit::Identity::TokensForGUID(guid, tokens, 16);
        for (int i = 0; i < n; ++i) {
            Game::Lua::PushString(L, tokens[i]);
            Game::Lua::PushBool(L, inView);
            Event::Custom::FireValues(slot, L, top + 1, 2);
            Game::Lua::SetTop(L, top);
        }
    }
}

void Register() {
    const auto *site = reinterpret_cast<const void *>(&UpdateRaidRecordSite);
    Common::RedirectCall(Offsets::CALL_PLAYER_POST_INIT_RAID_RECORD,
                         Offsets::FUN_UPDATE_RAID_MEMBER_RECORD, site);
    Common::RedirectCall(Offsets::CALL_PLAYER_POST_UPDATE_RAID_RECORD,
                         Offsets::FUN_UPDATE_RAID_MEMBER_RECORD, site);
    Common::RedirectCall(Offsets::CALL_PLAYER_RELEASE_RAID_RECORD,
                         Offsets::FUN_UPDATE_RAID_MEMBER_RECORD, site);
    Common::RedirectCall(Offsets::CALL_PLAYER_DTOR_RAID_RECORD,
                         Offsets::FUN_UPDATE_RAID_MEMBER_RECORD, site);
}

const Game::ModuleAutoRegister _autoreg{&Register};
const Tick::WorldTick::AutoSubscribe _tick{&OnWorldTick};

} // namespace

} // namespace Unit::DistanceCheck
