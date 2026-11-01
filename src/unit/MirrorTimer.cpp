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

// `GetMirrorTimerInfo(index)` / `GetMirrorTimerProgress(timer)` — readers for
// the EXHAUSTION (deep-water fatigue), BREATH and FEIGNDEATH bars.
//
// 1.12's handler for the three mirror-timer SMSGs (`0x5E7990`) reads each
// packet and signals MIRROR_TIMER_START / _PAUSE / _STOP, nothing more. The
// state these functions read is what 3.3.5 added to that same handler
// (`CGGameUI::OnMirrorTimerEvent` + `m_mirrorTimerInfo`), and this file keeps
// its rules:
//   - START (0x1D9) writes the slot of its type and stamps the time.
//   - PAUSE (0x1DA) writes nothing. (Tortoise never sends it: the stock
//     handler misreads its args, so the server resends START with the paused
//     byte instead.)
//   - STOP (0x1DB) resets the slot to inactive.
//   - Leaving the world resets every slot. `CGPlayer_C::Release` (0x5E9AA0)
//     runs `StopMirrorTimers` (0x5E9B90) for the active player — STOP for all
//     three types, no packet — and then `LeaveWorldUI` (0x490A80), which
//     fires PLAYER_LEAVING_WORLD. That event is the reset point.
//
// The packet subscriber runs before the engine's handler, so a Lua handler of
// a MIRROR_TIMER_* event already reads the new state.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "event/SignalHook.h"
#include "net/PacketDispatch.h"
#include "net/PacketReader.h"
#include "time/Clock.h"

#include <cstdint>

namespace Unit::MirrorTimer {

namespace {

using TimerTypeName_t = const char *(__fastcall *)(uint32_t type);
using TimerLabel_t = const char *(__fastcall *)(uint32_t type, uint32_t spellID);

// 3.3.5's UNIT_MIRROR_TIMER_INACTIVE. The engine's name helper returns
// "UNKNOWN" for it, which is what an empty slot reports.
constexpr uint32_t kInactive = Offsets::MIRROR_TIMER_SLOT_COUNT;

// "EXHAUSTION" / "BREATH" / "FEIGNDEATH", "UNKNOWN" for any other type.
const char *TypeName(uint32_t type) {
    return reinterpret_cast<TimerTypeName_t>(Offsets::FUN_MIRROR_TIMER_TYPE_NAME)(type);
}

// The spell's name when the spell exists, else the global <NAME>_LABEL ("" if
// that is missing).
const char *Label(uint32_t type, uint32_t spellID) {
    return reinterpret_cast<TimerLabel_t>(Offsets::FUN_MIRROR_TIMER_LABEL)(type, spellID);
}

// 3.3.5's MirrorTimerInfo; the table is indexed by type.
struct Timer {
    uint32_t type = kInactive;
    int32_t value = 0;     // ms, as of `startMs`
    int32_t maxValue = 0;  // ms
    int32_t scale = 0;     // ms of value per ms of time (-1 drains, 10 refills)
    int32_t paused = 0;
    uint32_t spellID = 0;
    uint32_t startMs = 0;
};

Timer g_timers[Offsets::MIRROR_TIMER_SLOT_COUNT];

// 3.3.5's `value + scale * (now - start)`, except that a timer the server
// started paused holds its value: 3.3.5 keeps extrapolating it. An inactive
// slot (all zero) reads 0, as in 3.3.5.
int32_t Progress(const Timer &t, uint32_t nowMs) {
    if (t.paused != 0)
        return t.value;
    return t.value + t.scale * static_cast<int32_t>(Time::Clock::Elapsed(t.startMs, nowMs));
}

void OnPacket(uint32_t opcode, Net::CDataStore *packet) {
    if (opcode == Offsets::SMSG_OPCODE_MIRROR_TIMER_START) {
        Timer t;
        t.type = Net::Read<uint32_t>(packet);
        t.value = Net::Read<int32_t>(packet);
        t.maxValue = Net::Read<int32_t>(packet);
        t.scale = Net::Read<int32_t>(packet);
        t.paused = Net::Read<uint8_t>(packet);
        t.spellID = Net::Read<uint32_t>(packet);
        if (packet->m_read > packet->m_size || t.type >= Offsets::MIRROR_TIMER_SLOT_COUNT)
            return;
        t.startMs = Time::Clock::NowMs();
        g_timers[t.type] = t;
    } else if (opcode == Offsets::SMSG_OPCODE_MIRROR_TIMER_STOP) {
        const uint32_t type = Net::Read<uint32_t>(packet);
        if (packet->m_read > packet->m_size || type >= Offsets::MIRROR_TIMER_SLOT_COUNT)
            return;
        g_timers[type] = Timer{};
    }
}

const Net::PacketDispatch::AutoSubscribe _sub{&OnPacket};

int g_leavingWorldId = -1;

bool OnSignal(int eventID) {
    if (g_leavingWorldId < 0)
        g_leavingWorldId = Event::Custom::LookupByName("PLAYER_LEAVING_WORLD");
    if (eventID == g_leavingWorldId) {
        for (Timer &t : g_timers)
            t = Timer{};
    }
    return false;
}

const Event::SignalHook::AutoSubscribe _leavingWorld{&OnSignal};

// Returns 3.3.5's six values plus retail's seventh (spellID). An empty slot
// returns "UNKNOWN" and zeros, which retail's MirrorTimer.lua tests for on
// PLAYER_ENTERING_WORLD. An index outside 1..3 returns nothing, as retail
// documents (3.3.5 raised an error).
int __fastcall Script_GetMirrorTimerInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetMirrorTimerInfo(index)");
        return 0;
    }
    const int index = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    if (index < 0 || index >= Offsets::MIRROR_TIMER_SLOT_COUNT)
        return 0;
    const Timer &t = g_timers[index];
    Game::Lua::PushString(L, TypeName(t.type));
    Game::Lua::PushNumber(L, static_cast<double>(t.value));
    Game::Lua::PushNumber(L, static_cast<double>(t.maxValue));
    Game::Lua::PushNumber(L, static_cast<double>(t.scale));
    Game::Lua::PushNumber(L, static_cast<double>(t.paused));
    Game::Lua::PushString(L, Label(t.type, t.spellID));
    Game::Lua::PushNumber(L, static_cast<double>(t.spellID));
    return 7;
}

