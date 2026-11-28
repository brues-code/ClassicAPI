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

#include "Game.h"
#include "Offsets.h"
#include "group/Roster.h"
#include "lua/Arg.h"
#include "map/Instance.h"
#include "unit/Identity.h"

#include <cstdint>

// `UnitInBattleground([unit, partyIndex])` -> position.
//
// As the 3.80.2 client's (Script_UnitInBattleground 0x140E393C0) does it:
//   - The unit and the party index are both optional generated-reader
//     arguments (0x140B296B0 unit, 0x140B0B220 luaIndex). A missing unit is
//     no unit, so the answer is nil.
//   - The group is partyIndex's (1 home, 2 instance), or with none given the
//     instance group when there is one, else the home group.
//   - The answer is nil unless the current map's Map.dbc instance type is
//     3 or 4 (0x1416A32C0, the PvP-map test 3.3.5 calls CGRaidInfo::IsPVPRaid).
//   - It walks the group's member list and pushes the first slot whose GUID
//     is the unit's, as a luaIndex (slot + 1, 0x140A936B0). No match is nil.
//
// 1.12.1 has one group, so the home group is the raid roster
// (VAR_RAID_MEMBER_PTRS, Group::Roster) and partyIndex 2 has no group, so it
// is nil. The server puts every battleground player in a raid, and the
// roster holds the player too, as 3.80.2's member list does, so "player"
// answers its own slot. A party is not a raid roster, so it answers nil, as
// the 3.3.5 client's raid-only walk (0x0060CAA0) does. 1.12's Map.dbc has no
// type 4; arenas carry type 3 (PvP::Arena), so the type-3 test covers both.
//
// The slot is raidN's N: GetRaidRosterInfo(position) is that member.

namespace PvP::Battleground {

namespace {

constexpr const char *kUsage = "Usage: local result = UnitInBattleground([unit, partyIndex])";

// The home group's index (zero-based, after the luaIndex reader's - 1).
constexpr uint32_t kHomeGroup = 0;

int __fastcall Script_UnitInBattleground(void *L) {
    Game::Lua::Arg::Text token;
    Game::Lua::Arg::String(L, 1, kUsage, token, true);

    uint32_t group = kHomeGroup;
    if (Game::Lua::Type(L, 2) > Game::Lua::TYPE_NIL)
        Game::Lua::Arg::Index(L, 2, kUsage, group);

    const uint64_t guid = token.data != nullptr ? Unit::Identity::GuidForToken(token.data) : 0;
    if (group == kHomeGroup && guid != 0 &&
        Map::Instance::CurrentType() == Offsets::MAP_INSTANCE_TYPE_BATTLEGROUND) {
        for (int i = 0, n = Group::Roster::RaidCount(); i < n; ++i) {
            if (Group::Roster::RaidGuid(i) == guid) {
                Game::Lua::PushNumber(L, static_cast<double>(i + 1));
                return 1;
            }
        }
    }
    Game::Lua::PushNil(L);
    return 1;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Opt("unit", "UnitToken"),
    Game::Doc::Opt("partyIndex", "luaIndex", nullptr,
                   "1 for your own group; 2 for an instance group, which is always nil here."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("result", "luaIndex", nullptr,
                   "The unit's raid slot (raidN's N) while you are on a battleground or arena map."),
};
const Game::Doc::Function kDoc{
    "Returns the raid slot of a unit in your battleground group.", kArgs, kRets, "UnitGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitInBattleground", &Script_UnitInBattleground, &kDoc);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace PvP::Battleground
