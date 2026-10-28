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

// `InviteUnit(name)`, `UninviteUnit(name)` and `PromoteToLeader(name [,
// exactNameMatch])` — 4.3.4's group verbs (PartyFrame_Scripts.cpp,
// RaidInfo_Scripts.cpp). 1.12 splits each verb into a token global
// (`InviteToParty`, `UninviteFromParty`, `PromoteToPartyLeader`) and a name
// global (`*ByName`); every one ends in a sender on the active player that
// this file calls directly (`FUN_GROUP_*`).
//
//   - InviteUnit takes a name only, as 4.3.4 does: CGPlayer_C::Invite sends
//     the string as given. Its body is 1.12's Script_InviteByName self check
//     (ERR_INVITE_SELF) plus FUN_GROUP_INVITE_BY_NAME, which also raises the
//     invite tutorial flag the way CGPlayer_C::Invite does. The role
//     booleans (args 2-4) have no field in 1.12's CMSG_GROUP_INVITE.
//   - UninviteUnit resolves a unit token (and, on this client, a GUID
//     literal) to a GUID like 4.3.4's Script_GetGUIDFromToken; anything else
//     is a name. 4.3.4 matches that name whole against the group to get a
//     GUID; 1.12's CMSG_GROUP_UNINVITE carries the name and the server
//     makes the same match, so exactNameMatch changes nothing here. The
//     reason string has no field in 1.12's packets.
//   - PromoteToLeader is 4.3.4's ClosestObjectMatch(name, TYPEMASK_PLAYER,
//     …, exact): a token resolves to its GUID, a name goes through the
//     engine's by-name search with PromoteByName's mode (2).
//
// 4.3.4's taint gate (CanPerformAction) is dropped: 1.12 has no secure
// environment.

#include "Game.h"
#include "Offsets.h"
#include "unit/Identity.h"
#include "unit/TokenResolve.h"

#include <cstdint>
#include <cstring>

namespace Group::Invite {

namespace {

using TokenToGuid_t = uint64_t(__fastcall *)(const char *token);
using SendName_t = void(__thiscall *)(const void *player, const char *name);
using SendGuid_t = void(__thiscall *)(const void *player, uint32_t guidLo, uint32_t guidHi);
using GetName_t = const char *(__thiscall *)(const void *object, int *outFlags);
using DisplayError_t = void(__cdecl *)(int id, ...);

// The by-name search mode `Script_PromoteByName` passes (0x0048A855).
constexpr int kPromoteMatchMode = 2;

// 4.3.4 caps the name at 48 bytes before sending.
constexpr size_t kMaxInviteName = 48;

uint64_t TokenGuid(const char *token) {
    return reinterpret_cast<TokenToGuid_t>(Offsets::FUN_TOKEN_TO_GUID)(token);
}

void SendGuid(uintptr_t sender, const uint8_t *player, uint64_t guid) {
    reinterpret_cast<SendGuid_t>(sender)(player, static_cast<uint32_t>(guid),
                                         static_cast<uint32_t>(guid >> 32));
}

int __fastcall Script_InviteUnit(void *L) {
    const char *name = Game::Lua::IsString(L, 1) ? Game::Lua::ToString(L, 1) : nullptr;
    if (name == nullptr)
        return 0;
    if (strlen(name) > kMaxInviteName) {
        Game::Lua::Error(L, "Name too long");
        return 0;
    }
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return 0;
    const char *self =
        reinterpret_cast<GetName_t>(Offsets::FUN_OBJECT_GET_NAME)(player, nullptr);
    if (Game::StrCmpI(name, self) == 0) {
        reinterpret_cast<DisplayError_t>(Offsets::FUN_GAMEUI_DISPLAY_ERROR)(
            Offsets::GAME_ERROR_INVITE_SELF);
        return 0;
    }
    reinterpret_cast<SendName_t>(Offsets::FUN_GROUP_INVITE_BY_NAME)(player, name);
    return 0;
}

int __fastcall Script_UninviteUnit(void *L) {
    const char *name = Game::Lua::IsString(L, 1) ? Game::Lua::ToString(L, 1) : nullptr;
    if (name == nullptr || *name == '\0')
        return 0;
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return 0;
    if (Unit::TokenResolve::IsUnitToken(name)) {
        const uint64_t guid = TokenGuid(name);
        if (guid != 0)
            SendGuid(Offsets::FUN_GROUP_UNINVITE_BY_GUID, player, guid);
        return 0;
    }
    reinterpret_cast<SendName_t>(Offsets::FUN_GROUP_UNINVITE_BY_NAME)(player, name);
    return 0;
}

int __fastcall Script_PromoteToLeader(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: PromoteToLeader(name)");
        return 0;
    }
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return 0;
    const char *name = Game::Lua::ToString(L, 1);
    const uint64_t guid =
        Unit::TokenResolve::IsUnitToken(name)
            ? TokenGuid(name)
            : Unit::Identity::FindGuidByName(name, Offsets::TYPEMASK_PLAYER,
                                             Game::Lua::ToFlag(L, 2), kPromoteMatchMode);
    if (guid != 0)
        SendGuid(Offsets::FUN_GROUP_SET_LEADER, player, guid);
    return 0;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kInviteArgs[] = {
    Game::Doc::Req("targetName", "string", "The name of the player to invite."),
};
const Game::Doc::Function kInviteUnit{
    "Invites a player to your group by name.", kInviteArgs, {}, "PartyInfo"};

const Game::Doc::Field kUninviteArgs[] = {
    Game::Doc::Req("name", "string", "A unit token or the name of a group member."),
};
const Game::Doc::Function kUninviteUnit{
    "Removes a player from your group.", kUninviteArgs, {}, "PartyInfo"};

const Game::Doc::Field kPromoteArgs[] = {
    Game::Doc::Req("name", "string", "A unit token or the name of a group member."),
    Game::Doc::Opt("exactNameMatch", "bool", "false",
                   "When a name is given, require the whole name instead of a prefix."),
};
const Game::Doc::Function kPromoteToLeader{
    "Makes a group member the group leader.", kPromoteArgs, {}, "PartyInfo"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("InviteUnit", &Script_InviteUnit, &kInviteUnit);
    Game::Lua::RegisterGlobalFunction("UninviteUnit", &Script_UninviteUnit, &kUninviteUnit);
    Game::Lua::RegisterGlobalFunction("PromoteToLeader", &Script_PromoteToLeader,
                                      &kPromoteToLeader);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Group::Invite