// Matches the name case-insensitively, as 3.3.5's MirrorTimerToIndex does. A
// name that is not a timer type returns nil, as retail documents (3.3.5
// raised an error).
int __fastcall Script_GetMirrorTimerProgress(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: GetMirrorTimerProgress(\"timer\")");
        return 0;
    }
    const char *name = Game::Lua::ToString(L, 1);
    for (uint32_t type = 0; type < Offsets::MIRROR_TIMER_SLOT_COUNT; ++type) {
        if (Game::StrCmpI(name, TypeName(type)) == 0) {
            Game::Lua::PushNumber(L, static_cast<double>(
                Progress(g_timers[type], Time::Clock::NowMs())));
            return 1;
        }
    }
    return 0;
}

const Game::Doc::Field kInfoArgs[] = {
    Game::Doc::Req("timerIndex", "luaIndex", "The timer slot, 1 to 3."),
};
const Game::Doc::Field kInfoRets[] = {
    Game::Doc::Req("name", "string", "EXHAUSTION, BREATH or FEIGNDEATH, or UNKNOWN for an empty slot."),
    Game::Doc::Req("startValue", "number", "The value in milliseconds when the timer started."),
    Game::Doc::Req("maxValue", "number", "The full value in milliseconds."),
    Game::Doc::Req("scale", "number", "The change in value per millisecond; negative drains."),
    Game::Doc::Req("paused", "number", "1 when the timer is paused, else 0."),
    Game::Doc::Req("label", "string", "The text the timer bar shows."),
    Game::Doc::Req("spellID", "number", "The spell that started the timer, or 0."),
};
const Game::Doc::Function kGetMirrorTimerInfo{
    "Returns the state of a mirror timer slot.", kInfoArgs, kInfoRets, "MirrorTimer"};

const Game::Doc::Field kProgressArgs[] = {
    Game::Doc::Req("timerName", "string", "EXHAUSTION, BREATH or FEIGNDEATH."),
};
const Game::Doc::Field kProgressRets[] = {
    Game::Doc::Opt("progress", "number", nullptr,
                   "The current value in milliseconds, or 0 when the timer is not running."),
};
const Game::Doc::Function kGetMirrorTimerProgress{
    "Returns the current value of a mirror timer.", kProgressArgs, kProgressRets, "MirrorTimer"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetMirrorTimerInfo", &Script_GetMirrorTimerInfo,
                                      &kGetMirrorTimerInfo);
    Game::Lua::RegisterGlobalFunction("GetMirrorTimerProgress", &Script_GetMirrorTimerProgress,
                                      &kGetMirrorTimerProgress);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::MirrorTimer
