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

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "net/PacketWriter.h"
#include "time/Clock.h"
#include "unit/Identity.h"

#include <cstdint>

// SetPVP / GetPVPDesired / GetPVPTimer / IsPVPTimerRunning.
//
// SetPVP is 3.3.5's Script_SetPVP (0x005168B0): CMSG_TOGGLE_PVP, the opcode
// TogglePVP sends empty, with one state byte. Both tortoise-wow and vmangos
// HandleTogglePvP read the byte when present and set PLAYER_FLAGS_PVP_DESIRED
// to it instead of toggling.
//
// The timer is 3.3.5's CGPlayer_C::GetPVPToggleTimer (0x006DE7A0): a client
// timestamp of now + 5 minutes, taken when PLAYER_FLAGS_PVP_DESIRED is
// cleared (SetPvpCooldownTimestamp, 0x006DE750), and the server's
// PLAYER_FLAGS 0x40000 for "running". That bit is 3.0.2; no 1.12 server sets
// it. It stands for "desired was cleared and the server still holds the PvP
// flag", which the client sees directly:
//   - armed by the engine's own ERR_PVP_TOGGLE_OFF notice, which
//     OnPlayerFlagsChanged shows only for the active player and only when the
//     desired bit was cleared, and only while UNIT_FLAG_PVP is still set;
//   - disarmed by the ERR_PVP_TOGGLE_ON notice, by OnUnitFlagsChanged's
//     UNIT_FLAG_PVP-changed branch once the active player is no longer
//     flagged (the server timer ran out), and at every glue boot (logout).
// PVP_TIMER_UPDATE("player") fires when the timer starts or stops.
// The server's 5 minutes start from its own timer, refreshed by PvP combat
// (Player::UpdatePvP); the client's countdown, like 3.3.5's, does not see
// the refresh, so it can reach zero before the flag drops.
namespace PvP::Flag {

namespace {

constexpr uint32_t CMSG_TOGGLE_PVP = 0x253;
// 3.3.5 SetPvpCooldownTimestamp's span, and GetPVPToggleTimer's answer when
// no timer runs; Script_GetPVPTimer adds a second to whatever it gets.
constexpr uint32_t TOGGLE_TIMER_MS = 300000;
constexpr int32_t TIMER_DISPLAY_PAD_MS = 1000;

bool g_running = false;
uint32_t g_deadline = 0;

const Game::Doc::Field kTimerUpdatePayload[] = {
    Game::Doc::Req("unitTarget", "UnitToken", "Always \"player\"."),
};
const Game::Doc::Event kTimerUpdateDoc{
    "Unit", "Fires when the PvP flag starts or stops counting down to off.", kTimerUpdatePayload};
const Event::Custom::AutoReserve _timerUpdate{"PVP_TIMER_UPDATE", &kTimerUpdateDoc};

void SetRunning(bool running) {
    const bool changed = running || g_running;
    g_running = running;
    if (changed)
        Event::Custom::Fire(_timerUpdate.Slot(), "%s", "player");
}

using DisplayError_t = void(__cdecl *)(uint32_t id);
using UnitFn_t = uint32_t(__thiscall *)(void *unit);

uint32_t UnitFlags(const uint8_t *unit) {
    const auto *fields = Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_DESCRIPTOR);
    return fields != nullptr ? Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FLAGS) : 0;
}

void __cdecl OnToggleOnNotice(uint32_t id) {
    reinterpret_cast<DisplayError_t>(Offsets::FUN_GAMEUI_DISPLAY_ERROR)(id);
    SetRunning(false);
}

void __cdecl OnToggleOffNotice(uint32_t id) {
    reinterpret_cast<DisplayError_t>(Offsets::FUN_GAMEUI_DISPLAY_ERROR)(id);
    const uint8_t *player = Unit::Identity::PlayerObject();
    g_deadline = Time::Clock::NowMs() + TOGGLE_TIMER_MS;
    SetRunning(player != nullptr && (UnitFlags(player) & Offsets::UNIT_FLAG_PVP) != 0);
}

void __fastcall OnPvpFlagChanged(uint8_t *unit) {
    reinterpret_cast<UnitFn_t>(Offsets::FUN_UNIT_REFRESH_REACTION)(unit);
    if ((UnitFlags(unit) & Offsets::UNIT_FLAG_PVP) == 0 &&
        reinterpret_cast<UnitFn_t>(Offsets::FUN_IS_ACTIVE_PLAYER_UNIT)(unit) != 0)
        SetRunning(false);
}

// The player's PLAYER_FLAGS, or null when there is no player.
const uint32_t *PlayerFlags() {
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return nullptr;
    const auto *info = Game::Read<const uint8_t *>(player, Offsets::OFF_CGPLAYER_INFO);
    return info != nullptr
               ? reinterpret_cast<const uint32_t *>(info + Offsets::OFF_PLAYER_INFO_FLAGS)
               : nullptr;
}

