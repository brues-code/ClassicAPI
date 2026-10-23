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

// `GetFrameCPUUsage(frame[, includeChildren])` — 3.3.5's Script_GetFrameCPUUsage
// (0x00517890): each of the frame's scripts' inclusive time and calls
// (FrameScript_Object::GetScriptTime), then, unless told not to, its child
// frames' the same way (CSimpleFrame::GetScriptTime). Every script counts,
// OnUpdate included, as retail's does.

#include "Game.h"
#include "Offsets.h"
#include "frame/ScriptSlots.h"
#include "profile/Cpu.h"

#include <cstdint>

namespace Profile::FrameTime {

namespace {

struct Totals {
    void *L;
    int64_t ticks;
    uint32_t calls;
};

void AddSlot(const uint32_t *slot, void *ctx) {
    if (slot[0] == 0)
        return;
    auto *t = static_cast<Totals *>(ctx);
    Game::Lua::RawGetI(t->L, Game::Lua::REGISTRY_INDEX, static_cast<int>(slot[0]));
    int64_t time = 0;
    int64_t totaltime = 0;
    uint32_t calls = 0;
    Cpu::FunctionTime(Game::Lua::ToPointer(t->L, -1), &time, &totaltime, &calls);
    Game::Lua::SetTop(t->L, -2);
    t->ticks += totaltime;
    t->calls += calls;
}

void AddFrame(void *frame, bool children, Totals *t) {
    Frame::ScriptSlots::ForEach(frame, &AddSlot, t);
    if (children)
        Game::ForEachFrameListObject(frame, Offsets::OFF_FRAME_CHILD_LIST,
                                     [&](void *child) { AddFrame(child, true, t); });
}

int __fastcall Script_GetFrameCPUUsage(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: GetFrameCPUUsage(frame[, includeChildren])");
        return 0;
    }
    Game::Lua::RawGetI(L, 1, 0);
    void *frame = const_cast<void *>(Game::Lua::ToPointer(L, -1));
    Game::Lua::SetTop(L, -2);
    if (frame == nullptr) {
        Game::Lua::Error(L, "GetFrameCPUUsage(): Couldn't find 'this' in frame object");
        return 0;
    }
    if (!Game::Lua::IsA(frame, Offsets::VAR_FRAME_LUA_TYPE_ID)) {
        Game::Lua::Error(L, "GetFrameCPUUsage(): Wrong object type, expected frame");
        return 0;
    }
    Totals t{L, 0, 0};
    if (Cpu::Enabled())
        AddFrame(frame, Game::Lua::ToFlag(L, 2, true), &t);
    Game::Lua::PushNumber(L, Cpu::Ms(t.ticks));
    Game::Lua::PushNumber(L, static_cast<double>(t.calls));
    return 2;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("frame", "SimpleFrame"),
    Game::Doc::Opt("includeChildren", "bool", "true", "Include the frame's child frames."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("usage", "number", "Milliseconds the frame's scripts have run."),
    Game::Doc::Req("calls", "number", "Times the frame's scripts were called."),
};
const Game::Doc::Function kGetFrameCPUUsage{
    "Returns the time a frame's scripts have run and how often they were called, while "
    "script profiling is on.",
    kArgs, kRets, "ProfilingGlobals"};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetFrameCPUUsage", &Script_GetFrameCPUUsage,
                                      &kGetFrameCPUUsage);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Profile::FrameTime
