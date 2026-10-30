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

// Retail's insert mode on ScrollingMessageFrame: `SetInsertMode(mode)`,
// `GetInsertMode()` and the SCROLLING_MESSAGE_FRAME_INSERT_MODE_* globals.
//
// Retail's mixin (Blizzard_SharedXML/ScrollingMessageFrame.lua, RefreshLayout)
// stacks the lines up from the bottom in BOTTOM mode, the newest message at
// the bottom, and down from the top in TOP mode: the first line's TOPLEFT on
// the frame's TOPLEFT, each next line's TOPLEFT on the BOTTOMLEFT of the line
// before, the spacing below it. The scroll offset counts from the newest
// message in both modes. Setting the mode the frame has does nothing; a change
// lays every line out again and refreshes the display.
//
// 1.12's line layout is retail's BOTTOM mode. It anchors a line once, when it
// makes the line, through one SetPoint call (Offsets.h,
// CALL_SMF_LAYOUT_ANCHOR_LINE), so that call is redirected: a line of a frame
// in TOP mode takes the TOP anchors, every other line the engine's own. The
// layout makes the lines again when the frame changes size, which the
// redirect covers too. When a frame's mode changes, its lines are anchored
// again in order with the layout's own steps: clear the points, set the
// point, lay the line out now. The hyperlink buttons and inline textures are
// anchored to the lines, so they move with them. Retail's frame has no text
// insets; the TOP mode's first line sits the top inset below the frame's top,
// as the BOTTOM mode's sits the bottom inset above its bottom.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "frame/Destruction.h"
#include "scrollingmessageframe/DisplayRefreshed.h"

#include <cstdint>
#include <unordered_set>

namespace ScrollingMessageFrame::InsertMode {

namespace {

// SCROLLING_MESSAGE_FRAME_INSERT_MODE_TOP / _BOTTOM.
constexpr int kInsertModeTop = 1;
constexpr int kInsertModeBottom = 2;

using SetPoint_t = void(__thiscall *)(void *anchor, int point, void *relAnchor, int relPoint, float x, float y,
                                      int flag);
using ClearAllPoints_t = void(__fastcall *)(void *anchor);
using Resize_t = void(__fastcall *)(void *anchor, void *edx, int force);
using PixelAlignY_t = float(__stdcall *)(float length);

// The frames in TOP mode. Every frame is rebuilt on /reload, so the set is
// cleared then.
std::unordered_set<void *> g_topFrames;

bool IsTop(void *smf) { return g_topFrames.count(smf) != 0; }

void *AnchorOf(void *region) { return static_cast<uint8_t *>(region) + Offsets::OFF_REGION_ANCHOR; }

// Sets a line's anchor as the layout gives it - (BOTTOMLEFT, the frame,
// BOTTOMLEFT, x, y) for the first line, (BOTTOMLEFT, the line before, TOPLEFT,
// x, y) for the others - or, for a frame in TOP mode, its mirror.
void SetLinePoint(void *smf, void *anchor, int point, void *relative, int relativePoint, float x, float y,
                  int flag) {
    if (IsTop(smf)) {
        point = Offsets::FRAMEPOINT_TOPLEFT;
        if (relativePoint == Offsets::FRAMEPOINT_BOTTOMLEFT) { // the first line, on the frame
            relativePoint = Offsets::FRAMEPOINT_TOPLEFT;
            y = -Game::Read<float>(smf, Offsets::OFF_SCROLLINGMESSAGEFRAME_INSET_TOP);
        } else {
            relativePoint = Offsets::FRAMEPOINT_BOTTOMLEFT;
            y = -y;
        }
    }
    reinterpret_cast<SetPoint_t>(Offsets::FUN_REGION_SET_POINT)(anchor, point, relative, relativePoint, x, y, flag);
}

// The layout's SetPoint for a line it has just made; the line's parent is
// the frame.
void __fastcall AnchorNewLine(void *anchor, void *, int point, void *relative, int relativePoint, float x, float y,
                              int flag) {
    void *line = static_cast<uint8_t *>(anchor) - Offsets::OFF_REGION_ANCHOR;
    SetLinePoint(Game::Read<void *>(line, Offsets::OFF_REGION_PARENT), anchor, point, relative, relativePoint, x, y,
                 flag);
}

// Anchors the frame's lines again, in order, with the layout's arguments and
// steps.
void AnchorLines(void *smf) {
    const int count = Game::Read<int>(smf, Offsets::OFF_SMF_LINE_COUNT);
    const auto *lines = Game::Read<const uint8_t *>(smf, Offsets::OFF_SMF_LINE_ARRAY);
    void *previous = nullptr;
    for (int i = 0; i < count; ++i) {
        void *line = Game::Read<void *>(lines + i * Offsets::SMF_LINE_STRIDE, Offsets::OFF_SMF_LINE_FONTSTRING);
        void *anchor = AnchorOf(line);
        reinterpret_cast<ClearAllPoints_t>(Offsets::FUN_LAYOUT_CLEAR_ALL_POINTS)(anchor);
        if (previous == nullptr)
            SetLinePoint(smf, anchor, Offsets::FRAMEPOINT_BOTTOMLEFT, AnchorOf(smf), Offsets::FRAMEPOINT_BOTTOMLEFT,
                         Game::Read<float>(smf, Offsets::OFF_SCROLLINGMESSAGEFRAME_INSET_LEFT),
                         Game::Read<float>(smf, Offsets::OFF_SCROLLINGMESSAGEFRAME_INSET_BOTTOM), 1);
        else
            SetLinePoint(smf, anchor, Offsets::FRAMEPOINT_BOTTOMLEFT, AnchorOf(previous), Offsets::FRAMEPOINT_TOPLEFT,
                         0.0f,
                         reinterpret_cast<PixelAlignY_t>(Offsets::FUN_PIXEL_ALIGN_Y)(
                             Game::Read<float>(line, Offsets::OFF_FONTSTRING_SPACING)),
                         1);
        reinterpret_cast<Resize_t>(Offsets::FUN_LAYOUT_RESIZE)(anchor, nullptr, 1);
        previous = line;
    }
}

int __fastcall Script_SetInsertMode(void *L) {
    void *frame = Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_SCROLLINGMESSAGEFRAME_LUA_TYPE_ID);
    if (frame == nullptr)
        return 0;
    const double mode = Game::Lua::IsNumber(L, 2) ? Game::Lua::ToNumber(L, 2) : 0;
    if (mode != kInsertModeTop && mode != kInsertModeBottom) {
        Game::Lua::Error(L,
                         "Usage: %s:SetInsertMode(SCROLLING_MESSAGE_FRAME_INSERT_MODE_TOP or "
                         "SCROLLING_MESSAGE_FRAME_INSERT_MODE_BOTTOM)",
                         Game::Lua::ObjectDisplayName(frame));
        return 0;
    }
    const bool top = mode == kInsertModeTop;
    if (top == IsTop(frame))
        return 0;
    if (top)
        g_topFrames.insert(frame);
    else
        g_topFrames.erase(frame);
    AnchorLines(frame);
    DisplayRefreshed::MarkDirty(frame);
    return 0;
}

int __fastcall Script_GetInsertMode(void *L) {
    void *frame = Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_SCROLLINGMESSAGEFRAME_LUA_TYPE_ID);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushNumber(L, IsTop(frame) ? kInsertModeTop : kInsertModeBottom);
    return 1;
}

