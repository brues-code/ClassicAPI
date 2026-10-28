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

// `CanBeRaidTarget(unit)` — true when SetRaidTarget would mark the unit.
//
// 4.3.4's Script_CanBeRaidTarget pushes CGRaidInfo::CanBeRaidTarget. 1.12
// has no such function: its Script_SetRaidTarget (0x004BBEC0) inlines the
// same test before it sends, and shows ERR_INVALID_RAID_TARGET when it
// fails. `CanBeRaidTarget` below is that inline block, 0x004BBF48-0x004BBFD0:
//
//   a zero GUID fails; a raid or party member or pet passes; the player
//   (TYPEMASK_PLAYER) and the unit (TYPEMASK_UNIT) must both resolve; a
//   unit without the player type bit passes; a player passes when the
//   player cooperates with it, or when it is the player.

#include "Game.h"
#include "Offsets.h"
#include "object/Resolve.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Unit::RaidTargetEligible {

namespace {

using TokenToGuid_t = uint64_t(__fastcall *)(const char *token);
using IsPlayerOrPet_t = int(__fastcall *)(const uint64_t *guid);
using CanCooperate_t = bool(__fastcall *)(const void *self, void *, const void *other);

bool CanBeRaidTarget(uint64_t guid) {
    if (guid == 0)
        return false;
    if (reinterpret_cast<IsPlayerOrPet_t>(Offsets::FUN_RAID_IS_PLAYER_OR_PET)(&guid) ||
        reinterpret_cast<IsPlayerOrPet_t>(Offsets::FUN_PARTY_IS_PLAYER_OR_PET)(&guid))
        return true;

    const void *player = Object::ByGuid(Offsets::TYPEMASK_PLAYER, Unit::Identity::PlayerGuid());
    const void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid);
    if (player == nullptr || unit == nullptr)
        return false;

    const auto *fields = Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_GUID_PTR);
    if ((Game::Read<uint32_t>(fields, Offsets::OFF_OBJECT_FIELD_TYPE) &
         Offsets::TYPEMASK_PLAYER) == 0)
        return true;
    return reinterpret_cast<CanCooperate_t>(Offsets::FUN_UNIT_CAN_COOPERATE)(player, nullptr,
                                                                           unit) ||
           unit == player;
}

int __fastcall Script_CanBeRaidTarget(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: CanBeRaidTarget(\"unit\")");
        return 0;
    }
    const uint64_t guid = reinterpret_cast<TokenToGuid_t>(Offsets::FUN_TOKEN_TO_GUID)(
        Game::Lua::ToString(L, 1));
    Game::Lua::PushBool(L, CanBeRaidTarget(guid));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("unit", "string", "The unit to test."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("canBeRaidTarget", "boolean", "True if you can put a raid target icon on the unit."),
};
const Game::Doc::Function kCanBeRaidTarget{
    "Returns true if you can put a raid target icon on a unit.",
    kArgs, kRets, "UnitGlobals"};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("CanBeRaidTarget", &Script_CanBeRaidTarget,
                                      &kCanBeRaidTarget);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Unit::RaidTargetEligible