// 3.3.5 GetPVPToggleTimer: the full span when no timer runs, the time left,
// or -1 once the countdown has passed.
int32_t ToggleTimer() {
    if (!g_running)
        return TOGGLE_TIMER_MS;
    const uint32_t left = Time::Clock::Remaining(g_deadline);
    return left != 0 ? static_cast<int32_t>(left) : -1;
}

// Script_SetPVP reads a number; the modern argument is a bool, and the
// modern UI passes 1 or nil. A number counts by value, anything else by Lua
// truth.
int __fastcall Script_SetPVP(void *L) {
    const bool enable = Game::Lua::IsNumber(L, 1) ? Game::Lua::ToNumber(L, 1) != 0.0
                                                  : Game::Lua::ToBoolean(L, 1) != 0;
    Net::OutPacket packet(CMSG_TOGGLE_PVP);
    packet.PutU8(enable ? 1 : 0);
    packet.Send();
    return 0;
}

int __fastcall Script_GetPVPDesired(void *L) {
    const uint32_t *flags = PlayerFlags();
    Game::Lua::PushBool(L, flags != nullptr && (*flags & Offsets::PLAYER_FLAG_PVP_DESIRED) != 0);
    return 1;
}

int __fastcall Script_GetPVPTimer(void *L) {
    const int32_t timer =
        Unit::Identity::PlayerObject() != nullptr ? ToggleTimer() + TIMER_DISPLAY_PAD_MS : 0;
    Game::Lua::PushNumber(L, timer);
    return 1;
}

int __fastcall Script_IsPVPTimerRunning(void *L) {
    Game::Lua::PushBool(L, g_running && Unit::Identity::PlayerObject() != nullptr);
    return 1;
}

const Game::Doc::Field kSetArgs[] = {
    Game::Doc::Opt("enablePVP", "bool", "false", "True to stay flagged for PvP."),
};
const Game::Doc::Function kSetPVP{
    "Asks the server to turn the player's PvP preference on or off.", kSetArgs, {}, "PvPGlobals"};
const Game::Doc::Function kSetPVPTable{
    "Asks the server to turn the player's PvP preference on or off.", kSetArgs, {}};

const Game::Doc::Field kDesiredRets[] = {
    Game::Doc::Req("result", "bool", "True when the player chose to stay flagged for PvP."),
};
const Game::Doc::Function kGetPVPDesired{
    "Returns whether the player chose to stay flagged for PvP.", {}, kDesiredRets, "PvPGlobals"};

const Game::Doc::Field kTimerRets[] = {
    Game::Doc::Req("result", "number", "Milliseconds until the PvP flag drops."),
};
const Game::Doc::Function kGetPVPTimer{
    "Returns the milliseconds left before the PvP flag drops.", {}, kTimerRets, "PvPGlobals"};

const Game::Doc::Field kRunningRets[] = {
    Game::Doc::Req("result", "bool", "True while the PvP flag counts down to off."),
};
const Game::Doc::Function kIsPVPTimerRunning{
    "Returns whether the PvP flag is counting down to off.", {}, kRunningRets, "PvPGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("SetPVP", &Script_SetPVP, &kSetPVP);
    Game::Lua::RegisterTableFunction("C_PvP", "SetPVP", &Script_SetPVP, &kSetPVPTable);
    Game::Lua::RegisterGlobalFunction("GetPVPDesired", &Script_GetPVPDesired, &kGetPVPDesired);
    Game::Lua::RegisterGlobalFunction("GetPVPTimer", &Script_GetPVPTimer, &kGetPVPTimer);
    Game::Lua::RegisterGlobalFunction("IsPVPTimerRunning", &Script_IsPVPTimerRunning,
                                      &kIsPVPTimerRunning);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// Every glue boot: a new character starts with no timer. Glue boot always
// precedes the world, so the patches are in before any flag can change.
void OnGlueBoot() {
    g_running = false;
    Common::RedirectCall(Offsets::SITE_PVP_TOGGLE_ON_NOTICE, Offsets::FUN_GAMEUI_DISPLAY_ERROR,
                         reinterpret_cast<const void *>(&OnToggleOnNotice));
    Common::RedirectCall(Offsets::SITE_PVP_TOGGLE_OFF_NOTICE, Offsets::FUN_GAMEUI_DISPLAY_ERROR,
                         reinterpret_cast<const void *>(&OnToggleOffNotice));
    Common::RedirectCall(Offsets::SITE_UNIT_PVP_FLAG_CHANGED, Offsets::FUN_UNIT_REFRESH_REACTION,
                         reinterpret_cast<const void *>(&OnPvpFlagChanged));
}

const Game::GlueModuleAutoRegister _glueAutoreg{&OnGlueBoot};

} // namespace

} // namespace PvP::Flag
