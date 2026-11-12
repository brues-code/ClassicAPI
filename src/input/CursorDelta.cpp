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

// `GetCursorDelta()` -> deltaX, deltaY
//
// Retail's: how far the cursor moved since the last frame, zero when it did
// not, in GetCursorPosition's units (independent of the UI scale; InputUtil.
// GetCursorDelta divides by a frame's effective scale). The orbit camera of
// a ModelScene drags with it.
//
// The cursor is sampled once a frame, at the head of the UI render callback,
// before any OnUpdate runs, the way the in-game GetCursorPosition (0x0048B820)
// reads it: the top's NDC position (OFF_UI_CONTEXT_MOUSE_POSITION) through
// NDCToDDC, then the engine's script push (Game::InternalToPixel). The delta is
// this frame's sample less the last frame's, so every OnUpdate of a frame sees
// the same value.

#include "Game.h"
#include "Offsets.h"
#include "frame/Layout.h"
#include "tick/FrameTick.h"

namespace Input::CursorDelta {

namespace {

struct Position {
    double x = 0.0;
    double y = 0.0;
};

Position g_previous;
Position g_current;
bool g_sampled = false;

// What GetCursorPosition pushes now, or false before the UI top exists.
bool ReadCursor(Position &out) {
    const void *top = Game::Read<const void *>(Offsets::VAR_UI_CONTEXT_PTR);
    if (top == nullptr)
        return false;
    const float ndcX = Game::Read<float>(top, Offsets::OFF_UI_CONTEXT_MOUSE_POSITION);
    const float ndcY = Game::Read<float>(top, Offsets::OFF_UI_CONTEXT_MOUSE_POSITION + 4);
    float ddcX = 0.0f;
    float ddcY = 0.0f;
    Frame::Layout::NdcToDdc(&ddcX, &ddcY, ndcX, ndcY);
    out = {Game::InternalToPixel(ddcX), Game::InternalToPixel(ddcY)};
    return true;
}

void Sample() {
    Position now;
    if (!ReadCursor(now))
        return;
    g_previous = g_sampled ? g_current : now;
    g_current = now;
    g_sampled = true;
}

const Tick::FrameTick::AutoSubscribeBefore _tick{&Sample};

int __fastcall Script_GetCursorDelta(void *L) {
    Game::Lua::PushNumber(L, g_current.x - g_previous.x);
    Game::Lua::PushNumber(L, g_current.y - g_previous.y);
    return 2;
}

const Game::Doc::Field kRets[] = {
    Game::Doc::Req("deltaX", "number"),
    Game::Doc::Req("deltaY", "number"),
};
const Game::Doc::Function kDoc{"Returns how far the cursor moved since the last frame.", {},
                               kRets, "Input"};

void RegisterInGame() {
    Game::Lua::RegisterGlobalFunction("GetCursorDelta", &Script_GetCursorDelta, &kDoc);
}

void RegisterGlue() {
    Game::Lua::RegisterGlueFunction("GetCursorDelta", &Script_GetCursorDelta, &kDoc);
}

const Game::ModuleAutoRegister _autoreg{&RegisterInGame};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterGlue};

} // namespace

} // namespace Input::CursorDelta