const Game::Lua::FrameMethodEntry kMethods[] = {
    {"SetInsertMode", &Script_SetInsertMode},
    {"GetInsertMode", &Script_GetInsertMode},
};

const Game::Doc::Field kModeArgs[] = {
    Game::Doc::Req("mode", "number",
                   "SCROLLING_MESSAGE_FRAME_INSERT_MODE_TOP or SCROLLING_MESSAGE_FRAME_INSERT_MODE_BOTTOM."),
};
const Game::Doc::Field kModeRets[] = {
    Game::Doc::Req("mode", "number", "BOTTOM until SetInsertMode changes it."),
};
const Game::Doc::Function kSetInsertMode{
    "Sets where the newest message shows: at the top, with older messages below it, or at the bottom.", kModeArgs,
    {}};
const Game::Doc::Function kGetInsertMode{"Returns where the newest message shows.", {}, kModeRets};

const Game::Doc::Method kDocs[] = {
    {"SetInsertMode", &kSetInsertMode},
    {"GetInsertMode", &kGetInsertMode},
};

void RegisterLuaFunctions() {
    void *L = Game::Lua::State();
    Game::Lua::SetGlobalNumber(L, "SCROLLING_MESSAGE_FRAME_INSERT_MODE_TOP", kInsertModeTop);
    Game::Lua::SetGlobalNumber(L, "SCROLLING_MESSAGE_FRAME_INSERT_MODE_BOTTOM", kInsertModeBottom);
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_SCROLLINGMESSAGEFRAME_METHOD_REGISTRY),
                                    kMethods, 2, kDocs, 2);
    Common::RedirectCall(Offsets::CALL_SMF_LAYOUT_ANCHOR_LINE, Offsets::FUN_REGION_SET_POINT,
                         reinterpret_cast<const void *>(&AnchorNewLine));
}

void PrepareForReload() { g_topFrames.clear(); }

void OnFrameDestroyed(void *frame) { g_topFrames.erase(frame); }

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::ReloadAutoRegister _reload{&PrepareForReload};
const Frame::Destruction::AutoSubscribe _destructionSub{&OnFrameDestroyed};

} // namespace

} // namespace ScrollingMessageFrame::InsertMode
